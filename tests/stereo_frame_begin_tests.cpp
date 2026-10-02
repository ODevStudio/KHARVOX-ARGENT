#include "../src/QuadRuntime.h"
#include "../src/openxr/OpenXRRuntimePolicy.h"
#include "../src/openxr/CinematicProjection.h"
#include "../src/openxr/CinewindowPosePolicy.h"
#include <iostream>
#include <new>
#include <stdexcept>

namespace argent {
namespace {
enum class Scenario { Normal, NoRender, InvalidTracking, WaitFailure, BeginFailure,
    UpdateFailure, LocateFailure, BeginLogFailure, FailureLogFailure, BothLogsFailure, EndFailure, SwapchainFailure };
Scenario scenario{};
bool nativeOpen{}, failed{}, running{true}, steamFrameBegun{}, previousMenuQuad{};
unsigned waits{}, begins{}, ends{}, swapchainCalls{};
XrTime endedTime{}, referenceChangeTime{};
constexpr XrTime displayTime = 123456;
std::recursive_mutex mutex;
Device device;
XrInstance instance{};
XrSession session{};
XrSpace space{}, localSpace{};
VkDevice boundDevice{};
XrSessionState sessionState = XR_SESSION_STATE_FOCUSED;
kharvox::OpenXRRuntimeKind runtimeKind{};
kharvox::CinewindowAnchor menuAnchor;
kharvox::CinewindowCaptureReadiness menuReadiness;
uint64_t steamOrderViolations{}, steamWaitCalls{}, steamBeginCalls{}, steamEndCalls{}, stereoSerial{};
namespace input {
struct Snapshot {};
Snapshot snapshot() { return {}; }
}
namespace camera {
void updateHeadsetFov(const XrPosef&, const XrView*, uint32_t) noexcept {}
float unitsPerMeter() noexcept { return 1; }
}
struct StereoPending { bool begun{}; sfs::FramePose pose{}; input::Snapshot hands{}; } stereoPending;
struct FrameTiming { struct Totals {}; FrameTiming(const char*, Totals&) {} };
struct Actions {
    void update(XrTime, bool, bool) {
        if (scenario == Scenario::UpdateFailure || scenario == Scenario::FailureLogFailure
            || scenario == Scenario::BothLogsFailure) throw std::runtime_error("action failure");
    }
} controllerActions;
struct Worker { template<class F> auto invoke(F&& function) { return function(); } } worker;
Worker& xrWorker() { return worker; }
void check(XrResult result, const char* operation) {
    if (XR_FAILED(result)) throw std::runtime_error(operation);
}
void events() {}
void startSession(Device&, VkQueue, uint32_t, uint32_t) {}
void createSwapchain(const Source&, uint32_t, bool) {
    ++swapchainCalls;
    if (scenario == Scenario::SwapchainFailure) throw std::runtime_error("swapchain failure");
}
XrResult fake_xrWaitFrame(XrSession, const XrFrameWaitInfo*, XrFrameState* state) {
    ++waits;
    state->predictedDisplayTime = displayTime;
    state->shouldRender = scenario != Scenario::NoRender && scenario != Scenario::EndFailure;
    return scenario == Scenario::WaitFailure ? XR_ERROR_RUNTIME_FAILURE : XR_SUCCESS;
}
XrResult fake_xrBeginFrame(XrSession, const XrFrameBeginInfo*) {
    ++begins;
    if (scenario == Scenario::BeginFailure) return XR_ERROR_RUNTIME_FAILURE;
    if (nativeOpen) return XR_ERROR_CALL_ORDER_INVALID;
    nativeOpen = true;
    return XR_SUCCESS;
}
XrResult fake_xrEndFrame(XrSession, const XrFrameEndInfo* end) {
    ++ends;
    if (!nativeOpen) return XR_ERROR_CALL_ORDER_INVALID;
    nativeOpen = false;
    endedTime = end->displayTime;
    return scenario == Scenario::EndFailure ? XR_ERROR_RUNTIME_FAILURE : XR_SUCCESS;
}
XrResult fake_xrLocateViews(XrSession, const XrViewLocateInfo*, XrViewState* state,
    uint32_t, uint32_t* count, XrView* views) {
    if (scenario == Scenario::LocateFailure) return XR_ERROR_RUNTIME_FAILURE;
    *count = 2;
    state->viewStateFlags = scenario == Scenario::InvalidTracking ? 0
        : XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
    for (unsigned eye = 0; eye < 2; ++eye) views[eye].pose.orientation.w = 1;
    return XR_SUCCESS;
}
XrResult fake_xrLocateSpace(XrSpace, XrSpace, XrTime, XrSpaceLocation* location) {
    location->locationFlags = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
    location->pose.orientation.w = 1;
    return XR_SUCCESS;
}
}
void log(const std::string& message) {
    const bool begin = message.rfind("[STEAM-XR-STEREO] begin ", 0) == 0;
    const bool failure = message.rfind("STEREO_BEGIN_FAILED ", 0) == 0;
    if ((begin && (scenario == Scenario::BeginLogFailure || scenario == Scenario::BothLogsFailure))
        || (failure && (scenario == Scenario::FailureLogFailure || scenario == Scenario::BothLogsFailure)))
        throw std::bad_alloc{};
}
namespace {
#define XR(name) fake_##name
#include "../src/StereoFrameBegin.inc"
#undef XR

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void run(Scenario mode, bool steam) {
    scenario = mode;
    nativeOpen = failed = steamFrameBegun = previousMenuQuad = false;
    waits = begins = ends = swapchainCalls = 0;
    steamOrderViolations = steamWaitCalls = steamBeginCalls = steamEndCalls = stereoSerial = 0;
    endedTime = referenceChangeTime = 0;
    stereoPending = {};
    menuAnchor = {};
    menuReadiness = {};
    runtimeKind = steam ? kharvox::OpenXRRuntimeKind::SteamVR : kharvox::OpenXRRuntimeKind::Unknown;
    instance = reinterpret_cast<XrInstance>(std::uintptr_t{1});
    session = reinterpret_cast<XrSession>(std::uintptr_t{2});
    device.device = boundDevice = reinterpret_cast<VkDevice>(std::uintptr_t{3});
    device.graphicsQueue = reinterpret_cast<VkQueue>(std::uintptr_t{4});
    Source source;
    source.extent = {1280, 1280};
    sfs::FramePose pose{};
    XrPosef head{};
    bool result{}, escaped{};
    try { result = beginStereoImpl(device, source, pose, head, false, nullptr, nullptr); }
    catch (...) { escaped = true; }
    if (mode == Scenario::Normal) {
        require(result && stereoPending.begun && nativeOpen && !failed, "Normal frame was not retained");
        require(pose.displayTime == displayTime && pose.serial == 1, "Frame pose did not advance");
        cancelStereoImpl();
    }
    const bool beforeWait = mode == Scenario::SwapchainFailure;
    const bool beforeBegin = beforeWait || mode == Scenario::WaitFailure || mode == Scenario::BeginFailure;
    const bool terminal = mode != Scenario::Normal && mode != Scenario::NoRender && mode != Scenario::InvalidTracking;
    require(!escaped, "Logging failure escaped before frame cancellation");
    require(!nativeOpen && !stereoPending.begun && !steamFrameBegun, "Begun XR frame was abandoned");
    require(failed == terminal, "Incorrect terminal/retryable frame state");
    if (terminal) require(!beginStereoImpl(device, source, pose, head, false, nullptr, nullptr)
        && swapchainCalls == 1, "Terminal setup failure retried a partial swapchain");
    require(waits == (beforeWait ? 0u : 1u) && begins == (beforeWait || mode == Scenario::WaitFailure ? 0u : 1u)
        && ends == (beforeBegin ? 0u : 1u), "Incorrect wait/begin/end call count");
    if (ends) require(endedTime == displayTime, "Cancellation used the wrong display time");
    std::cout << "steam=" << steam << " scenario=" << int(mode)
        << " calls=" << waits << '/' << begins << '/' << ends << '\n';
}
}
}

int main() {
    unsigned failures{};
    for (const bool steam : {false, true}) {
        for (const auto mode : {argent::Scenario::Normal, argent::Scenario::NoRender,
                argent::Scenario::InvalidTracking, argent::Scenario::WaitFailure,
                argent::Scenario::BeginFailure, argent::Scenario::UpdateFailure,
                argent::Scenario::LocateFailure, argent::Scenario::BeginLogFailure,
                argent::Scenario::FailureLogFailure, argent::Scenario::BothLogsFailure,
                argent::Scenario::EndFailure, argent::Scenario::SwapchainFailure}) {
            if (!steam && (mode == argent::Scenario::BeginLogFailure || mode == argent::Scenario::BothLogsFailure)) continue;
            try {
                argent::run(mode, steam);
            } catch (const std::exception& error) {
                ++failures;
                std::cerr << "steam=" << steam << " scenario=" << int(mode) << ": " << error.what() << '\n';
            }
        }
    }
    return failures ? 1 : 0;
}
