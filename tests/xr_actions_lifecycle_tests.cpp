#include "../src/QuadRuntime.h"
#include "../src/vulkan/GpuRetirement.h"
#include "../src/openxr/GripThresholdPolicy.h"
#include "../src/psvr2/Psvr2IpcClient.h"
#include "../src/bhaptics/BhapticsIpcClient.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {
enum class Scenario {
    Normal, Partial, HapticFailure, TriggerFailure, AimLeftFailure, AimRightFailure,
    GripLeftFailure, GripRightFailure, SetFailure, PathFailure, ActionSetFailure,
    ActionFailure, SpaceFailure, AttachFailure, BhapticsStartFailure, Psvr2StartFailure,
    ReadyLogFailure, HapticsLogFailure, FailureLogFailure
};
Scenario scenario{};
bool inputActive{}, bhapticsRunning{}, psvr2Running{};
unsigned bhapticsStops{}, psvr2Stops{}, inputClears{}, hapticStops{}, triggerUpdates{}, setAttempts{};
unsigned createdSets{}, createdSpaces{}, createdActions{}, createdPaths{}, sessionStops{}, gpuWaits{};
std::vector<XrSpace> spaceAttempts;
kharvox::psvr2::TriggerCommand triggerCommand;
void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
template<class T> T handle(std::uintptr_t value) { return reinterpret_cast<T>(value); }
}

void KharvoxBhapticsIpcStart() {
    if(scenario==Scenario::BhapticsStartFailure)throw std::bad_alloc{};
    bhapticsRunning=true;
}
void KharvoxPsvr2IpcStart() {
    if(scenario==Scenario::Psvr2StartFailure)throw std::bad_alloc{};
    psvr2Running=true;
}
void KharvoxBhapticsIpcRequestStop() { ++bhapticsStops;bhapticsRunning=false; }
void KharvoxPsvr2IpcRequestStop() { ++psvr2Stops;psvr2Running=false; }
void KharvoxPsvr2SubmitTrigger(const kharvox::psvr2::TriggerCommand& command) { triggerCommand=command; }

