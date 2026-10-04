# Presentation Hook Audit

Runtime commits: `40cf63b` (installation), `1237c97` (callback boundaries).
Subsystem: Native presentation, pause, upgrade, death and dossier lifecycle.
Category: Stability.

## Installation

The original installer published build/player state before validation, attempted
cleanup of every target after partial creation, and ignored removal failures.
That could remove another owner's hook and leave freed trampolines in callback
slots. A final diagnostic exception terminated its `noexcept` boundary after
activation. Patched entry bytes also prevented matching retries.

Installation now validates all nine existing signatures without publishing state,
prepares every trampoline, then publishes the player type and build flag before
activation. A stack owner removes only successfully created hooks in reverse
order and restores previous trampoline/type/build values after failed setup.
Checked MinHook removal also disables an enabled target. Failed owned removal
fails fast with `0xc0000602` rather than returning with partial native state.

A startup mutex serializes overlapping installers. Matching committed retries
reuse installation; another image cannot replace it. Each setup/refusal log is
guarded, including string allocation. Successful hooks remain process-lifetime
resources. No global MinHook disable, removal or queued activation is used.

## Callback Boundaries

Menu and frame diagnostics could throw through native callbacks. Store first-frame
and drone logging ran before sample publication; render-frame logging could skip
the native consumer entirely. Formatting and logging are now guarded at each
diagnostic site. Pause observer locking cannot prevent the native transition.

The frame-consumer wrapper catches observer failure only, then calls its native
consumer outside the handler. Native exceptions remain native exceptions.
Valid decoded samples still publish after diagnostic failures, including player
and menu reset on leaving gameplay. Rejected/null frames retain the last sample.
The existing pause-child/force-close and compare-exchanged menu-owner semantics
are unchanged.

## Evidence

`presentation_hook_installation_tests` includes production source and uses bounded,
hidden child processes. Its pre-stage source copy is byte-identical to the file
at `0d8ae7a`, verified with SHA-256 before edits. Both source versions pass the
normal installation and callback cases.

The final 242-case fixture reports 198 original failures and zero fixed failures
across Steam and Microsoft Store profiles:

| Group | Scenarios | Original Failures | Fixed Failures |
| --- | --- | --- | --- |
| Installation | 118 | 116 | 0 |
| Native callbacks and frame publication | 124 | 82 | 0 |

Installation cases cover each signature, creation, activation, conflicting hook
and cleanup position; null input; setup/refusal logging and allocation faults;
same/different-image retries; and overlapping callers. Activation patches the
synthetic entry bytes, and verifies all nine native slots and dependencies are
ready. Failed attempts restore previous state, preserve foreign/unrelated hooks,
and permit clean retry. Removal failure produces the expected fail-fast status.

Callback cases cover normal, throwing-log and formatting-OOM paths for menu
transitions, null/rejected/first frames, drone changes and both frame reports.
The final-report OOM check confirms the first diagnostic completed before the
fault. Assertions retain native call counts/arguments, published classification,
pause-child ownership, foreign menu owners and out-of-game resets. Additional
cases inject an observer read exception and verify native exception propagation.
Memory reads use real Windows reads of fixture-owned objects; native callbacks
and MinHook dispatch are controlled.

## Verification And Limits

The full harness rebuild succeeds, including separate production presentation,
player, monkey-bar, camera and HUD objects. All 109 CTest checks pass in 72.53
seconds. Raw diffs and relevant diagnostics were inspected; the existing
`/DNDEBUG` versus `/UNDEBUG` warning remains.

No normal-frame GPU wait or allocation is added and no FPS gain is claimed.
These checks do not install hooks in the approved executable or prove safe
rollback with concurrent in-flight gameplay callbacks. Native menu/render-view
integration, revenant/DLSS ownership, persistent player state, calibration/input,
package linking and game/headset lifecycle remain pending.
