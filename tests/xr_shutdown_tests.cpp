#include "../src/QuadRuntime.h"
#include "../src/vulkan/GpuRetirement.h"
#include <atomic>
#include <iostream>
#include <new>
#include <stdexcept>

namespace argent {
namespace {
enum class Scenario { Normal, Partial, CancelFailure, CancelLogFailure, FinalLogFailure, DeviceLoss, Mismatch, Unbound };
Scenario scenario{};
bool running{}, failed{}, gpuLive{};
unsigned waits{}, actionStops{}, cancels{}, destroyedSpaces{}, destroyedSessions{}, destroyedFences{}, destroyedPools{};
unsigned timingStops{}, bridgeStops{}, swapchainStops{}, pauseStops{};
std::recursive_mutex mutex;
Device device;
VkDevice boundDevice{};
VkFence fence{};
VkCommandPool pool{};
XrSpace space{}, localSpace{};
XrSession session{};
std::atomic<VkDevice> runtimeDevice{};
std::atomic<PFN_vkGetDeviceProcAddr> runtimeGdpa{};
void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
template<class T> T handle(std::uintptr_t value) { return reinterpret_cast<T>(value); }
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL fakeProc(VkDevice,const char*) { return nullptr; }
void cancelStereoImpl() {
    ++cancels;
    if(scenario==Scenario::CancelFailure||scenario==Scenario::CancelLogFailure)throw std::runtime_error("cancel failure");
}
struct Actions { void destroy() { ++actionStops; } } controllerActions;
struct Timing {
    void shutdownAfterCompletion() { require(!gpuLive,"Query resources destroyed before retirement");++timingStops; }
} copyTiming, diagnosticCopyTiming;
struct Bridge { void destroy(Device&) { require(!gpuLive,"Bridge destroyed before retirement");++bridgeStops; } } bridge;
void destroySwapchain() { require(!gpuLive,"Swapchain resources destroyed before retirement");++swapchainStops; }
void releasePauseBindings() { require(!gpuLive,"Pause resources destroyed before retirement");++pauseStops; }
VkResult fake_vkDeviceWaitIdle(VkDevice) {
    ++waits;gpuLive=false;
    return scenario==Scenario::DeviceLoss?VK_ERROR_DEVICE_LOST:VK_SUCCESS;
}
XrResult fake_xrDestroySpace(XrSpace target) {
    require(!gpuLive&&(target==space||target==localSpace),"Invalid space destruction");
    ++destroyedSpaces;return XR_SUCCESS;
}
XrResult fake_xrDestroySession(XrSession target) {
    require(!gpuLive&&target==session,"Invalid session destruction");++destroyedSessions;return XR_SUCCESS;
}
void fake_vkDestroyFence(VkDevice,VkFence target,const VkAllocationCallbacks*) {
    require(!gpuLive&&target==fence,"Invalid fence destruction");++destroyedFences;
}
void fake_vkDestroyCommandPool(VkDevice,VkCommandPool target,const VkAllocationCallbacks*) {
    require(!gpuLive&&target==pool,"Invalid command pool destruction");++destroyedPools;
}
}
void log(const std::string& message) {
    if((scenario==Scenario::CancelLogFailure&&message.rfind("STEREO_SHUTDOWN_END_FAILED ",0)==0)
        ||(scenario==Scenario::FinalLogFailure&&message=="XR_SHUTDOWN"))throw std::bad_alloc{};
}
namespace {
#define XR(name) fake_##name
#define VK(name) fake_##name
#include "../src/XrShutdown.inc"
#undef VK
#undef XR

void run(Scenario mode) {
    scenario=mode;
    waits=actionStops=cancels=destroyedSpaces=destroyedSessions=destroyedFences=destroyedPools=0;
    timingStops=bridgeStops=swapchainStops=pauseStops=0;
    running=gpuLive=true;failed=false;
    device.device=boundDevice=handle<VkDevice>(1);runtimeDevice=boundDevice;runtimeGdpa=fakeProc;
    fence=handle<VkFence>(2);pool=handle<VkCommandPool>(3);space=handle<XrSpace>(4);
    localSpace=handle<XrSpace>(5);session=handle<XrSession>(6);
    if(mode==Scenario::Partial){fence=VK_NULL_HANDLE;space=XR_NULL_HANDLE;}
    if(mode==Scenario::Unbound)boundDevice=VK_NULL_HANDLE;
    const auto target=mode==Scenario::Mismatch?handle<VkDevice>(7):device.device;
    const bool ignored=mode==Scenario::Mismatch||mode==Scenario::Unbound;
    bool escaped{};
    try { shutdownXRImpl(target); }
    catch(...) { escaped=true; }
    require(!escaped,"Diagnostic exception escaped shutdown");
    if(ignored) {
        require(!waits&&!cancels&&!actionStops&&running&&gpuLive&&!failed,"Unrelated device changed XR lifetime");
        require(runtimeDevice==device.device&&runtimeGdpa==fakeProc,"Unrelated device cleared runtime dispatch");
    }else{
        require(waits==1&&actionStops==1&&cancels==1&&!gpuLive,"Shutdown skipped cancellation or retirement");
        require(destroyedSpaces==(mode==Scenario::Partial?1u:2u)&&destroyedSessions==1
            &&destroyedFences==(mode==Scenario::Partial?0u:1u)&&destroyedPools==1,"Partial handles were not destroyed exactly once");
        require(timingStops==2&&bridgeStops==1&&swapchainStops==1&&pauseStops==1,"Subsystem cleanup was bypassed");
        require(!space&&!localSpace&&!session&&!fence&&!pool&&!boundDevice&&!running&&failed,"Retired handles/state were retained");
        require(!runtimeDevice&&!runtimeGdpa,"Destroyed device retained runtime dispatch");
        shutdownXRImpl(target);
        require(waits==1&&actionStops==1&&cancels==1&&destroyedSessions==1,"Repeated shutdown destroyed resources twice");
    }
    std::cout<<"scenario="<<int(mode)<<" waits="<<waits<<" spaces="<<destroyedSpaces<<" dispatchCleared="<<!runtimeDevice<<'\n';
}
}
}

int main() {
    unsigned failures{};
    for(const auto mode:{argent::Scenario::Normal,argent::Scenario::Partial,argent::Scenario::CancelFailure,
        argent::Scenario::CancelLogFailure,argent::Scenario::FinalLogFailure,argent::Scenario::DeviceLoss,
        argent::Scenario::Mismatch,argent::Scenario::Unbound}) {
        try { argent::run(mode); }
        catch(const std::exception& error) { ++failures;std::cerr<<"scenario="<<int(mode)<<": "<<error.what()<<'\n'; }
    }
    std::cout<<"8 scenarios, "<<failures<<" failures\n";
    return failures?1:0;
}
