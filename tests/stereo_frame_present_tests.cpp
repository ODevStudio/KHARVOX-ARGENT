#include "../src/QuadRuntime.h"
#include "../src/XrWorker.h"
#include "../src/openxr/GameImageLifetime.h"
#include "../src/openxr/NativeXrReleasePolicy.h"
#include "../src/openxr/StereoCopyBarrier.h"
#include "../src/openxr/StereoProjection.h"
#include "../src/vulkan/GpuRetirement.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <new>
#include <stdexcept>

namespace argent {
namespace perf {
std::atomic<int> mode{};
bool enabled() { return false; }
bool sample(uint64_t,int) { return false; }
}
namespace {
enum class Scenario { Normal, InvalidPair, FirstAcquireFailure, SecondAcquireFailure,
    FirstWaitFailure, SecondWaitFailure, ResetFailure, FsrReject, RecordHandsFailure,
    SubmitFailure, FenceFailure, DeviceLost, ReleaseFailure, EndFailure, MirrorFailure, SuccessLogFailure };
Scenario scenario{};
bool failureLog{}, failed{}, nativeOpen{}, gpuLive{}, earlyAllowed{}, steamFrameBegun{}, pauseBindingsReady{};
std::array<bool,2> imageOwned{}, imageWaited{};
std::array<unsigned,2> releases{};
unsigned submits{}, queueWaits{}, ends{}, discarded{}, handFinished{};
DWORD callerThread{};
XrTime endedTime{};
constexpr XrTime displayTime=123456;
std::recursive_mutex mutex;
Device device;
VkDevice boundDevice{};
VkQueue boundQueue{};
VkCommandBuffer command{};
VkFence fence{};
XrSession session{};
XrSpace space{}, localSpace{};
VkExtent2D extent{1280,1280}, sourceExtent=extent;
VkFormat format=VK_FORMAT_R8G8B8A8_UNORM;
uint32_t swapchainLayers=2, copyTimingStride=16;
uint64_t steamWaitCalls{}, steamBeginCalls{}, steamEndCalls{};
kharvox::OpenXRRuntimeKind runtimeKind{};
struct Pending { bool begun{};sfs::FramePose pose{}; } stereoPending;
struct Eye { XrSwapchain handle{};std::vector<XrSwapchainImageVulkanKHR> images;std::vector<bool> initialized; };
std::array<Eye,2> stereoEyes;
struct FrameTiming { struct Totals {};FrameTiming(const char*,Totals&) {} };
struct Timing {
    bool recorded{};
    void begin(VkCommandBuffer,bool) {}
    void end(VkCommandBuffer) {}
    void point(VkCommandBuffer,unsigned) {}
    bool completed(double&) { return false; }
    bool read(std::array<uint64_t,4>&) { return false; }
    double ms(uint64_t,uint64_t) { return 0; }
} copyTiming, diagnosticCopyTiming;
struct Fsr {
    struct Rect { int32_t x,y;uint32_t width,height; };
    bool active() { return scenario==Scenario::FsrReject; }
    VkImage record(VkCommandBuffer,VkImage,unsigned,uint64_t,Rect,VkExtent2D,unsigned) { return VK_NULL_HANDLE; }
    void discardRecordedFrame() { ++discarded; }
} fsr1;
void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
template<class T> T handle(std::uintptr_t value) { return reinterpret_cast<T>(value); }
struct Hands {
    void finishSceneIntegratedFrame() {
        require(!gpuLive,"Hand resources retired before GPU completion");
        ++handFinished;
    }
} handRenderer;
bool earlyStereoReleaseRequested(uint64_t) { return true; }
bool extendedLogging() { return false; }
namespace camera { bool renderedHead(uint64_t,XrPosef&) { return false; } }
namespace presentation { bool pauseRootVisible() { return false; } }
XrCompositionLayerQuad pauseBindingsLayer(const XrCompositionLayerQuad& quad) { return quad; }
void prepareHands(Device&,bool) {}
void recordHands(const std::array<uint32_t,2>&,const sfs::StereoFrame&,
    const std::array<XrCompositionLayerProjectionView,2>&,kharvox::GameImageLifetime::Use& use) {
    require(use.select([]{return true;},[]{return std::array{kharvox::GameImageLifetime::key(handle<VkImage>(20))};}),
        "Could not borrow game depth");
    if(scenario==Scenario::RecordHandsFailure)throw std::runtime_error("hand recording failure");
}
void check(XrResult result,const char* operation) {
    if(XR_FAILED(result))throw std::runtime_error(operation);
}
void checkVk(VkResult result,const char* operation) {
    if(result!=VK_SUCCESS)throw std::runtime_error(operation);
}
unsigned eyeIndex(XrSwapchain target) { return target==stereoEyes[0].handle?0:1; }
XrResult fake_xrAcquireSwapchainImage(XrSwapchain target,const XrSwapchainImageAcquireInfo*,uint32_t* index) {
    const auto eye=eyeIndex(target);
    if((scenario==Scenario::FirstAcquireFailure&&eye==0)||(scenario==Scenario::SecondAcquireFailure&&eye==1))
        return XR_ERROR_RUNTIME_FAILURE;
    imageOwned[eye]=true;*index=0;return XR_SUCCESS;
}
XrResult fake_xrWaitSwapchainImage(XrSwapchain target,const XrSwapchainImageWaitInfo*) {
    const auto eye=eyeIndex(target);
    if((scenario==Scenario::FirstWaitFailure&&eye==0)||(scenario==Scenario::SecondWaitFailure&&eye==1))
        return XR_ERROR_RUNTIME_FAILURE;
    imageWaited[eye]=true;return XR_SUCCESS;
}
XrResult fake_xrReleaseSwapchainImage(XrSwapchain target,const XrSwapchainImageReleaseInfo*) {
    const auto eye=eyeIndex(target);++releases[eye];
    require(imageOwned[eye]&&imageWaited[eye]&&(!gpuLive||earlyAllowed),"Eye released without eligible ownership/completion");
    if(scenario==Scenario::ReleaseFailure&&eye==1&&releases[eye]==1)return XR_ERROR_RUNTIME_FAILURE;
    imageOwned[eye]=imageWaited[eye]=false;return XR_SUCCESS;
}
XrResult fake_xrEndFrame(XrSession,const XrFrameEndInfo* end) {
    ++ends;endedTime=end->displayTime;
    require(nativeOpen&&endedTime==displayTime,"Invalid frame end ownership/display time");
    if(kharvox::isSteamBackedOpenXRRuntime(runtimeKind))require(GetCurrentThreadId()!=callerThread,"SteamVR end bypassed worker");
    nativeOpen=false;return scenario==Scenario::EndFailure?XR_ERROR_RUNTIME_FAILURE:XR_SUCCESS;
}
void cancelStereoImpl() {
    if(!stereoPending.begun)return;
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};end.displayTime=stereoPending.pose.displayTime;
    const bool steam=kharvox::isSteamBackedOpenXRRuntime(runtimeKind);
    const auto call=[&]{std::lock_guard<std::recursive_mutex> lock(*device.queueMutex);return fake_xrEndFrame(session,&end);};
    const auto result=steam?xrWorker().invoke(call):call();
    stereoPending.begun=false;
    if(steam){steamFrameBegun=false;++steamEndCalls;}
    check(result,"cancel stereo frame");
}
VkResult fake_vkResetCommandBuffer(VkCommandBuffer,VkCommandBufferResetFlags) {
    return scenario==Scenario::ResetFailure?VK_ERROR_OUT_OF_HOST_MEMORY:VK_SUCCESS;
}
VkResult fake_vkBeginCommandBuffer(VkCommandBuffer,const VkCommandBufferBeginInfo*) { return VK_SUCCESS; }
void fake_vkCmdPipelineBarrier(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,
    uint32_t,const VkMemoryBarrier*,uint32_t,const VkBufferMemoryBarrier*,uint32_t,const VkImageMemoryBarrier*) {}
void fake_vkCmdCopyImage(VkCommandBuffer,VkImage,VkImageLayout,VkImage,VkImageLayout,uint32_t,const VkImageCopy*) {}
void fake_vkCmdBlitImage(VkCommandBuffer,VkImage,VkImageLayout,VkImage,VkImageLayout,uint32_t,const VkImageBlit*,VkFilter) {}
VkResult fake_vkEndCommandBuffer(VkCommandBuffer) { return VK_SUCCESS; }
VkResult fake_vkResetFences(VkDevice,uint32_t,const VkFence*) { return VK_SUCCESS; }
VkResult fake_vkQueueSubmit(VkQueue,uint32_t,const VkSubmitInfo*,VkFence) {
    if(scenario==Scenario::SubmitFailure)return VK_ERROR_OUT_OF_HOST_MEMORY;
    ++submits;gpuLive=true;return VK_SUCCESS;
}
VkResult fake_vkWaitForFences(VkDevice,uint32_t,const VkFence*,VkBool32,uint64_t) {
    if(scenario==Scenario::DeviceLost)return VK_ERROR_DEVICE_LOST;
    if(scenario==Scenario::FenceFailure)return VK_ERROR_OUT_OF_HOST_MEMORY;
    gpuLive=false;return VK_SUCCESS;
}
VkResult fake_vkQueueWaitIdle(VkQueue) {
    ++queueWaits;gpuLive=false;
    return scenario==Scenario::DeviceLost?VK_ERROR_DEVICE_LOST:VK_SUCCESS;
}
}
namespace sfs {
bool sourceRingActive(VkDevice) { return true; }
void markXrCheckpoint(VkDevice,VkCommandBuffer,const char*) {}
void reportGpuCheckpoints(VkDevice,VkQueue) noexcept {}
}
void log(const std::string& message) {
    if((failureLog&&message.rfind("STEREO_PRESENT_FAILED ",0)==0)
        ||(scenario==Scenario::SuccessLogFailure&&message.rfind("STEREO_PROJECTION_SUBMITTED ",0)==0))
        throw std::bad_alloc{};
}
namespace {
#define XR(name) fake_##name
#define VK(name) fake_##name
#include "../src/StereoFramePresent.inc"
#undef VK
#undef XR

void run(Scenario mode,kharvox::OpenXRRuntimeKind runtime,bool throwLog,bool quad=false) {
    scenario=mode;runtimeKind=runtime;failureLog=throwLog;
    nativeOpen=stereoPending.begun=true;
    gpuLive=failed=false;imageOwned=imageWaited={};releases={};
    submits=queueWaits=ends=discarded=handFinished=0;endedTime=0;callerThread=GetCurrentThreadId();
    steamFrameBegun=kharvox::isSteamBackedOpenXRRuntime(runtime);
    steamWaitCalls=steamBeginCalls=1;steamEndCalls=0;
    device.device=boundDevice=handle<VkDevice>(1);device.graphicsQueue=boundQueue=handle<VkQueue>(2);
    command=handle<VkCommandBuffer>(3);fence=handle<VkFence>(4);session=handle<XrSession>(5);
    for(unsigned eye=0;eye<2;++eye) {
        stereoEyes[eye].handle=handle<XrSwapchain>(6+eye);
        stereoEyes[eye].images={{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR,nullptr,handle<VkImage>(8+eye)}};
        stereoEyes[eye].initialized={false};
    }
    sfs::StereoFrame pair{};pair.generation=pair.pose.serial=1;pair.pose.displayTime=displayTime;
    pair.pose.quadView=quad;
    for(unsigned eye=0;eye<2;++eye) {
        auto& view=pair.pose.views[eye];view.type=XR_TYPE_VIEW;view.pose.orientation.w=1;
        view.fov={-.7f,.7f,.7f,-.7f};
        pair.eyes[eye]={handle<VkImage>(10+eye),sourceExtent,format,VK_IMAGE_LAYOUT_GENERAL,view.pose,view.fov,0,1};
    }
    stereoPending.pose=pair.pose;
    if(mode==Scenario::InvalidPair)pair.generation=2;
    StereoMirror mirror;
    if(mode==Scenario::MirrorFailure)mirror=[](VkImage,VkExtent2D,VkImageLayout){throw std::runtime_error("mirror failure");};
    earlyAllowed=runtime==kharvox::OpenXRRuntimeKind::VirtualDesktop&&!mirror;
    VkSemaphore wait=handle<VkSemaphore>(12);
    StereoPresentResult result{};bool escaped{};
    try { result=presentStereoImpl(device,pair,1,&wait,mirror); }
    catch(...) { escaped=true; }
    require(!escaped,"Diagnostic exception bypassed stereo recovery");
    require(!nativeOpen&&!stereoPending.begun&&!steamFrameBegun&&ends==1,"Frame was not ended exactly once");
    require(!gpuLive,"Live GPU work survived recovery");
    require(result.waitsConsumed==(submits!=0),"Consumed binary waits were not reported");
    require(result.sourceComplete==(submits!=0&&mode!=Scenario::DeviceLost),"Incorrect source completion result");
    require(failed==(mode!=Scenario::Normal&&mode!=Scenario::InvalidPair),"Incorrect failed-frame state");
    for(unsigned eye=0;eye<2;++eye) {
        const bool unwaited=(mode==Scenario::FirstWaitFailure&&eye==0)||(mode==Scenario::SecondWaitFailure&&eye==1);
        const bool lost=mode==Scenario::DeviceLost&&!earlyAllowed;
        require(!imageOwned[eye]||unwaited||lost,"Waited eye leaked during recovery");
    }
    const bool recoveryWait=mode==Scenario::FenceFailure||mode==Scenario::DeviceLost
        ||(earlyAllowed&&(mode==Scenario::ReleaseFailure||mode==Scenario::EndFailure||mode==Scenario::SuccessLogFailure));
    require(queueWaits==(recoveryWait?1u:0u),"Unexpected queue-idle recovery work");
    require(discarded==(submits==0&&mode!=Scenario::InvalidPair?1u:0u),"Unsubmitted FSR state was not discarded");
    kharvox::gameImageLifetime().retire(kharvox::GameImageLifetime::key(handle<VkImage>(20)),[]{
        require(!gpuLive,"Borrowed depth retired before completion/device loss");
    });
    std::cout<<"runtime="<<int(runtime)<<" scenario="<<int(mode)<<" failureLog="<<throwLog<<" quad="<<quad
        <<" released="<<releases[0]<<'/'<<releases[1]<<" waitsConsumed="<<result.waitsConsumed
        <<" sourceComplete="<<result.sourceComplete<<'\n';
}
}
}

