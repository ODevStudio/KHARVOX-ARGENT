#include "PerformanceDiagnostics.h"
#include "openxr/DiagnosticGpuTiming.h"
#include "hud/EternalWeaponWheel.h"
#include "hud/TutorialBindingHook.h"
#include "hands/HandSceneDepthTracker.h"
#include "fsr/Fsr1Upscaler.h"
#include "openxr/PhysicalGloryKill.h"
#include "openxr/HandsJumpPolicy.h"
#include "openxr/EternalMotionWheel.h"
#include "openxr/CrucibleGesture.h"
#include "QuadRuntime.h"
#include "EternalCameraHook.h"
#include "EternalPlayerHooks.h"
#include "EternalPresentation.h"
#include "XrWorker.h"
#include "QueueBridge.h"
#include "openxr/CopyGpuTiming.h"
#include "openxr/StereoProjection.h"
#include "openxr/StereoCopyBarrier.h"
#include "openxr/GameImageLifetime.h"
#include "openxr/CinewindowPosePolicy.h"
#include "openxr/CinematicProjection.h"
#include "FrameTiming.h"
#include "openxr/OpenXRRuntimePolicy.h"
#include "openxr/RuntimeVulkanDispatch.h"
#include "openxr/RuntimeDeviceCreateChain.h"
#include "openxr/NativeXrReleasePolicy.h"
#include "vulkan/GpuRetirement.h"
#include <array>
#include <atomic>
#include "hands/HandRenderer.h"
#include "hands/HandDispatch.h"
#include "hands/WeaponPoseCalibration.h"
#include "openxr/DisplayFormat.h"
#include "openxr/ControllerInput.h"
#include "openxr/GameplayMapping.h"
#include "openxr/WeaponConfig.h"
#include "openxr/HandSmoothing.h"
#include "openxr/ShoulderChainsaw.h"
#include "openxr/HapticBridge.h"
#include "openxr/GripThresholdPolicy.h"
#include "openxr/ControllerProfile.h"
#include "bhaptics/BhapticsIpcClient.h"
#include "psvr2/Psvr2IpcClient.h"
#include <unordered_map>
#include <algorithm>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <wincodec.h>
#include <wrl/client.h>

