#include "../src/QuadRuntime.h"
#include "../src/XrWorker.h"
#include "../src/openxr/OpenXRRuntimePolicy.h"
#include "../src/openxr/StereoProjection.h"
#include "../src/vulkan/GpuRetirement.h"
#include <array>
#include <cstring>
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
bool stateLog{},nativeSessionRunning{},sessionBeginError{},sessionEndError{},inputActive{};
unsigned waits{}, begins{}, ends{}, releases{}, submits{}, deviceWaits{};
unsigned sessionStarts{},sessionStops{},inputClears{};
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
XrTime referenceChangeTime{};
std::vector<XrEventDataBuffer> queuedEvents;
size_t eventIndex{};
struct Actions { uint64_t syncCount{}; } controllerActions;
struct Pending { bool begun{}; } stereoPending;
namespace input { void clear() { ++inputClears;inputActive=false; } }
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
XrResult fake_xrPollEvent(XrInstance,XrEventDataBuffer* event) {
    if(scenario==Scenario::EventFailure)return XR_ERROR_RUNTIME_FAILURE;
    if(eventIndex==queuedEvents.size())return XR_EVENT_UNAVAILABLE;
    *event=queuedEvents[eventIndex++];return XR_SUCCESS;
}
XrResult fake_xrBeginSession(XrSession,const XrSessionBeginInfo*) {
    ++sessionStarts;
    require(!nativeSessionRunning,"Beginning an already running native session");
    if(sessionBeginError)return XR_ERROR_RUNTIME_FAILURE;
    nativeSessionRunning=true;nativeOpen=false;return XR_SUCCESS;
}
XrResult fake_xrEndSession(XrSession) {
    ++sessionStops;
    require(nativeSessionRunning,"Ending a stopped native session");
    if(sessionEndError)return XR_ERROR_RUNTIME_FAILURE;
    nativeSessionRunning=nativeOpen=false;return XR_SUCCESS;
}
#define XR(name) fake_##name
#include "../src/XrSessionEvents.inc"
#undef XR
}
void log(const std::string& message) {
    if((stateLog&&message.rfind("XR_STATE=",0)==0)
        ||(failureLog&&(message.rfind("QUAD_DISABLED ",0)==0||message.rfind("[STEAM-XR-SPLIT] prepare failed: ",0)==0))
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
    nativeSessionRunning=inputActive=true;stateLog=sessionBeginError=sessionEndError=stereoPending.begun=false;
    sessionStarts=sessionStops=inputClears=0;referenceChangeTime=0;controllerActions.syncCount=50;
    queuedEvents.clear();eventIndex=0;sessionState=XR_SESSION_STATE_FOCUSED;
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
    if(mode==Scenario::SwapchainFailure){
        scenario=Scenario::Normal;
        require(!presentQuadImpl(device,boundQueue,0,0,source,0,present)&&failed,"Terminal setup failure retried a partial swapchain");
    }
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
template<class T> XrEventDataBuffer eventBuffer(const T& event) {
    static_assert(sizeof(T)<=sizeof(XrEventDataBuffer));
    XrEventDataBuffer buffer{};std::memcpy(&buffer,&event,sizeof(event));return buffer;
}
void queueState(XrSessionState state,XrSession owner) {
    XrEventDataSessionStateChanged event{XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED};
    event.session=owner;event.state=state;queuedEvents={eventBuffer(event)};eventIndex=0;
}
struct StateCase { XrSessionState state;bool initialRunning=true,error=false,foreign=false; };
void runState(StateCase test,bool throwLog) {
    resetFrame(Scenario::Normal,true,false);stateLog=throwLog;running=nativeSessionRunning=test.initialRunning;
    sessionBeginError=test.error&&test.state==XR_SESSION_STATE_READY;
    sessionEndError=test.error&&test.state==XR_SESSION_STATE_STOPPING;
    if(!test.foreign&&test.state==XR_SESSION_STATE_STOPPING&&test.initialRunning)
        nativeOpen=stereoPending.begun=steamFrameBegun=true;
    queueState(test.state,test.foreign?handle<XrSession>(99):session);
    bool diagnosticEscaped{},nativeError{};
    try { events(); }
    catch(const std::bad_alloc&) { diagnosticEscaped=true; }
    catch(const std::exception&) { nativeError=true; }
    require(!diagnosticEscaped,"A consumed session event lost its transition to diagnostics");
    const bool terminal=test.state==XR_SESSION_STATE_EXITING||test.state==XR_SESSION_STATE_LOSS_PENDING;
    require(nativeError==(!test.foreign&&(test.error||terminal)),"Native session failure was swallowed or invented");
    const bool start=!test.foreign&&test.state==XR_SESSION_STATE_READY&&!test.initialRunning;
    const bool stop=!test.foreign&&test.state==XR_SESSION_STATE_STOPPING&&test.initialRunning;
    const bool expectedRunning=test.error?test.initialRunning:(start||(!stop&&test.initialRunning));
    require(running==expectedRunning&&nativeSessionRunning==expectedRunning
        &&sessionStarts==unsigned(start)&&sessionStops==unsigned(stop),"Native session ownership diverged");
    require(inputClears==unsigned(!test.foreign&&test.state!=XR_SESSION_STATE_FOCUSED)
        &&inputActive==(!inputClears),"Session event retained unfocused input");
    require(sessionState==(test.foreign?XR_SESSION_STATE_FOCUSED:test.state),"Foreign event changed session state");
    if(stop)require(stereoPending.begun==test.error&&steamFrameBegun==test.error
        &&nativeOpen==test.error&&!steamFramePrepared,"Session end result lost native frame ownership");
    std::cout<<"state="<<test.state<<" initiallyRunning="<<test.initialRunning<<" nativeError="<<test.error
        <<" foreign="<<test.foreign<<" logFailure="<<throwLog<<'\n';
}
void runStopRestart(bool throwLog) {
    resetFrame(Scenario::Normal,true,false);stateLog=throwLog;
    prepareSteamFrame(device,handle<VkSwapchainKHR>(12));
    require(nativeOpen&&steamFramePrepared&&steamFrameBegun,"No frame prepared for session stop");
    queueState(XR_SESSION_STATE_STOPPING,session);events();
    require(!running&&!nativeOpen&&!steamFramePrepared&&!steamFrameBegun&&!stereoPending.begun&&sessionStops==1,
        "Stopped session retained a frame for a later session run");
    queueState(XR_SESSION_STATE_READY,session);events();
    require(running&&sessionStarts==1,"Session did not restart");
    prepareSteamFrame(device,handle<VkSwapchainKHR>(12));
    Source source;source.extent=extent;source.images={handle<VkImage>(10)};
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    require(presentQuadImpl(device,boundQueue,0,0,source,0,present)&&!failed&&!nativeOpen
        &&waits==2&&begins==2&&ends==1&&steamConsumedPreparedFrames==1,
        "Restarted session reused the prior run's prepared frame");
    std::cout<<"stop/restart logFailure="<<throwLog<<" frameCalls="<<waits<<'/'<<begins<<'/'<<ends<<'\n';
}
void runOtherEvents() {
    resetFrame(Scenario::Normal,true,false);
    XrEventDataInteractionProfileChanged profile{XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED};
    profile.session=handle<XrSession>(99);queuedEvents={eventBuffer(profile)};events();
    require(controllerActions.syncCount==50,"Foreign profile event changed controller state");
    XrEventDataReferenceSpaceChangePending reference{XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING};
    reference.session=handle<XrSession>(99);reference.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;reference.changeTime=42;
    queuedEvents={eventBuffer(reference)};eventIndex=0;events();
    require(!referenceChangeTime,"Foreign reference event requested recentering");
    reference.session=session;reference.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_STAGE;
    queuedEvents={eventBuffer(reference)};eventIndex=0;events();
    require(!referenceChangeTime,"Stage reference event changed local recentering");
    profile.session=session;reference.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;
    queuedEvents={eventBuffer(profile),eventBuffer(reference)};eventIndex=0;events();
    require(!controllerActions.syncCount&&referenceChangeTime==42&&inputActive&&running,
        "Controller/recenter events lost their session scope");
    std::cout<<"Profile and reference events preserve session scope\n";
}
void runTerminalEvent(bool pollError) {
    resetFrame(pollError?Scenario::EventFailure:Scenario::Normal,true,false);
    XrEventDataInstanceLossPending loss{XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING};
    queuedEvents={eventBuffer(loss)};
    bool nativeError{};
    try { events(); }catch(const std::exception&) { nativeError=true; }
    require(nativeError&&!sessionStarts&&!sessionStops,"Terminal event did not reach the caller");
    std::cout<<"terminal event pollError="<<pollError<<'\n';
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
    for(const auto test:{argent::StateCase{XR_SESSION_STATE_READY,false},argent::StateCase{XR_SESSION_STATE_READY},
        argent::StateCase{XR_SESSION_STATE_STOPPING},argent::StateCase{XR_SESSION_STATE_STOPPING,false},
        argent::StateCase{XR_SESSION_STATE_FOCUSED},argent::StateCase{XR_SESSION_STATE_VISIBLE},
        argent::StateCase{XR_SESSION_STATE_SYNCHRONIZED},argent::StateCase{XR_SESSION_STATE_IDLE,false},
        argent::StateCase{XR_SESSION_STATE_READY,false,true},argent::StateCase{XR_SESSION_STATE_STOPPING,true,true},
        argent::StateCase{XR_SESSION_STATE_STOPPING,true,false,true},argent::StateCase{XR_SESSION_STATE_EXITING},
        argent::StateCase{XR_SESSION_STATE_LOSS_PENDING}}) {
        for(const bool throwLog:{false,true}) {
            ++scenarios;
            try { argent::runState(test,throwLog); }
            catch(const std::exception& error) { ++failures;std::cerr<<"state="<<test.state<<" logFailure="<<throwLog<<": "<<error.what()<<'\n'; }
        }
    }
    for(const bool throwLog:{false,true}) {
        ++scenarios;
        try { argent::runStopRestart(throwLog); }
        catch(const std::exception& error) { ++failures;std::cerr<<"stop/restart logFailure="<<throwLog<<": "<<error.what()<<'\n'; }
    }
    ++scenarios;
    try { argent::runOtherEvents(); }
    catch(const std::exception& error) { ++failures;std::cerr<<"other events: "<<error.what()<<'\n'; }
    for(const bool pollError:{false,true}) {
        ++scenarios;
        try { argent::runTerminalEvent(pollError); }
        catch(const std::exception& error) { ++failures;std::cerr<<"terminal event: "<<error.what()<<'\n'; }
    }
    std::cout<<scenarios<<" scenarios, "<<failures<<" failures\n";
    return failures?1:0;
}