namespace argent {
namespace input {
void clear() { ++inputClears;inputActive=false; }
bool install() { return true; }
bool installHaptics() { return true; }
const char* faceButtonComponent(bool,int,bool upper) { return upper?"b/click":"a/click"; }
const char* pauseComponent(bool) { return "menu/click"; }
}
void log(const std::string& message) {
    if((scenario==Scenario::ReadyLogFailure&&message.rfind("XR_INPUT_READY ",0)==0)
        ||(scenario==Scenario::HapticsLogFailure&&message.rfind("XR_HAPTICS ",0)==0)
        ||(scenario==Scenario::FailureLogFailure&&message.rfind("XR_INPUT_FAILED ",0)==0))throw std::bad_alloc{};
}
namespace {
XrInstance instance=handle<XrInstance>(100);
XrSession session{};
XrSpace space{},localSpace{};
VkDevice boundDevice{};
VkFence fence{};
VkCommandPool pool{};
Device device;
std::recursive_mutex mutex;
std::atomic<VkDevice> runtimeDevice{};
std::atomic<PFN_vkGetDeviceProcAddr> runtimeGdpa{};
bool running{},failed{};
void check(XrResult result,const char* operation) {
    if(XR_FAILED(result))throw std::runtime_error(operation);
}
XrResult fake_xrStringToPath(XrInstance,const char*,XrPath* out) {
    if(scenario==Scenario::PathFailure)return XR_ERROR_RUNTIME_FAILURE;
    *out=++createdPaths;return XR_SUCCESS;
}
XrResult fake_xrCreateActionSet(XrInstance,const XrActionSetCreateInfo*,XrActionSet* out) {
    if(scenario==Scenario::ActionSetFailure)return XR_ERROR_RUNTIME_FAILURE;
    ++createdSets;*out=handle<XrActionSet>(10);return XR_SUCCESS;
}
XrResult fake_xrCreateAction(XrActionSet,const XrActionCreateInfo*,XrAction* out) {
    if((scenario==Scenario::ActionFailure||scenario==Scenario::FailureLogFailure)&&createdActions==3)
        return XR_ERROR_RUNTIME_FAILURE;
    *out=handle<XrAction>(20+(++createdActions));return XR_SUCCESS;
}
XrResult fake_xrSuggestInteractionProfileBindings(XrInstance,const XrInteractionProfileSuggestedBinding*) {
    return XR_SUCCESS;
}
XrResult fake_xrCreateActionSpace(XrSession,const XrActionSpaceCreateInfo*,XrSpace* out) {
    if(scenario==Scenario::SpaceFailure&&createdSpaces==2)return XR_ERROR_RUNTIME_FAILURE;
    *out=handle<XrSpace>(++createdSpaces);return XR_SUCCESS;
}
XrResult fake_xrAttachSessionActionSets(XrSession,const XrSessionActionSetsAttachInfo*) {
    return scenario==Scenario::AttachFailure?XR_ERROR_RUNTIME_FAILURE:XR_SUCCESS;
}
XrResult fake_xrDestroySpace(XrSpace target) {
    require(std::find(spaceAttempts.begin(),spaceAttempts.end(),target)==spaceAttempts.end(),"Repeated space destruction");
    spaceAttempts.push_back(target);
    if((scenario==Scenario::AimLeftFailure&&target==handle<XrSpace>(1))
        ||(scenario==Scenario::AimRightFailure&&target==handle<XrSpace>(3))
        ||(scenario==Scenario::GripLeftFailure&&target==handle<XrSpace>(2))
        ||(scenario==Scenario::GripRightFailure&&target==handle<XrSpace>(4)))throw std::bad_alloc{};
    return XR_SUCCESS;
}
XrResult fake_xrDestroyActionSet(XrActionSet) {
    ++setAttempts;
    if(scenario==Scenario::SetFailure)throw std::bad_alloc{};
    return XR_SUCCESS;
}
struct ControllerActions {
    XrActionSet set{};
    XrAction aim{},grip{},stick{},trigger{},squeeze{},lower{},upper{},click{},menu{},indexMenu{},haptic{};
    std::array<XrPath,2> hands{};
    std::array<XrSpace,2> aimSpace{},gripSpace{};
    std::array<bool,2> heldGrip{};
    std::array<unsigned,2> handFilters{},gripProfiles{};
    bool ready{},indexMenuHeld{};
    uint64_t syncCount{};
    struct Configuration { bool leftHanded=true; } configuration;
    void updateHaptics(bool) {
        ++hapticStops;
        if(scenario==Scenario::HapticFailure)throw std::bad_alloc{};
    }
    void updatePsvr2Triggers(bool,bool,bool) {
        ++triggerUpdates;
        if(scenario==Scenario::TriggerFailure)throw std::bad_alloc{};
    }
#define XR(name) fake_##name
#include "../src/XrActionLifecycle.inc"
} controllerActions;

void cancelStereoImpl() {}
struct Timing { void shutdownAfterCompletion() {} } copyTiming,diagnosticCopyTiming;
struct Bridge { void destroy(Device&) {} } bridge;
void destroySwapchain() {}
void releasePauseBindings() {}
VkResult fake_vkDeviceWaitIdle(VkDevice) {
    require(!inputActive&&!bhapticsRunning&&!psvr2Running,"Controller exception bypassed mandatory cleanup");
    ++gpuWaits;return VK_SUCCESS;
}
void fake_vkDestroyFence(VkDevice,VkFence,const VkAllocationCallbacks*) {}
void fake_vkDestroyCommandPool(VkDevice,VkCommandPool,const VkAllocationCallbacks*) {}
XrResult fake_xrDestroySession(XrSession) { ++sessionStops;return XR_SUCCESS; }
#define VK(name) fake_##name
#include "../src/XrShutdown.inc"
#undef VK
#undef XR

void reset(Scenario mode) {
    scenario=mode;
    controllerActions={};
    bhapticsStops=psvr2Stops=inputClears=hapticStops=triggerUpdates=setAttempts=0;
    createdSets=createdSpaces=createdActions=createdPaths=sessionStops=gpuWaits=0;
    spaceAttempts.clear();triggerCommand={};
    inputActive=bhapticsRunning=psvr2Running=true;
    session=handle<XrSession>(101);space=localSpace=XR_NULL_HANDLE;
    device.device=boundDevice=handle<VkDevice>(102);fence=VK_NULL_HANDLE;pool=VK_NULL_HANDLE;
    runtimeDevice=boundDevice;runtimeGdpa=nullptr;running=true;failed=false;
}
void requireStopped(unsigned expectedSpaces,unsigned expectedSets) {
    require(!inputActive&&!bhapticsRunning&&!psvr2Running&&inputClears&&bhapticsStops&&psvr2Stops,
        "Input or IPC cleanup was bypassed");
    require(!controllerActions.ready&&!controllerActions.indexMenuHeld&&!controllerActions.syncCount,
        "Controller activity was retained");
    require(!controllerActions.heldGrip[0]&&!controllerActions.heldGrip[1]
        &&!controllerActions.handFilters[0]&&!controllerActions.handFilters[1]
        &&!controllerActions.gripProfiles[0]&&!controllerActions.gripProfiles[1],"Controller filters were retained");
    require(!controllerActions.set&&!controllerActions.aimSpace[0]&&!controllerActions.aimSpace[1]
        &&!controllerActions.gripSpace[0]&&!controllerActions.gripSpace[1],"Retired action handles were retained");
    require(spaceAttempts.size()==expectedSpaces&&setAttempts==expectedSets,"Resource cleanup attempts were skipped");
    require(triggerCommand==kharvox::psvr2::offCommand(true,kharvox::psvr2::TriggerOffReason::SessionEnd),
        "Session-end trigger command was skipped");
}
void runDestroy(Scenario mode) {
    reset(mode);
    controllerActions.ready=mode!=Scenario::Partial;
    controllerActions.set=handle<XrActionSet>(10);
    controllerActions.aimSpace={handle<XrSpace>(1),mode==Scenario::Partial?XR_NULL_HANDLE:handle<XrSpace>(3)};
    controllerActions.gripSpace={handle<XrSpace>(2),mode==Scenario::Partial?XR_NULL_HANDLE:handle<XrSpace>(4)};
    controllerActions.heldGrip={true,true};controllerActions.handFilters={1,1};controllerActions.gripProfiles={1,1};
    controllerActions.indexMenuHeld=true;controllerActions.syncCount=50;
    bool escaped{};
    try { shutdownXRImpl(boundDevice); }
    catch(...) { escaped=true; }
    require(!escaped,"Controller exception escaped through device teardown");
    requireStopped(mode==Scenario::Partial?2:4,1);
    require(hapticStops==(mode==Scenario::Partial?0u:1u)&&triggerUpdates==(mode==Scenario::Partial?0u:1u),
        "One optional shutdown failure skipped the other integration");
    require(gpuWaits==1&&sessionStops==1&&!boundDevice&&!session&&!running&&failed&&!runtimeDevice,
        "Controller failure interrupted outer XR teardown");
    const auto attempts=spaceAttempts.size();
    controllerActions.destroy();
    require(spaceAttempts.size()==attempts&&setAttempts==1,"Repeated cleanup retried retired handles");
    std::cout<<"destroy scenario="<<int(mode)<<" spaces="<<attempts<<" retirement="<<gpuWaits<<'\n';
}
void runCreate(Scenario mode) {
    reset(mode);
    bool escaped{};
    try { controllerActions.create(); }
    catch(...) { escaped=true; }
    require(!escaped,"Failure diagnostics escaped action creation");
    requireStopped(createdSpaces,createdSets);
    require(createdSets<=1&&createdSpaces<=4,"Unexpected resource creation");
    const auto attempts=spaceAttempts.size();
    controllerActions.destroy();
    require(spaceAttempts.size()==attempts&&setAttempts==createdSets,"Partial cleanup destroyed resources twice");
    std::cout<<"create scenario="<<int(mode)<<" sets="<<createdSets<<" spaces="<<attempts<<'\n';
}
void runSuccessfulCreate() {
    reset(Scenario::Normal);
    inputActive=bhapticsRunning=psvr2Running=false;
    controllerActions.create();
    require(controllerActions.ready&&createdSets==1&&createdSpaces==4&&createdActions==11,
        "Successful action creation changed");
    require(bhapticsRunning&&psvr2Running&&!bhapticsStops&&!psvr2Stops&&!setAttempts&&spaceAttempts.empty(),
        "Successful setup stopped integrations or destroyed actions");
    controllerActions.destroy();requireStopped(4,1);
    std::cout<<"successful create: 11 actions, 4 spaces\n";
}
}
}

