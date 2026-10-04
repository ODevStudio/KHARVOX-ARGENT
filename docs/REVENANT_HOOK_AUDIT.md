# Revenant Hook Audit

Runtime commits: `015299e` (installation), `af2a116` (command recovery).
Subsystem: Native campaign revenant input and view adapter.
Category: Stability.

## Installation

The original installer changed the native setters and expected type before
creation succeeded, left freed trampoline/state values after failed activation,
ignored removal failure and rejected retries against patched entry bytes. Null
input accessed the image, and final logging could terminate its `noexcept`
boundary after successful activation.

Installation now serializes callers, validates the existing three signatures
and vtable method, and creates the trampoline in local storage. Dependencies
publish only after creation succeeds, before activation. Failed activation
checks owned removal and restores all previous callback/type values. Cleanup
failure produces `0xc0000602` rather than continuing with abandoned hook state.
Foreign hooks are not removed. Matching committed retries reuse installation;
another image cannot replace it. Optional setup logging cannot change success.
The successful hook retains the existing process-lifetime policy.

## Command Recovery

Failed physics-position, current-command or binding reads returned unmodified
native commands without clearing the previous VR action mask. On the next valid
tick, that stale mask overwrote the native previous command and could suppress
a new action edge. Other fallback branches already cleared the same state.
The shared read/position refusal now clears it too, without changing native
fallback calls or modifying caller-owned command storage.

The periodic diagnostic now guards formatting/logging and uses an atomic counter
instead of an unsynchronized static count. Native thinker and setter calls stay
outside the diagnostic handler; their failures are not swallowed or retried.
The counter's thread safety follows from its atomic type, not a stress-test claim.

## Evidence

`revenant_hook_installation_tests` includes production source and runs bounded,
hidden children across Steam and Microsoft Store profiles. The source snapshot
was byte-identical to `2bb2043`, verified with SHA-256 before edits.

| Group | Scenarios | Original Failures | Fixed Failures |
| --- | --- | --- | --- |
| Installation | 30 | 20 | 0 |
| Commands and callback boundaries | 44 | 12 | 0 |

Installation covers signature/vtable refusal, null input, creation/activation
failure, a foreign target, checked removal failure, logging/allocation faults,
prepared dependencies, patched same/different-image retries and overlapping
callers. Unrelated live hooks survive, and clean failures permit retry.

Command checks cover throwing logs, formatting OOM, invalid/read-failed physics,
command/binding/previous reads, pause inhibition, null commands, non-local control,
inactive/stale/mismatched input, disabled gameplay, unavailable/invalid aim and
invalid native angle deltas. They check exact native fallback arguments, held
action continuity, reset after fallback, native exception propagation and
unchanged caller storage. Real Windows memory reads use fixture-owned objects;
camera providers, native callbacks and MinHook results are controlled.

## Verification And Limits

All 74 production-source scenarios pass. The existing linked native adapter and
detection/angle checks also pass and are now included in the focused harness.
Its full rebuild includes a separate production revenant object. All 112 CTest
checks pass in 75.30 seconds. Raw diffs and relevant diagnostics were inspected;
the existing `/DNDEBUG` versus `/UNDEBUG` warning remains.

No normal-path GPU wait or allocation is added; no FPS gain is claimed. These
fixtures do not install the hook in the approved game or prove rollback safe
with concurrent in-flight gameplay. Native possession/view/action behavior,
DLSS, persistent player state, controller IAT ownership, calibration/input,
remaining capture/shader paths and package/game/headset validation remain pending.
