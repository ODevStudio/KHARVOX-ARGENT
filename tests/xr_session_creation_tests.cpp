#include "../src/QuadRuntime.h"
#include "../src/openxr/OpenXRRuntimePolicy.h"
#include "../src/vulkan/GpuRetirement.h"
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
DWORD fakeEnvironment(LPCSTR,LPSTR,DWORD) { return 0; }
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
    if((!capacity&&scenario==Scenario::FormatCountFailure)||(capacity&&scenario==Scenario::FormatFillFailure))
        return XR_ERROR_RUNTIME_FAILURE;
    *count=1;if(out)*out=VK_FORMAT_R8G8B8A8_SRGB;return XR_SUCCESS;
}
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
void destroySwapchain() { require(!gpuLive,"Swapchain destroyed before retirement"); }
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
        ||(scenario==Scenario::FinalLogFailure&&message=="XR_SESSION created: Vulkan composition ready"))throw std::bad_alloc{};
}
namespace {
#define XR(name) fake_##name
#define VK(name) fake_##name
#define GetModuleHandleW fakeModule
#define GetProcAddress fakeExport
#define GetEnvironmentVariableA fakeEnvironment
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
void run(Scenario mode) {
    scenario=mode;enable2=mode!=Scenario::Enable1;simulator=mode==Scenario::SimulatorMissingModule;
    running=failed=gpuLive=pauseOwned=false;
    createdSessions=createdSpaces=createdPools=createdCommands=createdFences=propertiesCalls=selections=0;
    destroyedSessions=destroyedSpaces=destroyedPools=destroyedFences=waits=pauseStops=actionStops=0;
    session=XR_NULL_HANDLE;space=localSpace=XR_NULL_HANDLE;boundDevice=VK_NULL_HANDLE;boundQueue=VK_NULL_HANDLE;
    pool=VK_NULL_HANDLE;fence=VK_NULL_HANDLE;command=VK_NULL_HANDLE;controllerActions={};copyTiming={};diagnosticCopyTiming={};
    runtimeSelectedPhysical=mode==Scenario::CachedDevice?handle<VkPhysicalDevice>(2):VK_NULL_HANDLE;
    instance=handle<XrInstance>(1);
    Device input;input.instance=handle<VkInstance>(3);input.physical=handle<VkPhysicalDevice>(4);
    input.device=handle<VkDevice>(5);input.graphicsQueue=handle<VkQueue>(6);input.graphicsFamily=1;
    input.gipa=mode==Scenario::MissingDownstream?nullptr:fakeNext;input.gdpa=fakeDeviceProc;input.setLoaderData=fakeLoaderData;
    runtimeDevice=input.device;runtimeGdpa=input.gdpa;
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
    const auto expectedPauseStops=pauseOwned?1u:0u;
    gpuLive=bound;
    shutdownXRImpl(input.device);
    require(waits==unsigned(bound)&&actionStops==unsigned(bound)&&destroyedSessions==createdSessions
        &&destroyedSpaces==createdSpaces&&destroyedPools==createdPools&&destroyedFences==createdFences
        &&pauseStops==expectedPauseStops,"Partial construction bypassed retirement or resource cleanup");
    require(!gpuLive&&!pauseOwned&&!session&&!space&&!localSpace&&!boundDevice&&!pool&&!fence
        &&!copyTiming.pool&&!diagnosticCopyTiming.pool&&!controllerActions.ready,"Shutdown retained partial resources");
    shutdownXRImpl(input.device);
    require(waits==unsigned(bound)&&destroyedSessions==createdSessions&&destroyedPools==createdPools,
        "Repeated shutdown destroyed a partially constructed resource twice");
    std::cout<<names[size_t(mode)]<<" rejected="<<rejected<<" session="<<createdSessions<<" spaces="<<createdSpaces
        <<" pools="<<createdPools<<" fences="<<createdFences<<" retirement="<<waits<<std::endl;
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
    std::cout<<scenarios<<" scenarios, "<<failures<<" failures\n";
    return !scenarios||failures?1:0;
}