namespace argent {
std::filesystem::path runtimePath(){
    HMODULE module{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&runtimePath),&module);
    wchar_t path[32768]{};GetModuleFileNameW(module,path,32768);return std::filesystem::path(path).parent_path();
}
void log(const std::string& message){
    static std::mutex mutex;std::lock_guard<std::mutex> guard(mutex);
    static const auto file=[] {wchar_t dest[32768]{};const auto n=GetEnvironmentVariableW(L"ARGENT_LOG",dest,32768);
        return n&&n<32768?std::filesystem::path(dest):runtimePath()/L"logs/ArgentQuad.log";}();
    static std::ofstream out;
    if(!out.is_open()||!out){out.close();out.clear();std::error_code ignored;std::filesystem::create_directories(file.parent_path(),ignored);out.open(file,std::ios::app);}
    // Keep the handle open, but retain per-line flushing for crash diagnostics.
    out<<GetTickCount64()<<" pid="<<GetCurrentProcessId()<<" "<<message<<std::endl;
}
namespace {
std::recursive_mutex mutex;
HMODULE loader{};PFN_xrGetInstanceProcAddr get{};
XrInstance instance{};XrSystemId system{};XrSession session{};XrSpace space{},localSpace{};XrSwapchain swapchain{};
bool attempted{},running{},failed{},simulator{};VkDevice boundDevice{};Device device;
bool enable2{};
VkPhysicalDevice runtimeSelectedPhysical{};
kharvox::OpenXRRuntimeKind runtimeKind{};
std::atomic<PFN_vkGetInstanceProcAddr> runtimeNext{};
std::atomic<PFN_vkGetDeviceProcAddr> runtimeGdpa{};
std::atomic<VkDevice> runtimeDevice{};
std::atomic<PFN_vkCreateDevice> runtimeCreate{};
std::atomic<VkPhysicalDevice> runtimePhysical{};
std::atomic<const VkDeviceCreateInfo*> runtimeDownstream{};
std::atomic<bool> runtimeSessionRoute{};
PFN_vkGetInstanceProcAddr publicGipa(){
    auto module=GetModuleHandleW(L"vulkan-1.dll");
    return module?reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(module,"vkGetInstanceProcAddr")):nullptr;
}
VKAPI_ATTR VkResult VKAPI_CALL runtimeCreateAdapter(VkPhysicalDevice,const VkDeviceCreateInfo* ci,const VkAllocationCallbacks* a,VkDevice* out){
    const auto create=runtimeCreate.load();const auto physical=runtimePhysical.load();const auto downstream=runtimeDownstream.load();
    if(!create||!physical||!ci||!downstream)return VK_ERROR_INITIALIZATION_FAILED;
    VkDeviceCreateInfo merged=*ci;
    merged.pNext=downstream->pNext;
    log("[RUNTIME-ENABLE2] vkCreateDevice adapter reattached Vulkan loader chain");
    return create(physical,&merged,a,out);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL runtimeGipa(VkInstance i,const char* name){
    const auto resolved=kharvox::resolveRuntimeVulkanProc(i,name,runtimeSessionRoute.load(),simulator,
        runtimeNext.load(),publicGipa(),runtimeCreate.load()?reinterpret_cast<PFN_vkVoidFunction>(runtimeCreateAdapter):nullptr,
        runtimeDevice.load(),runtimeGdpa.load());
    static std::atomic<uint32_t> traceCount{};
    const auto trace=traceCount.fetch_add(1,std::memory_order_relaxed);
    if(trace<128){
        const char* route="downstream";
        switch(resolved.route){
        case kharvox::RuntimeDispatchRoute::CreateDevice: route="create-device"; break;
        case kharvox::RuntimeDispatchRoute::SessionLoader: route="session-loader-gipa"; break;
        case kharvox::RuntimeDispatchRoute::PhysicalLoader: route="physical-loader"; break;
        case kharvox::RuntimeDispatchRoute::RuntimeDevice: route="runtime-device-downstream"; break;
        default: break;
        }
        log(std::string("[RUNTIME-ENABLE2] GIPA thread=")+std::to_string(GetCurrentThreadId())+
            " name="+(name?name:"NULL")+" adapter="+route+
            " returned="+std::to_string(reinterpret_cast<uintptr_t>(resolved.function)));
    }
    return resolved.function;
}
VkQueue boundQueue{};uint32_t boundFamily{};VkCommandPool pool{};VkCommandBuffer command{};VkFence fence{};
VkExtent2D extent{};VkFormat format{};std::vector<XrSwapchainImageVulkanKHR> images;
struct EyeSwapchain {XrSwapchain handle{};std::vector<XrSwapchainImageVulkanKHR> images;std::vector<bool> initialized;};
std::array<EyeSwapchain,2> stereoEyes;
struct PauseBindingsImage {XrSwapchain handle{};uint32_t width{},height{};std::vector<XrSwapchainImageVulkanKHR> images;};
PauseBindingsImage pauseBindings;
bool pauseBindingsReady{};
uint64_t frames{};XrSessionState sessionState=XR_SESSION_STATE_UNKNOWN;
uint32_t swapchainLayers{};
VkFormat compositionFormat{};
uint64_t steamVrNativePresentDeferrals{};
bool steamFramePrepared{},steamFrameBegun{};
XrFrameState steamPreparedFrame{XR_TYPE_FRAME_STATE};
uint64_t steamPreparedFrames{},steamConsumedPreparedFrames{},steamRepeatedAcquires{},steamWaitCalls{},steamBeginCalls{},steamEndCalls{},steamOrderViolations{};
struct StereoPending {bool begun{};sfs::FramePose pose{};input::Snapshot hands{};} stereoPending;
uint64_t stereoSerial{};
XrTime referenceChangeTime{};
QueueBridge bridge;
kharvox::CopyGpuTiming copyTiming;
argent::perf::GpuTiming<4> diagnosticCopyTiming;
uint32_t copyTimingStride=16;

template<class T> T xr(const char* name){static std::unordered_map<std::string,PFN_xrVoidFunction> cache;auto found=cache.find(name);if(found!=cache.end())return reinterpret_cast<T>(found->second);PFN_xrVoidFunction fn{};auto r=get(instance,name,&fn);if(XR_FAILED(r)||!fn)throw std::runtime_error(std::string("XR proc missing: ")+name);cache[name]=fn;return reinterpret_cast<T>(fn);}
#define XR(name) xr<PFN_##name>(#name)
#define VK(name) device.proc<PFN_##name>(#name)
void check(XrResult r,const char* operation){if(XR_FAILED(r))throw std::runtime_error(std::string(operation)+" result="+std::to_string(r));}
void checkVk(VkResult r,const char* operation){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(operation)+" result="+std::to_string(r));}
#include "XrActions.inc"
#include "FsrRuntime.inc"
#include "HandsRuntime.inc"
#include "PauseBindings.inc"
void destroySwapchain(){releaseHands();fsr1.releaseAfterCompletion();if(swapchain){XR(xrDestroySwapchain)(swapchain);swapchain=XR_NULL_HANDLE;}images.clear();for(auto& eye:stereoEyes){if(eye.handle){XR(xrDestroySwapchain)(eye.handle);eye.handle=XR_NULL_HANDLE;}eye.images.clear();eye.initialized.clear();}}
void createSwapchain(const Source& source,uint32_t layers=1,bool immersive=false){
    const bool upscale=immersive&&fsrRequested();
    const auto targetFormat=xrDisplayFormat(source.format,source.displaySrgb);
    if((layers==2?stereoEyes[0].handle:swapchain)&&sourceExtent.width==source.extent.width&&sourceExtent.height==source.extent.height&&swapchainFsrRequested==upscale&&format==source.format&&compositionFormat==targetFormat&&swapchainLayers==layers)return;
    destroySwapchain();
    const auto output=upscale?fsrOutput(source,targetFormat):source.extent;
    sourceExtent=source.extent;swapchainFsrRequested=upscale;
    if(upscale&&!fsr1.active())log("FSR1 using native eye copy");
    uint32_t count=0;check(XR(xrEnumerateSwapchainFormats)(session,0,&count,nullptr),"xrEnumerateSwapchainFormats");
    std::vector<int64_t> formats(count);check(XR(xrEnumerateSwapchainFormats)(session,count,&count,formats.data()),"xrEnumerateSwapchainFormats");
    if(std::find(formats.begin(),formats.end(),targetFormat)==formats.end())throw std::runtime_error("XR swapchain does not support required display format "+std::to_string(targetFormat));
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};ci.usageFlags=XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    char mirrorEnabled[8]{};
    if(layers==2&&GetEnvironmentVariableA("ARGENT_DESKTOP_MIRROR",mirrorEnabled,sizeof(mirrorEnabled))==1&&mirrorEnabled[0]=='1')ci.usageFlags|=XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT;
    ci.format=targetFormat;ci.sampleCount=1;ci.width=output.width;ci.height=output.height;ci.faceCount=1;ci.arraySize=layers==2?1:layers;ci.mipCount=1;
    if(layers==2){
        for(unsigned eye=0;eye<2;++eye){
            check(XR(xrCreateSwapchain)(session,&ci,&stereoEyes[eye].handle),"xrCreateSwapchain eye");
            check(XR(xrEnumerateSwapchainImages)(stereoEyes[eye].handle,0,&count,nullptr),"xrEnumerateSwapchainImages eye");
            stereoEyes[eye].images.assign(count,{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
            check(XR(xrEnumerateSwapchainImages)(stereoEyes[eye].handle,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(stereoEyes[eye].images.data())),"xrEnumerateSwapchainImages eye");
            stereoEyes[eye].initialized.assign(count,false);
        }
        extent=output;format=source.format;compositionFormat=targetFormat;swapchainLayers=layers;log("XR_EYE_SWAPCHAINS images="+std::to_string(stereoEyes[0].images.size())+"/"+std::to_string(stereoEyes[1].images.size())+" extent="+std::to_string(extent.width)+"x"+std::to_string(extent.height)+" sourceFormat="+std::to_string(format)+" compositionFormat="+std::to_string(compositionFormat));
        return;
    }
    check(XR(xrCreateSwapchain)(session,&ci,&swapchain),"xrCreateSwapchain");
    check(XR(xrEnumerateSwapchainImages)(swapchain,0,&count,nullptr),"xrEnumerateSwapchainImages");
    images.assign(count,{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
    check(XR(xrEnumerateSwapchainImages)(swapchain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),"xrEnumerateSwapchainImages");
    extent=output;format=source.format;compositionFormat=targetFormat;swapchainLayers=layers;log("XR_SWAPCHAIN images="+std::to_string(count)+" extent="+std::to_string(extent.width)+"x"+std::to_string(extent.height)+" layers="+std::to_string(layers)+" sourceFormat="+std::to_string(format)+" compositionFormat="+std::to_string(compositionFormat));
}
#include "XrSessionCreation.inc"
#include "XrSessionEvents.inc"
}
bool initializeXRImpl(){
    std::lock_guard<std::recursive_mutex> guard(mutex);if(attempted)return instance&&!failed;attempted=true;
    try{
        loader=LoadLibraryExW((runtimePath()/L"openxr_loader.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!loader)throw std::runtime_error("OpenXR loader unavailable");get=reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(loader,"xrGetInstanceProcAddr"));if(!get)throw std::runtime_error("xrGetInstanceProcAddr unavailable");
        uint32_t count=0;check(XR(xrEnumerateInstanceExtensionProperties)(nullptr,0,&count,nullptr),"XR extensions");
        std::vector<XrExtensionProperties> props(count,{XR_TYPE_EXTENSION_PROPERTIES});check(XR(xrEnumerateInstanceExtensionProperties)(nullptr,count,&count,props.data()),"XR extensions");
        bool vulkan=false,vulkan2=false;for(auto& p:props){if(!strcmp(p.extensionName,XR_KHR_VULKAN_ENABLE_EXTENSION_NAME))vulkan=true;if(!strcmp(p.extensionName,XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME))vulkan2=true;}
        runtimeKind=kharvox::classifyOpenXRRuntime(kharvox::activeOpenXRRuntimeManifest());
        auto path=kharvox::selectOpenXRVulkanPath(runtimeKind,false,vulkan,vulkan2);
        char forceSteamEnable1[8]{};
        if(kharvox::isSteamBackedOpenXRRuntime(runtimeKind)
            &&GetEnvironmentVariableA("ARGENT_STEAMVR_FORCE_ENABLE1",forceSteamEnable1,sizeof(forceSteamEnable1))==1
            &&forceSteamEnable1[0]=='1'
            &&vulkan)
            path=kharvox::OpenXRVulkanPath::VulkanEnable1Direct;
        if(path==kharvox::OpenXRVulkanPath::None)throw std::runtime_error("No OpenXR Vulkan graphics extension available");
        enable2=path==kharvox::OpenXRVulkanPath::VulkanEnable2RuntimeManaged;
        const char* extension=enable2?XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME:XR_KHR_VULKAN_ENABLE_EXTENSION_NAME;
        log(std::string("XR_VULKAN_PATH ")+extension);
        XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
        strcpy_s(ci.applicationInfo.applicationName,"DOOM Eternal");
        strcpy_s(ci.applicationInfo.engineName,"idTech7");
        ci.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);ci.enabledExtensionCount=1;ci.enabledExtensionNames=&extension;
        log(std::string("XR_APPLICATION_IDENTITY app=")+ci.applicationInfo.applicationName+" engine="+ci.applicationInfo.engineName);
        check(XR(xrCreateInstance)(&ci,&instance),"xrCreateInstance");
        XrSystemGetInfo si{XR_TYPE_SYSTEM_GET_INFO};si.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;check(XR(xrGetSystem)(instance,&si,&system),"xrGetSystem");
        XrGraphicsRequirementsVulkanKHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};check(enable2?XR(xrGetVulkanGraphicsRequirements2KHR)(instance,system,&req):XR(xrGetVulkanGraphicsRequirementsKHR)(instance,system,&req),"XR graphics requirements");
        XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};check(XR(xrGetInstanceProperties)(instance,&ip),"XR properties");simulator=!strcmp(ip.runtimeName,"OpenXR Simulator Runtime");log(std::string("XR_INITIALIZED runtime=")+ip.runtimeName);return true;
    }catch(const std::exception& e){log(std::string("XR_DISABLED ")+e.what());failed=true;return false;}
}
bool initializeXR(){
    const auto activeRuntime=kharvox::classifyOpenXRRuntime(kharvox::activeOpenXRRuntimeManifest());
    if(sfs::vrEnabled()&&kharvox::isSteamBackedOpenXRRuntime(activeRuntime)){
        return xrWorker().invoke([]{
            log("[STEAM-XR-THREAD] initializing OpenXR on worker thread="+std::to_string(GetCurrentThreadId()));
            return initializeXRImpl();
        });
    }
    return initializeXRImpl();
}
bool xrCreateGameInstance(PFN_vkGetInstanceProcAddr next,const VkInstanceCreateInfo* ci,const VkAllocationCallbacks* a,VkInstance* out,VkResult& result){
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(!instance||failed||!enable2)return false;
    result=VK_ERROR_INITIALIZATION_FAILED;
    try{
        XrVulkanInstanceCreateInfoKHR info{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
        info.systemId=system;info.pfnGetInstanceProcAddr=next;info.vulkanCreateInfo=ci;info.vulkanAllocator=a;
        const auto r=XR(xrCreateVulkanInstanceKHR)(instance,&info,out,&result);
        kharvox::finishRuntimeVulkanCreate(XR_SUCCEEDED(r),&result,out);
        log("XR_VULKAN_INSTANCE xr="+std::to_string(r)+" vk="+std::to_string(result));
    }catch(const std::exception& e){log(e.what());*out=VK_NULL_HANDLE;}
    return true;
}
bool xrCreateGameDevice(PFN_vkGetInstanceProcAddr next,PFN_vkGetDeviceProcAddr gdpa,VkInstance vi,VkPhysicalDevice physical,const VkDeviceCreateInfo* runtimeInfo,const VkDeviceCreateInfo* downstreamInfo,const VkAllocationCallbacks* a,VkDevice* out,VkResult& result){
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(!instance||failed||!enable2)return false;
    result=VK_ERROR_INITIALIZATION_FAILED;
    try{
        XrVulkanGraphicsDeviceGetInfoKHR query{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};query.systemId=system;query.vulkanInstance=vi;
        VkPhysicalDevice selected{};check(XR(xrGetVulkanGraphicsDevice2KHR)(instance,&query,&selected),"xrGetVulkanGraphicsDevice2KHR");
        auto selectedGet=simulator?next:publicGipa();
        auto properties=[&](PFN_vkGetInstanceProcAddr get,VkPhysicalDevice gpu,VkPhysicalDeviceIDProperties& id){
            auto fn=get?reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(get(vi,"vkGetPhysicalDeviceProperties2")):nullptr;
            if(!fn&&get)fn=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(get(vi,"vkGetPhysicalDeviceProperties2KHR"));
            if(!fn)throw std::runtime_error("XR GPU UUID query unavailable");
            VkPhysicalDeviceProperties2 p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};p.pNext=&id;fn(gpu,&p);
        };
        VkPhysicalDeviceIDProperties gameId{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES},xrId{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        properties(next,physical,gameId);properties(selectedGet,selected,xrId);
        if(memcmp(gameId.deviceUUID,xrId.deviceUUID,VK_UUID_SIZE))throw std::runtime_error("XR GPU UUID does not match game GPU");
        runtimeNext=next;runtimePhysical=physical;runtimeDownstream=downstreamInfo;
        runtimeSessionRoute=false;runtimeCreate=kharvox::resolveLayerCreateDevice(next,vi);
        XrVulkanDeviceCreateInfoKHR info{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
        info.systemId=system;info.pfnGetInstanceProcAddr=runtimeGipa;info.vulkanPhysicalDevice=selected;info.vulkanCreateInfo=runtimeInfo;info.vulkanAllocator=a;
        log("xrCreateVulkanDeviceKHR cross-thread layer adapter armed");
        XrResult r{};
        {
            RestoreDeviceCreateLinks restoreLinks(downstreamInfo->pNext);
            r=XR(xrCreateVulkanDeviceKHR)(instance,&info,out,&result);
        }
        kharvox::finishRuntimeVulkanCreate(XR_SUCCEEDED(r),&result,out);
        if(result==VK_SUCCESS){runtimeSelectedPhysical=selected;runtimeSessionRoute=kharvox::isSteamBackedOpenXRRuntime(runtimeKind);}
        log("XR_VULKAN_DEVICE xr="+std::to_string(r)+" vk="+std::to_string(result));
    }catch(const std::exception& e){log(e.what());*out=VK_NULL_HANDLE;}
    runtimeCreate=nullptr;runtimeDownstream=nullptr;runtimePhysical=VK_NULL_HANDLE;
    return true;
}
void xrBindGameDevice(VkDevice gameDevice,PFN_vkGetDeviceProcAddr gdpa){
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(!enable2||failed||!gameDevice||!gdpa)return;
    runtimeGdpa=gdpa;
    runtimeDevice=gameDevice;
    log("[RUNTIME-ENABLE2] retained callback bound after ARGENT device initialization");
}
std::vector<std::string> xrExtensions(bool isDevice){
    std::lock_guard<std::recursive_mutex> guard(mutex);std::vector<std::string> result;if(!instance||failed||enable2)return result;
    try{
        auto query=isDevice?reinterpret_cast<PFN_xrGetVulkanInstanceExtensionsKHR>(XR(xrGetVulkanDeviceExtensionsKHR)):XR(xrGetVulkanInstanceExtensionsKHR);
        uint32_t size=0;check(query(instance,system,0,&size,nullptr),"XR Vulkan extensions");std::vector<char> text(size?size:1);
        check(query(instance,system,uint32_t(text.size()),&size,text.data()),"XR Vulkan extensions");
        std::istringstream stream(text.data());std::string name;while(stream>>name){result.push_back(name);log("XR_REQUIRED_EXTENSION "+name);}
    }catch(const std::exception& e){log(e.what());failed=true;}
    return result;
}
#include "StereoXr.inc"
#include "PreparedFrame.inc"
#include "FlatFramePresent.inc"
bool presentQuad(Device& d,VkQueue q,uint32_t family,uint32_t index,const Source& source,uint32_t imageIndex,const VkPresentInfoKHR& present){
    auto frame=[&]{return presentQuadImpl(d,q,family,index,source,imageIndex,present);};
    return simulator?xrWorker().invoke(frame):frame();
}
#include "XrShutdown.inc"
void shutdownXR(VkDevice d){if(simulator)xrWorker().invoke([&]{shutdownXRImpl(d);});else shutdownXRImpl(d);
    if(runtimeDevice.load()==d){runtimeDevice=VK_NULL_HANDLE;runtimeGdpa=nullptr;}}
}

