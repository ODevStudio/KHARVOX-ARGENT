#include "../src/QuadRuntime.h"
#include "../src/XrWorker.h"
#include "../src/openxr/OpenXRRuntimePolicy.h"
#include "../src/openxr/StereoProjection.h"
#include "../src/vulkan/GpuRetirement.h"
#include <array>
#include <iostream>
#include <new>
#include <stdexcept>

namespace argent {
namespace {
enum class Scenario { Normal, NoRender, SwapchainFailure, WaitFailure, BeginFailure,
    AcquireFailure, ImageWaitFailure, ResetFailure, SubmitFailure, FenceFailure,
    ReleaseFailure, EndFailure, PreparedLogFailure, PrepareLogFailure, RepeatedAcquireLogFailure, EventFailure };
Scenario scenario{};
bool failureLog{}, nativeOpen{}, gpuLive{}, imageOwned{}, imageWaited{}, failed{}, running{true};
bool steamFramePrepared{}, steamFrameBegun{}, pauseBindingsReady{};
unsigned waits{}, begins{}, ends{}, releases{}, submits{}, deviceWaits{};
DWORD callerThread{}, lifecycleThread{};
XrTime endedTime{};
constexpr XrTime displayTime=123456;
std::recursive_mutex mutex;
Device device;
XrInstance instance{};
XrSession session{};
XrSpace space{};
XrSwapchain swapchain{};
VkDevice boundDevice{};
VkQueue boundQueue{};
uint32_t boundFamily{}, copyTimingStride=16;
VkCommandBuffer command{};
VkFence fence{};
VkExtent2D extent{1280,720};
std::vector<XrSwapchainImageVulkanKHR> images;
XrSessionState sessionState=XR_SESSION_STATE_FOCUSED;
kharvox::OpenXRRuntimeKind runtimeKind{};
XrFrameState steamPreparedFrame{XR_TYPE_FRAME_STATE};
uint64_t steamVrNativePresentDeferrals{}, steamOrderViolations{}, steamWaitCalls{}, steamBeginCalls{};
uint64_t steamEndCalls{}, steamConsumedPreparedFrames{}, steamPreparedFrames{}, steamRepeatedAcquires{}, frames{};
struct Timing {
    void begin(VkCommandBuffer,bool) {}
    void end(VkCommandBuffer) {}
    bool completed(double&) { return false; }
} copyTiming;
struct Bridge {
    VkBuffer buffer{};
    VkSemaphore ready{};
    void prepare(Device&,uint32_t,uint32_t,VkExtent2D,VkFormat) {}
    void stage(Device&,VkQueue,VkImage,VkExtent2D,const VkPresentInfoKHR&,bool&) {
        throw std::runtime_error("Unexpected cross-queue fixture path");
    }
} bridge;
namespace presentation { bool pauseRootVisible() { return false; } }
XrCompositionLayerQuad pauseBindingsLayer(const XrCompositionLayerQuad& quad) { return quad; }
void events() { if(scenario==Scenario::EventFailure)throw std::runtime_error("event failure"); }
void startSession(Device&,VkQueue,uint32_t,uint32_t) { throw std::runtime_error("Unexpected session creation"); }
void createSwapchain(const Source&) {
    if(scenario==Scenario::SwapchainFailure)throw std::runtime_error("swapchain failure");
}
void check(XrResult result,const char* operation) {
    if(XR_FAILED(result))throw std::runtime_error(operation);
}
void checkVk(VkResult result,const char* operation) {
    if(result!=VK_SUCCESS)throw std::runtime_error(operation);
}
void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
template<class T> T handle(std::uintptr_t value) { return reinterpret_cast<T>(value); }
XrResult fake_xrWaitFrame(XrSession,const XrFrameWaitInfo*,XrFrameState* state) {
    ++waits;
    lifecycleThread=GetCurrentThreadId();
    if(kharvox::isSteamBackedOpenXRRuntime(runtimeKind))require(lifecycleThread!=callerThread,"SteamVR wait did not run on the worker");
    state->predictedDisplayTime=displayTime;
    state->shouldRender=scenario!=Scenario::NoRender;
    return scenario==Scenario::WaitFailure?XR_ERROR_RUNTIME_FAILURE:XR_SUCCESS;
}
XrResult fake_xrBeginFrame(XrSession,const XrFrameBeginInfo*) {
    ++begins;
    require(GetCurrentThreadId()==lifecycleThread,"Wait and begin ran on different threads");
    if(scenario==Scenario::BeginFailure)return XR_ERROR_RUNTIME_FAILURE;
    require(!nativeOpen,"A frame was already open");
    nativeOpen=true;
    return XR_SUCCESS;
}
XrResult fake_xrEndFrame(XrSession,const XrFrameEndInfo* end) {
    ++ends;
    require(GetCurrentThreadId()==lifecycleThread,"Frame end changed lifecycle thread");
    require(nativeOpen,"Ending an unopened frame");
    endedTime=end->displayTime;
    if(endedTime!=displayTime)return XR_ERROR_TIME_INVALID;
    nativeOpen=false;
    return scenario==Scenario::EndFailure?XR_ERROR_RUNTIME_FAILURE:XR_SUCCESS;
}
XrResult fake_xrAcquireSwapchainImage(XrSwapchain,const XrSwapchainImageAcquireInfo*,uint32_t* target) {
    if(scenario==Scenario::AcquireFailure)return XR_ERROR_RUNTIME_FAILURE;
    imageOwned=true;
    *target=0;
    return XR_SUCCESS;
}
XrResult fake_xrWaitSwapchainImage(XrSwapchain,const XrSwapchainImageWaitInfo*) {
    if(scenario==Scenario::ImageWaitFailure)return XR_ERROR_RUNTIME_FAILURE;
    imageWaited=true;
    return XR_SUCCESS;
}
XrResult fake_xrReleaseSwapchainImage(XrSwapchain,const XrSwapchainImageReleaseInfo*) {
    ++releases;
    require(imageOwned&&imageWaited&&!gpuLive,"Image released before verified completion");
    if(scenario==Scenario::ReleaseFailure&&releases==1)return XR_ERROR_RUNTIME_FAILURE;
    imageOwned=imageWaited=false;
    return XR_SUCCESS;
}
VkResult fake_vkResetCommandBuffer(VkCommandBuffer,VkCommandBufferResetFlags) {
    return scenario==Scenario::ResetFailure?VK_ERROR_OUT_OF_HOST_MEMORY:VK_SUCCESS;
}
VkResult fake_vkBeginCommandBuffer(VkCommandBuffer,const VkCommandBufferBeginInfo*) { return VK_SUCCESS; }
void fake_vkCmdPipelineBarrier(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,
    uint32_t,const VkMemoryBarrier*,uint32_t,const VkBufferMemoryBarrier*,uint32_t,const VkImageMemoryBarrier*) {}
void fake_vkCmdCopyImage(VkCommandBuffer,VkImage,VkImageLayout,VkImage,VkImageLayout,uint32_t,const VkImageCopy*) {}
void fake_vkCmdCopyBufferToImage(VkCommandBuffer,VkBuffer,VkImage,VkImageLayout,uint32_t,const VkBufferImageCopy*) {}
VkResult fake_vkEndCommandBuffer(VkCommandBuffer) { return VK_SUCCESS; }
VkResult fake_vkResetFences(VkDevice,uint32_t,const VkFence*) { return VK_SUCCESS; }
VkResult fake_vkQueueSubmit(VkQueue,uint32_t,const VkSubmitInfo*,VkFence) {
    if(scenario==Scenario::SubmitFailure)return VK_ERROR_OUT_OF_HOST_MEMORY;
    ++submits;
    gpuLive=true;
    return VK_SUCCESS;
}
VkResult fake_vkWaitForFences(VkDevice,uint32_t,const VkFence*,VkBool32,uint64_t) {
    if(scenario==Scenario::FenceFailure)return VK_ERROR_OUT_OF_HOST_MEMORY;
    gpuLive=false;
    return VK_SUCCESS;
}
VkResult fake_vkDeviceWaitIdle(VkDevice) { ++deviceWaits;gpuLive=false;return VK_SUCCESS; }
}
void log(const std::string& message) {
    if((failureLog&&(message.rfind("QUAD_DISABLED ",0)==0||message.rfind("[STEAM-XR-SPLIT] prepare failed: ",0)==0))
        ||(scenario==Scenario::PreparedLogFailure&&message.rfind("[STEAM-XR-SPLIT] consumed=",0)==0)
        ||(scenario==Scenario::PrepareLogFailure&&message.rfind("[STEAM-XR-SPLIT] prepared=",0)==0)
        ||(scenario==Scenario::RepeatedAcquireLogFailure&&message.rfind("[STEAM-XR-SPLIT] acquire arrived ",0)==0)
        ||(failureLog&&scenario==Scenario::WaitFailure&&message.rfind("[STEAM-XR-SPLIT] xrWaitFrame result=",0)==0)
        ||(failureLog&&scenario==Scenario::BeginFailure&&message.rfind("[STEAM-XR-SPLIT] xrBeginFrame result=",0)==0))
        throw std::bad_alloc{};
}
#define XR(name) fake_##name
#include "../src/PreparedFrame.inc"
namespace {
#define VK(name) fake_##name
#include "../src/FlatFramePresent.inc"
#undef VK
#undef XR

void resetFrame(Scenario mode,bool steam,bool throwLog) {
    scenario=mode;
    failureLog=throwLog;
    nativeOpen=gpuLive=imageOwned=imageWaited=failed=steamFramePrepared=steamFrameBegun=false;
    waits=begins=ends=releases=submits=deviceWaits=0;
    frames=steamOrderViolations=steamWaitCalls=steamBeginCalls=steamEndCalls=steamConsumedPreparedFrames=0;
    steamPreparedFrames=steamRepeatedAcquires=0;running=true;
    endedTime=0;
    callerThread=GetCurrentThreadId();lifecycleThread=0;
    runtimeKind=steam?kharvox::OpenXRRuntimeKind::SteamVR:kharvox::OpenXRRuntimeKind::Unknown;
    instance=handle<XrInstance>(1);session=handle<XrSession>(2);space=handle<XrSpace>(3);
    swapchain=handle<XrSwapchain>(4);device.device=boundDevice=handle<VkDevice>(5);
    device.graphicsQueue=boundQueue=handle<VkQueue>(6);command=handle<VkCommandBuffer>(7);
    fence=handle<VkFence>(8);
    images={{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR,nullptr,handle<VkImage>(9)}};
}
void run(Scenario mode,bool steam,bool prepared,bool throwLog) {
    resetFrame(mode,steam,throwLog);
    Source source;
    source.extent=extent;
    source.images={handle<VkImage>(10)};
    if(prepared) {
        XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
        XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
        xrWorker().invoke([&]{
            require(XR_SUCCEEDED(fake_xrWaitFrame(session,&wait,&steamPreparedFrame)),"Preparation wait failed");
            require(XR_SUCCEEDED(fake_xrBeginFrame(session,&begin)),"Preparation begin failed");
        });
        steamFramePrepared=steamFrameBegun=true;
        steamWaitCalls=steamBeginCalls=1;
    }
    VkSemaphore semaphore=handle<VkSemaphore>(11);
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount=1;present.pWaitSemaphores=&semaphore;
    bool result{},escaped{};
    try { result=presentQuadImpl(device,boundQueue,0,0,source,0,present); }
    catch(...) { escaped=true; }
    const bool noBegin=mode==Scenario::WaitFailure||mode==Scenario::BeginFailure
        ||(mode==Scenario::SwapchainFailure&&!prepared);
    const bool success=mode==Scenario::Normal||mode==Scenario::NoRender;
    require(!escaped,"Failure escaped XR image/frame recovery");
    require(!nativeOpen&&!steamFrameBegun&&!steamFramePrepared,"XR frame ownership was abandoned");
    require(!gpuLive,"Submitted GPU work was not retired");
    require(failed!=success,"Incorrect terminal frame state");
    require(result==(submits!=0),"Consumed binary waits were not reported to the caller");
    require(waits==(mode==Scenario::SwapchainFailure&&!prepared?0u:1u),"Incorrect frame wait count");
    require(begins==(mode==Scenario::WaitFailure||(mode==Scenario::SwapchainFailure&&!prepared)?0u:1u),"Incorrect frame begin count");
    require(ends==(noBegin?0u:1u),"Incorrect frame cancellation count");
    if(steam)require(steamEndCalls==ends,"SteamVR end bookkeeping did not advance");
    if(ends)require(endedTime==displayTime,"Frame cancellation lost predicted display time");
    if(mode==Scenario::ImageWaitFailure)require(imageOwned&&!imageWaited&&!releases,"Unwaited image released unsafely");
    else require(!imageOwned,"Waited image was not released during recovery");
    require(deviceWaits==(mode==Scenario::FenceFailure?1u:0u),"Incorrect GPU recovery wait count");
    std::cout<<"steam="<<steam<<" prepared="<<prepared<<" failureLog="<<throwLog<<" scenario="<<int(mode)
        <<" calls="<<waits<<'/'<<begins<<'/'<<ends<<" released="<<releases<<" consumed="<<result<<'\n';
}
void runPreparation(Scenario mode,bool throwLog) {
    resetFrame(mode,true,throwLog);
    bool escaped{};
    try {
        prepareSteamFrame(device,handle<VkSwapchainKHR>(12));
        if(mode==Scenario::RepeatedAcquireLogFailure)prepareSteamFrame(device,handle<VkSwapchainKHR>(12));
    }catch(...) { escaped=true; }
    require(!escaped,"Preparation diagnostic escaped the successful Vulkan acquire");
    const bool waited=mode!=Scenario::EventFailure;
    const bool began=waited&&mode!=Scenario::WaitFailure;
    const bool prepared=began&&mode!=Scenario::BeginFailure;
    require(waits==unsigned(waited)&&begins==unsigned(began)&&!ends,"Preparation changed frame ordering");
    require(steamWaitCalls==waits&&steamBeginCalls==begins&&steamPreparedFrames==unsigned(prepared),
        "Preparation counters lost native lifecycle calls");
    require(steamFramePrepared==prepared&&steamFrameBegun==prepared&&nativeOpen==prepared,
        "Preparation lost native frame ownership");
    require(steamRepeatedAcquires==unsigned(mode==Scenario::RepeatedAcquireLogFailure),
        "Repeated acquire created a second frame");
    if(prepared) {
        require(steamPreparedFrame.predictedDisplayTime==displayTime,"Preparation lost predicted display time");
        scenario=mode==Scenario::NoRender?mode:Scenario::Normal;failureLog=false;
        Source source;source.extent=extent;source.images={handle<VkImage>(10)};
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        const auto consumed=presentQuadImpl(device,boundQueue,0,0,source,0,present);
        require(!failed&&!nativeOpen&&!steamFramePrepared&&!steamFrameBegun&&ends==1&&endedTime==displayTime,
            "A production-prepared frame was not presented exactly once");
        require(consumed==(mode!=Scenario::NoRender)&&waits==1&&begins==1&&steamConsumedPreparedFrames==1,
            "Presentation waited/began a second frame instead of consuming preparation");
    }
    std::cout<<"preparation scenario="<<int(mode)<<" failureLog="<<throwLog
        <<" calls="<<waits<<'/'<<begins<<'/'<<ends<<'\n';
}
}
}