int main() {
    unsigned failures{},scenarios{};
    for(const auto runtime:{kharvox::OpenXRRuntimeKind::Unknown,kharvox::OpenXRRuntimeKind::SteamVR,kharvox::OpenXRRuntimeKind::VirtualDesktop}) {
        for(const auto mode:{argent::Scenario::Normal,argent::Scenario::InvalidPair,argent::Scenario::FirstAcquireFailure,
            argent::Scenario::SecondAcquireFailure,argent::Scenario::FirstWaitFailure,argent::Scenario::SecondWaitFailure,
            argent::Scenario::ResetFailure,argent::Scenario::FsrReject,argent::Scenario::RecordHandsFailure,
            argent::Scenario::SubmitFailure,argent::Scenario::FenceFailure,argent::Scenario::DeviceLost,
            argent::Scenario::ReleaseFailure,argent::Scenario::EndFailure,argent::Scenario::MirrorFailure,argent::Scenario::SuccessLogFailure}) {
            for(const bool throwLog:{false,true}) {
                ++scenarios;
                try { argent::run(mode,runtime,throwLog); }
                catch(const std::exception& error) {
                    ++failures;std::cerr<<"runtime="<<int(runtime)<<" scenario="<<int(mode)<<" failureLog="<<throwLog<<": "<<error.what()<<'\n';
                }
            }
        }
        ++scenarios;
        try { argent::run(argent::Scenario::Normal,runtime,false,true); }
        catch(const std::exception& error) { ++failures;std::cerr<<error.what()<<'\n'; }
    }
    std::cout<<scenarios<<" scenarios, "<<failures<<" failures\n";
    return failures?1:0;
}
