#include "../src/QuadRuntime.h"
#include "../src/openxr/OpenXRRuntimePolicy.h"
#include "../src/openxr/DisplayFormat.h"
#include "../src/vulkan/GpuRetirement.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <new>
#include <stdexcept>

namespace argent {
namespace {
enum class Scenario {
    Normal, Enable1, CachedDevice, RequirementsFailure, SelectionFailure, UuidMismatch,
    MissingModule, MissingExport, MissingDownstream, SimulatorMissingModule,
    MissingPublicProperties, MissingNextProperties, SessionFailure, FormatCountFailure,
    FormatFillFailure, PauseLogFailure, ViewFailure, LocalFailure, PoolFailure,
    CommandFailure, LoaderDataFailure, FenceFailure, Timing, MissingQueueProperties,
    TimingLogFailure, FinalLogFailure
};
constexpr std::array<const char*,26> names{{"normal","enable1","cached-device","requirements-failed",
    "selection-failed","uuid-mismatch","missing-module","missing-export","missing-downstream",
    "simulator-missing-module","missing-public-properties","missing-next-properties","session-failed",
    "format-count-failed","format-fill-failed","pause-log-failed","view-failed","local-failed",
    "pool-failed","command-failed","loader-data-failed","fence-failed","timing",
    "missing-queue-properties","timing-log-failed","final-log-failed"}};
Scenario scenario{};
enum class SwapFailure { None, FormatCount, FormatFill, UnsupportedFormat, Create, ImageCount, ImageFill, Log };
struct SwapScenario { uint32_t layers=1;SwapFailure failure{};unsigned eye{};bool upscale{},fallback{},mirror{}; } swapTest;
std::array<bool,16> ownedSwapchains{};
unsigned createdSwapchains{},destroyedSwapchains{},swapAttempts{},handsStops{};
bool handsOwned{};
XrSwapchain swapchain{};
VkExtent2D extent{},sourceExtent{};
VkFormat format{},compositionFormat{};
uint32_t swapchainLayers{};
bool swapchainFsrRequested{};
std::vector<XrSwapchainImageVulkanKHR> images;
struct EyeSwapchain { XrSwapchain handle{};std::vector<XrSwapchainImageVulkanKHR> images;std::vector<bool> initialized; };
std::array<EyeSwapchain,2> stereoEyes;
Source expectedSource;
bool enable2{},simulator{},running{},failed{},gpuLive{},pauseOwned{};
constexpr bool cleanRelease=false;
unsigned createdSessions{},createdSpaces{},createdPools{},createdCommands{},createdFences{},propertiesCalls{},selections{};
unsigned destroyedSessions{},destroyedSpaces{},destroyedPools{},destroyedFences{},waits{},pauseStops{},actionStops{};
XrInstance instance{};
XrSystemId system=1;
XrSession session{};
XrSpace space{},localSpace{};
VkDevice boundDevice{};
VkQueue boundQueue{};
uint32_t boundFamily{},copyTimingStride{};
VkCommandPool pool{};
VkCommandBuffer command{};
VkFence fence{};
VkPhysicalDevice runtimeSelectedPhysical{};
kharvox::OpenXRRuntimeKind runtimeKind=kharvox::OpenXRRuntimeKind::Unknown;
Device device;
std::recursive_mutex mutex;
std::atomic<VkDevice> runtimeDevice{};
std::atomic<PFN_vkGetDeviceProcAddr> runtimeGdpa{};
void require(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
template<class T> T handle(std::uintptr_t value) { return reinterpret_cast<T>(value); }
bool timingsRequested() {
    return scenario==Scenario::Timing||scenario==Scenario::MissingQueueProperties||scenario==Scenario::TimingLogFailure;
}
bool environmentFlag(const char*) { return false; }
void check(XrResult result,const char* operation) { if(XR_FAILED(result))throw std::runtime_error(operation); }
void checkVk(VkResult result,const char* operation) { if(result!=VK_SUCCESS)throw std::runtime_error(operation); }
VKAPI_ATTR void VKAPI_CALL fakeProperties(VkPhysicalDevice physical,VkPhysicalDeviceProperties2* properties) {
    ++propertiesCalls;
    require(physical==handle<VkPhysicalDevice>(2)||physical==handle<VkPhysicalDevice>(4),"Unexpected physical-device dispatch");
    auto* id=reinterpret_cast<VkPhysicalDeviceIDProperties*>(properties->pNext);
    id->deviceUUID[0]=physical==handle<VkPhysicalDevice>(2)&&scenario==Scenario::UuidMismatch?8:7;
    properties->properties.limits.timestampPeriod=1;
    strcpy_s(properties->properties.deviceName,"Fixture GPU");
}
VKAPI_ATTR void VKAPI_CALL fakeQueueProperties(VkPhysicalDevice,uint32_t* count,VkQueueFamilyProperties* out) {
    *count=2;if(out)for(unsigned i=0;i<2;++i)out[i].timestampValidBits=64;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeNext(VkInstance,const char* name) {
    if(!std::strcmp(name,"vkGetPhysicalDeviceProperties2KHR"))
        return scenario==Scenario::MissingNextProperties?nullptr:reinterpret_cast<PFN_vkVoidFunction>(fakeProperties);
    if(!std::strcmp(name,"vkGetPhysicalDeviceQueueFamilyProperties"))
        return scenario==Scenario::MissingQueueProperties?nullptr:reinterpret_cast<PFN_vkVoidFunction>(fakeQueueProperties);
    return nullptr;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakePublic(VkInstance,const char* name) {
    if(!std::strcmp(name,"vkGetPhysicalDeviceProperties2KHR"))
        return scenario==Scenario::MissingPublicProperties?nullptr:reinterpret_cast<PFN_vkVoidFunction>(fakeProperties);
    return nullptr;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeDeviceProc(VkDevice,const char*) { return nullptr; }
HMODULE fakeModule(LPCWSTR) {
    return scenario==Scenario::MissingModule||scenario==Scenario::SimulatorMissingModule?nullptr:handle<HMODULE>(1);
}
FARPROC fakeExport(HMODULE module,LPCSTR) {
    return !module||scenario==Scenario::MissingExport?nullptr:reinterpret_cast<FARPROC>(fakePublic);
}
DWORD fakeEnvironment(LPCSTR name,LPSTR value,DWORD size) {
    if(swapTest.mirror&&!std::strcmp(name,"ARGENT_DESKTOP_MIRROR")&&size>=2){value[0]='1';value[1]=0;return 1;}
    return 0;
}
XrResult fake_xrGetVulkanGraphicsRequirementsKHR(XrInstance,XrSystemId,XrGraphicsRequirementsVulkanKHR*) {
    return scenario==Scenario::RequirementsFailure?XR_ERROR_RUNTIME_FAILURE:XR_SUCCESS;
}
XrResult fake_xrGetVulkanGraphicsRequirements2KHR(XrInstance a,XrSystemId b,XrGraphicsRequirementsVulkanKHR* out) {
    return fake_xrGetVulkanGraphicsRequirementsKHR(a,b,out);
}
XrResult fake_xrGetVulkanGraphicsDeviceKHR(XrInstance,XrSystemId,VkInstance,VkPhysicalDevice* out) {
    ++selections;if(scenario==Scenario::SelectionFailure)return XR_ERROR_RUNTIME_FAILURE;
    *out=handle<VkPhysicalDevice>(2);return XR_SUCCESS;
}
XrResult fake_xrGetVulkanGraphicsDevice2KHR(XrInstance a,const XrVulkanGraphicsDeviceGetInfoKHR*,VkPhysicalDevice* out) {
    return fake_xrGetVulkanGraphicsDeviceKHR(a,system,device.instance,out);
}
XrResult fake_xrCreateSession(XrInstance,const XrSessionCreateInfo* info,XrSession* out) {
    require(propertiesCalls==2,"Session bypassed UUID verification");
    const auto& binding=*reinterpret_cast<const XrGraphicsBindingVulkanKHR*>(info->next);
    require(binding.device==device.device&&binding.physicalDevice==handle<VkPhysicalDevice>(2)
        &&binding.queueFamilyIndex==1&&binding.queueIndex==0,"Session binding changed device dispatch ownership");
    if(scenario==Scenario::SessionFailure)return XR_ERROR_RUNTIME_FAILURE;
    ++createdSessions;*out=handle<XrSession>(7);return XR_SUCCESS;
}
XrResult fake_xrEnumerateSwapchainFormats(XrSession,uint32_t capacity,uint32_t* count,int64_t* out) {
    if((!capacity&&(scenario==Scenario::FormatCountFailure||swapTest.failure==SwapFailure::FormatCount))
        ||(capacity&&(scenario==Scenario::FormatFillFailure||swapTest.failure==SwapFailure::FormatFill)))
        return XR_ERROR_RUNTIME_FAILURE;
    const std::array<int64_t,4> formats{{VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R8G8B8A8_SRGB,
        VK_FORMAT_B8G8R8A8_UNORM,VK_FORMAT_B8G8R8A8_SRGB}};
    *count=swapTest.failure==SwapFailure::UnsupportedFormat?1:uint32_t(formats.size());
    if(out){
        require(capacity>=*count,"Format enumeration exceeded capacity");
        if(swapTest.failure==SwapFailure::UnsupportedFormat)*out=VK_FORMAT_R16G16B16A16_SFLOAT;
        else std::copy(formats.begin(),formats.end(),out);
    }
    return XR_SUCCESS;
}
XrResult fake_xrCreateSwapchain(XrSession,const XrSwapchainCreateInfo* info,XrSwapchain* out) {
    const auto eye=swapAttempts++%(swapTest.layers==2?2:1);
    const auto scale=swapTest.upscale&&!swapTest.fallback&&swapTest.layers==2?2u:1u;
    const auto usage=XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT
        |(swapTest.layers==2&&swapTest.mirror?XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT:0);
    require(info->width==expectedSource.extent.width*scale&&info->height==expectedSource.extent.height*scale
        &&info->format==xrDisplayFormat(expectedSource.format,expectedSource.displaySrgb)
        &&info->arraySize==(swapTest.layers==2?1:swapTest.layers)&&info->usageFlags==usage
        &&info->sampleCount==1&&info->mipCount==1&&info->faceCount==1,"Composition swapchain extent or usage changed");
    if(swapTest.failure==SwapFailure::Create&&eye==swapTest.eye)return XR_ERROR_RUNTIME_FAILURE;
    require(createdSwapchains<ownedSwapchains.size(),"Fixture swapchain capacity exceeded");
    ownedSwapchains[createdSwapchains]=true;
    *out=handle<XrSwapchain>(100+createdSwapchains++);return XR_SUCCESS;
}
XrResult fake_xrEnumerateSwapchainImages(XrSwapchain value,uint32_t capacity,uint32_t* count,XrSwapchainImageBaseHeader* out) {
    const auto index=reinterpret_cast<std::uintptr_t>(value)-100;
    require(index<createdSwapchains&&ownedSwapchains[index],"Images enumerated from an unowned swapchain");
    const auto eye=unsigned(index)%(swapTest.layers==2?2:1);
    if(eye==swapTest.eye&&((!capacity&&swapTest.failure==SwapFailure::ImageCount)
        ||(capacity&&swapTest.failure==SwapFailure::ImageFill)))return XR_ERROR_RUNTIME_FAILURE;
    *count=3;
    if(out){
        require(capacity>=*count,"Image enumeration exceeded capacity");
        auto* vulkan=reinterpret_cast<XrSwapchainImageVulkanKHR*>(out);
        for(unsigned i=0;i<*count;++i){require(vulkan[i].type==XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR,"Image type missing");vulkan[i].image=handle<VkImage>(200+index*3+i);}
    }
    return XR_SUCCESS;
}
XrResult fake_xrDestroySwapchain(XrSwapchain value) {
    const auto index=reinterpret_cast<std::uintptr_t>(value)-100;
    require(!gpuLive&&index<createdSwapchains&&ownedSwapchains[index],"Swapchain destroyed early or twice");
    ownedSwapchains[index]=false;++destroyedSwapchains;return XR_SUCCESS;
}
struct Fsr {
    bool ready{};
    bool active() const { return ready; }
    void releaseAfterCompletion() { require(!gpuLive,"FSR resources destroyed before retirement");ready=false; }
} fsr1;
bool fsrRequested() { return swapTest.upscale; }
VkExtent2D fsrOutput(const Source& source,VkFormat) {
    fsr1.ready=!swapTest.fallback;
    return fsr1.ready?VkExtent2D{source.extent.width*2,source.extent.height*2}:source.extent;
}
void releaseHands() { require(!gpuLive,"Hands destroyed before retirement");handsStops+=handsOwned;handsOwned=false; }
bool initializePauseBindings(const std::vector<int64_t>&) { pauseOwned=true;return true; }
XrResult fake_xrCreateReferenceSpace(XrSession,const XrReferenceSpaceCreateInfo* info,XrSpace* out) {
    if((info->referenceSpaceType==XR_REFERENCE_SPACE_TYPE_VIEW&&scenario==Scenario::ViewFailure)
        ||(info->referenceSpaceType==XR_REFERENCE_SPACE_TYPE_LOCAL&&scenario==Scenario::LocalFailure))return XR_ERROR_RUNTIME_FAILURE;
    ++createdSpaces;*out=handle<XrSpace>(info->referenceSpaceType==XR_REFERENCE_SPACE_TYPE_VIEW?8:9);return XR_SUCCESS;
}
VkResult fake_vkCreateCommandPool(VkDevice,const VkCommandPoolCreateInfo*,const VkAllocationCallbacks*,VkCommandPool* out) {
    if(scenario==Scenario::PoolFailure)return VK_ERROR_OUT_OF_HOST_MEMORY;
    ++createdPools;*out=handle<VkCommandPool>(10);return VK_SUCCESS;
}
VkResult fake_vkAllocateCommandBuffers(VkDevice,const VkCommandBufferAllocateInfo*,VkCommandBuffer* out) {
    if(scenario==Scenario::CommandFailure)return VK_ERROR_OUT_OF_HOST_MEMORY;
    ++createdCommands;*out=handle<VkCommandBuffer>(11);return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL fakeLoaderData(VkDevice,void*) {
    return scenario==Scenario::LoaderDataFailure?VK_ERROR_OUT_OF_HOST_MEMORY:VK_SUCCESS;
}
VkResult fake_vkCreateFence(VkDevice,const VkFenceCreateInfo*,const VkAllocationCallbacks*,VkFence* out) {
    if(scenario==Scenario::FenceFailure)return VK_ERROR_OUT_OF_HOST_MEMORY;
    ++createdFences;*out=handle<VkFence>(12);return VK_SUCCESS;
}
struct Timing {
    VkQueryPool pool{};float period{};uint32_t bits{};
    void initialize(VkDevice,PFN_vkGetDeviceProcAddr,float periodNs,uint32_t validBits) {
        pool=handle<VkQueryPool>(13);period=periodNs;bits=validBits;
    }
    void shutdownAfterCompletion() { require(!gpuLive,"Timing resources destroyed before retirement");pool=VK_NULL_HANDLE; }
} copyTiming,diagnosticCopyTiming;
struct Actions {
    bool ready{};
    void create() { ready=true; }
    void destroy() { ready=false;++actionStops; }
} controllerActions;
struct Bridge { void destroy(Device&) { require(!gpuLive,"Bridge destroyed before retirement"); } } bridge;
void releasePauseBindings() { require(!gpuLive,"Pause image destroyed before retirement");if(pauseOwned)++pauseStops;pauseOwned=false; }
void cancelStereoImpl() {}
VkResult fake_vkDeviceWaitIdle(VkDevice) { ++waits;gpuLive=false;return VK_SUCCESS; }
XrResult fake_xrDestroySpace(XrSpace) { require(!gpuLive,"Space destroyed before retirement");++destroyedSpaces;return XR_SUCCESS; }
XrResult fake_xrDestroySession(XrSession) { require(!gpuLive,"Session destroyed before retirement");++destroyedSessions;return XR_SUCCESS; }
void fake_vkDestroyFence(VkDevice,VkFence,const VkAllocationCallbacks*) { require(!gpuLive,"Fence destroyed before retirement");++destroyedFences; }
void fake_vkDestroyCommandPool(VkDevice,VkCommandPool,const VkAllocationCallbacks*) { require(!gpuLive,"Pool destroyed before retirement");++destroyedPools; }
}
namespace perf { bool enabled() { return timingsRequested(); } }
void log(const std::string& message) {
    if((scenario==Scenario::PauseLogFailure&&message.rfind("PAUSE_BINDINGS ",0)==0)
        ||(scenario==Scenario::TimingLogFailure&&message.rfind("PERF_XR_GPU_SUPPORT ",0)==0)
        ||(scenario==Scenario::FinalLogFailure&&message=="XR_SESSION created: Vulkan composition ready")
        ||(swapTest.failure==SwapFailure::Log&&(message.rfind("XR_SWAPCHAIN ",0)==0||message.rfind("XR_EYE_SWAPCHAINS ",0)==0)))throw std::bad_alloc{};
}
namespace {
#define XR(name) fake_##name
#define VK(name) fake_##name
#define GetModuleHandleW fakeModule
#define GetProcAddress fakeExport
#define GetEnvironmentVariableA fakeEnvironment
#include "../src/XrSwapchainCreation.inc"
#include "../src/XrSessionCreation.inc"
#undef GetEnvironmentVariableA
#undef GetProcAddress
#undef GetModuleHandleW
#include "../src/XrShutdown.inc"
#undef VK
#undef XR

bool expectedFailure(Scenario mode) {
    return mode!=Scenario::Normal&&mode!=Scenario::Enable1&&mode!=Scenario::CachedDevice
        &&mode!=Scenario::SimulatorMissingModule&&mode!=Scenario::Timing&&mode!=Scenario::MissingQueueProperties;
}
Device reset(Scenario mode) {
    scenario=mode;enable2=mode!=Scenario::Enable1;simulator=mode==Scenario::SimulatorMissingModule;
    running=failed=gpuLive=pauseOwned=false;
    createdSessions=createdSpaces=createdPools=createdCommands=createdFences=propertiesCalls=selections=0;
    destroyedSessions=destroyedSpaces=destroyedPools=destroyedFences=waits=pauseStops=actionStops=0;
    session=XR_NULL_HANDLE;space=localSpace=XR_NULL_HANDLE;boundDevice=VK_NULL_HANDLE;boundQueue=VK_NULL_HANDLE;
    pool=VK_NULL_HANDLE;fence=VK_NULL_HANDLE;command=VK_NULL_HANDLE;controllerActions={};copyTiming={};diagnosticCopyTiming={};
    swapTest={};ownedSwapchains={};createdSwapchains=destroyedSwapchains=swapAttempts=handsStops=0;
    handsOwned=swapchainFsrRequested=false;swapchain=XR_NULL_HANDLE;extent=sourceExtent={};
    format=compositionFormat=VK_FORMAT_UNDEFINED;swapchainLayers=0;images={};stereoEyes={};fsr1={};expectedSource={};
    runtimeSelectedPhysical=mode==Scenario::CachedDevice?handle<VkPhysicalDevice>(2):VK_NULL_HANDLE;
    instance=handle<XrInstance>(1);
    Device input;input.instance=handle<VkInstance>(3);input.physical=handle<VkPhysicalDevice>(4);
    input.device=handle<VkDevice>(5);input.graphicsQueue=handle<VkQueue>(6);input.graphicsFamily=1;
    input.gipa=mode==Scenario::MissingDownstream?nullptr:fakeNext;input.gdpa=fakeDeviceProc;input.setLoaderData=fakeLoaderData;
    runtimeDevice=input.device;runtimeGdpa=input.gdpa;
    return input;
}
void verifyShutdown(const Device& input) {
    const bool bound=createdSessions!=0;
    const auto expectedPauseStops=pauseOwned?1u:0u;
    gpuLive=bound;
    shutdownXRImpl(input.device);
    require(waits==unsigned(bound)&&actionStops==unsigned(bound)&&destroyedSessions==createdSessions
        &&destroyedSpaces==createdSpaces&&destroyedPools==createdPools&&destroyedFences==createdFences
        &&pauseStops==expectedPauseStops&&destroyedSwapchains==createdSwapchains,"Partial construction bypassed retirement or resource cleanup");
    require(!gpuLive&&!pauseOwned&&!session&&!space&&!localSpace&&!boundDevice&&!pool&&!fence
        &&!copyTiming.pool&&!diagnosticCopyTiming.pool&&!controllerActions.ready&&!fsr1.active()&&!handsOwned
        &&!swapchain&&images.empty()&&std::none_of(ownedSwapchains.begin(),ownedSwapchains.end(),[](bool live){return live;}),
        "Shutdown retained partial resources");
    for(const auto& eye:stereoEyes)require(!eye.handle&&eye.images.empty()&&eye.initialized.empty(),"Partial eye resources survived shutdown");
    shutdownXRImpl(input.device);
    require(waits==unsigned(bound)&&destroyedSessions==createdSessions&&destroyedPools==createdPools
        &&destroyedSwapchains==createdSwapchains,"Repeated shutdown destroyed a partially constructed resource twice");
}
void run(Scenario mode) {
    auto input=reset(mode);
    bool rejected{};
    try { startSession(input,input.graphicsQueue,1,0); }
    catch(const std::exception&) { rejected=true; }
    require(rejected==expectedFailure(mode),"Session creation accepted unavailable requirements or rejected a usable path");
    require(selections==(mode==Scenario::RequirementsFailure||mode==Scenario::CachedDevice?0u:1u),"Device selection call count changed");
    const bool bound=createdSessions!=0;
    require(bool(session)==bound&&bool(boundDevice)==bound,"Partial session was not published to its teardown owner");
    if(!rejected)require(createdSessions==1&&createdSpaces==2&&createdPools==1&&createdCommands==1&&createdFences==1
        &&controllerActions.ready&&pauseOwned,"Complete setup skipped an owned resource");
    if(mode==Scenario::UuidMismatch||mode==Scenario::MissingModule||mode==Scenario::MissingExport
        ||mode==Scenario::MissingDownstream||mode==Scenario::MissingPublicProperties||mode==Scenario::MissingNextProperties)
        require(!bound&&!createdSpaces&&!createdPools&&!createdFences&&!pauseOwned,"UUID verification failure created resources");
    if(mode==Scenario::MissingQueueProperties)require(!diagnosticCopyTiming.pool,"Unavailable optional timing dispatch was used");
    if(mode==Scenario::Timing)require(diagnosticCopyTiming.pool&&diagnosticCopyTiming.period==1&&diagnosticCopyTiming.bits==64,
        "Available diagnostic timing was disabled");
    verifyShutdown(input);
    std::cout<<names[size_t(mode)]<<" rejected="<<rejected<<" session="<<createdSessions<<" spaces="<<createdSpaces
        <<" pools="<<createdPools<<" fences="<<createdFences<<" retirement="<<waits<<std::endl;
}
void runSwapchain(const SwapScenario& test) {
    auto input=reset(Scenario::Normal);
    startSession(input,input.graphicsQueue,1,0);
    swapTest=test;
    Source source;source.extent={1280,720};source.format=VK_FORMAT_R8G8B8A8_UNORM;source.displaySrgb=true;
    expectedSource=source;
    bool rejected{};
    try { createSwapchain(source,test.layers,test.upscale&&test.layers==2); }
    catch(const std::exception&) { rejected=true; }
    require(rejected==(test.failure!=SwapFailure::None),"Swapchain creation accepted failure or rejected usable setup");
    if(!rejected){
        const auto count=test.layers==2?2u:1u;
        require(createdSwapchains==count&&compositionFormat==VK_FORMAT_R8G8B8A8_SRGB&&format==source.format
            &&swapchainLayers==test.layers,"Composition format or eye count changed");
        if(test.layers==2)for(const auto& eye:stereoEyes)
            require(eye.handle&&eye.images.size()==3&&eye.initialized==std::vector<bool>(3,false),"Stereo images were not fully initialized");
        else require(swapchain&&images.size()==3,"Flat images were not fully initialized");
        handsOwned=true;
        createSwapchain(source,test.layers,test.upscale&&test.layers==2);
        require(createdSwapchains==count&&!destroyedSwapchains&&handsOwned&&!handsStops,"Matching swapchain cache recreated resources");
        const auto changed=[&](const Source& next,uint32_t layers){
            swapTest.layers=layers;
            expectedSource=next;
            const auto previous=createdSwapchains;
            createSwapchain(next,layers,test.upscale&&layers==2);
            require(destroyedSwapchains==previous&&createdSwapchains==previous+(layers==2?2:1),"Recreation retained old resources");
            require(compositionFormat==xrDisplayFormat(next.format,next.displaySrgb)&&format==next.format
                &&swapchainLayers==layers,"Recreation retained stale metadata");
        };
        Source resized=source;resized.extent={1440,900};changed(resized,test.layers);
        require(handsStops==1&&!handsOwned,"Old hand resources survived recreation");
        Source reformatted=resized;reformatted.format=VK_FORMAT_B8G8R8A8_UNORM;changed(reformatted,test.layers);
        Source linear=reformatted;linear.displaySrgb=false;changed(linear,test.layers);
        changed(linear,test.layers==2?1:2);
    }else {
        unsigned expected{};
        if(test.failure==SwapFailure::Create)expected=test.eye;
        else if(test.failure==SwapFailure::ImageCount||test.failure==SwapFailure::ImageFill)expected=test.eye+1;
        else if(test.failure==SwapFailure::Log)expected=test.layers==2?2:1;
        require(createdSwapchains==expected&&!destroyedSwapchains,"Partial creation lost its owned handles");
    }
    verifyShutdown(input);
    std::cout<<"swapchains layers="<<test.layers<<" failure="<<int(test.failure)<<" eye="<<test.eye
        <<" upscale="<<test.upscale<<" fallback="<<test.fallback<<" mirror="<<test.mirror
        <<" created/destroyed="<<createdSwapchains<<'/'<<destroyedSwapchains<<'\n';
}
}
}

int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    unsigned failures{},scenarios{};
    for(size_t i=0;i<argent::names.size();++i) {
        if(argc==2&&std::strcmp(argv[1],argent::names[i]))continue;
        ++scenarios;
        try { argent::run(static_cast<argent::Scenario>(i)); }
        catch(const std::exception& error) { ++failures;std::cerr<<argent::names[i]<<": "<<error.what()<<'\n'; }
    }
    if(argc==1||!std::strcmp(argv[1],"swapchains")){
        const auto run=[&](const argent::SwapScenario& test){
            ++scenarios;
            try { argent::runSwapchain(test); }
            catch(const std::exception& error){++failures;std::cerr<<"swapchain layers="<<test.layers<<" failure="<<int(test.failure)<<" eye="<<test.eye<<": "<<error.what()<<'\n';}
        };
        for(const auto layers:{1u,2u,3u})for(const auto failure:{argent::SwapFailure::None,argent::SwapFailure::FormatCount,
            argent::SwapFailure::FormatFill,argent::SwapFailure::UnsupportedFormat,argent::SwapFailure::Create,
            argent::SwapFailure::ImageCount,argent::SwapFailure::ImageFill,argent::SwapFailure::Log})run({layers,failure});
        for(const auto failure:{argent::SwapFailure::Create,argent::SwapFailure::ImageCount,argent::SwapFailure::ImageFill})run({2,failure,1});
        run({2,argent::SwapFailure::None,0,true});
        run({2,argent::SwapFailure::None,0,true,true});
        run({2,argent::SwapFailure::None,0,false,false,true});
        run({2,argent::SwapFailure::None,0,true,false,true});
        run({2,argent::SwapFailure::ImageFill,1,true});
    }
    std::cout<<scenarios<<" scenarios, "<<failures<<" failures\n";
    return !scenarios||failures?1:0;
}