int main() {
    unsigned failures{},scenarios{};
    for(const auto mode:{Scenario::Normal,Scenario::Partial,Scenario::HapticFailure,Scenario::TriggerFailure,
        Scenario::AimLeftFailure,Scenario::AimRightFailure,Scenario::GripLeftFailure,Scenario::GripRightFailure,
        Scenario::SetFailure,Scenario::PathFailure,Scenario::ActionSetFailure,Scenario::ActionFailure,
        Scenario::SpaceFailure,Scenario::AttachFailure,Scenario::BhapticsStartFailure,Scenario::Psvr2StartFailure,
        Scenario::ReadyLogFailure,Scenario::HapticsLogFailure,Scenario::FailureLogFailure}) {
        ++scenarios;
        try { if(mode>=Scenario::PathFailure)argent::runCreate(mode);else argent::runDestroy(mode); }
        catch(const std::exception& error) { ++failures;std::cerr<<"scenario="<<int(mode)<<": "<<error.what()<<'\n'; }
    }
    ++scenarios;
    try { argent::runSuccessfulCreate(); }
    catch(const std::exception& error) { ++failures;std::cerr<<"successful create: "<<error.what()<<'\n'; }
    std::cout<<scenarios<<" scenarios, "<<failures<<" failures\n";
    return failures?1:0;
}
