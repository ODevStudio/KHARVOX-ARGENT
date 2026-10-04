# Monkey-Bar Hook Audit

Runtime commits: `bb53e72` (installation), `d7f66c0` (callback diagnostics).
Subsystem: Optional monkey-bar and facing-trigger adapter.
Category: Stability.

## Installation

The original installer published its image/facing state before validating the
required contracts. It left freed trampoline values in globals after rollback,
ignored cleanup failures and rejected retries against its patched entry bytes.
A final logging exception escaped after activating all five hooks. Null input
dereferenced the image before any refusal.

We validate the existing code and relative-call contracts using the supplied
image without changing global state. We prepare all five trampolines, then
publish the image, facing capability and verified native dash-stop callback
before activation. A stack owner records successful creations, previous slots
and previous adapter state. It rolls back only owned targets in reverse order,
restores previous values and permits a clean retry. Checked MinHook removal
disables an enabled target before freeing its trampoline. Failed owned cleanup
triggers `0xc0000602` instead of continuing with partial state.

A startup mutex excludes overlapping installers. Matching repeated calls reuse
committed installation without inspecting patched signatures. Another image
cannot replace it. The facing-trigger contract remains optional: its absence
disables that capability while retaining the required monkey-bar adapter.
Installation diagnostics guard both formatting and logging. Successful hooks
retain the existing process-lifetime policy; no global MinHook operation occurs.

## Callback Diagnostics

Six diagnostic sites could throw through native callbacks: accepted-bar dash
handoff, waiting/timeout completion, facing direction, cancellation deferral
and launch direction. We guard their formatting/logging without changing native
calls, completion values, deferred cancellation, snapshot consumption or heading
selection. Native callback errors remain outside these diagnostic guards.

## Evidence

`monkey_hook_installation_tests` includes the production source. Its bounded
hidden children use synthetic Steam/Store contracts, controlled MinHook results
and real Windows reads of fixture-owned player/controller/dash state. The
pre-stage source snapshot matches `6a6c2a7` after line-ending normalization.
The final fixture reports 142 failures in 162 scenarios against that snapshot
and zero failures against the fixes.

The 122 installation scenarios cover 30 required signature/relative-branch
fields, each creation/activation/conflict/cleanup position, null input, optional
facing refusal, dependency readiness, retained trampoline wiring, diagnostics,
formatting allocation, same/different-image retries and concurrent callers.
An unrelated live hook survives. Activation patches entry bytes, so retry tests
exercise committed state rather than unchanged signatures. During fixture
bring-up we corrected the completion prologue's stack-save byte to `0x18`, matching
the unchanged production contract. Normal cases pass on both source versions.

The 40 callback scenarios cover normal, throwing-log and formatting-OOM schedules
at all six diagnostic sites across both profiles, plus native-incomplete and
remote-facing fallback. The original callbacks fail 24 of those 40 cases. The
checks retain exact native completion/stop counts, no-refill dash requests,
protected cancellation, HMD heading and one-shot native-origin consumption.
Camera pose providers and native callbacks are controlled fixtures.

## Verification And Limits

The full harness rebuild succeeds, including a separate production monkey-bar
object and the existing real assembly bridge checks. All 108 CTest checks pass
in 67.19 seconds. Raw diffs and diagnostics were inspected. The existing
`/DNDEBUG` versus `/UNDEBUG` compiler warning remains. We add no normal-frame
GPU wait or allocation and claim no FPS improvement.

These checks do not install hooks in the approved game image or establish safe
rollback during concurrent gameplay. Native player/monkey-bar integration,
presentation/revenant/DLSS ownership, persistent player state, calibration/input,
package linking and game/headset lifecycle remain pending.