int main() {
    unsigned failures{},scenarios{};
    for(const auto route: {std::array<bool,2>{false,false},std::array<bool,2>{true,false},std::array<bool,2>{true,true}}) {
        for(const auto mode: {argent::Scenario::Normal,argent::Scenario::NoRender,argent::Scenario::SwapchainFailure,
            argent::Scenario::WaitFailure,argent::Scenario::BeginFailure,argent::Scenario::AcquireFailure,
            argent::Scenario::ImageWaitFailure,argent::Scenario::ResetFailure,argent::Scenario::SubmitFailure,
            argent::Scenario::FenceFailure,argent::Scenario::ReleaseFailure,argent::Scenario::EndFailure,
            argent::Scenario::PreparedLogFailure}) {
            if(route[1]&&(mode==argent::Scenario::WaitFailure||mode==argent::Scenario::BeginFailure))continue;
            if(!route[1]&&mode==argent::Scenario::PreparedLogFailure)continue;
            for(const bool throwLog: {false,true}) {
                ++scenarios;
                try { argent::run(mode,route[0],route[1],throwLog); }
                catch(const std::exception& error) {
                    ++failures;
                    std::cerr<<"steam="<<route[0]<<" prepared="<<route[1]<<" failureLog="<<throwLog
                        <<" scenario="<<int(mode)<<": "<<error.what()<<'\n';
                }
            }
        }
    }
    for(const auto mode:{argent::Scenario::Normal,argent::Scenario::NoRender,argent::Scenario::WaitFailure,
        argent::Scenario::BeginFailure,argent::Scenario::PrepareLogFailure,
        argent::Scenario::RepeatedAcquireLogFailure,argent::Scenario::EventFailure}) {
        for(const bool throwLog:{false,true}) {
            ++scenarios;
            try { argent::runPreparation(mode,throwLog); }
            catch(const std::exception& error) {
                ++failures;
                std::cerr<<"preparation scenario="<<int(mode)<<" failureLog="<<throwLog<<": "<<error.what()<<'\n';
            }
        }
    }
    std::cout<<scenarios<<" scenarios, "<<failures<<" failures\n";
    return failures?1:0;
}
