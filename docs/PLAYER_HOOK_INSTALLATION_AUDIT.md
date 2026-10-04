# Player Hook Installation Audit

Runtime commit: `548a7e1`.
Subsystem: Required player hooks and optional swimming/monkey-bar setup.
Category: Stability.

## Changes

The original installer interleaved signature checks, hook creation, activation
and allocating diagnostics. A later refusal left earlier hooks active. Retrying
then encountered patched signatures or existing hooks. Even the first refusal
published the image pointer. Optional monkey-bar and swimming setup could run
before the remaining required hooks succeeded.

We now check the 25 required code/field contracts before creating any hook.
We prepare all 21 required trampolines before publishing the image and activating
the group. A stack owner records successful creations and the previous image and
trampoline values. On failure it removes those owned hooks in reverse order,
restores their slots and permits a clean retry. MinHook's checked-in removal
operation disables an enabled target before freeing its trampoline. Failed
owned removal triggers `0xc0000602` rather than continuing with partial ownership.

A startup mutex excludes overlapping installers. A repeated call with the same
image returns the committed result without inspecting patched entry bytes;
a different image cannot replace that installation. No global hook enable,
disable, removal, queued apply or MinHook uninitialization occurs.

We defer optional setup until the required group commits. Optional monkey-bar
refusal or exceptions cannot change its successful result. Swimming activation
failure removes only that attempt's creation and restores its trampoline slot;
foreign creation conflicts remain untouched. Each diagnostic guards message
construction and logging. Successful hooks and trampolines retain the existing
process-lifetime policy.

`PlayerHookInstallation.inc` contains the production installer. The player source
is now 768 lines; the include is 124 lines. Gameplay callback behavior is unchanged.

## Evidence

`player_hook_installation_tests` includes the production installer with controlled
MinHook operations, synthetic Steam/Store code contracts and stub detours. Hidden,
bounded subprocesses isolate process-lifetime state. The original installer body
matches `b79ab92` after line-ending normalization; an ignored copy supplies the
baseline. The final 232-scenario fixture reports 220 baseline failures and zero
failures with the fix.

The fixture covers each required signature, creation failure, activation failure
and foreign conflict; clean retries; null/different images; all 12 successful-path
diagnostics; formatting allocation failure; optional refusal/exception and water
failures; checked fatal cleanup; and concurrent installers. The creation mock
supplies trampolines and activation patches entry bytes, so repeat checks cannot
pass by leaving signatures unchanged. Assertions check target/detour/slot wiring,
image and trampoline readiness before activation, retained successful state and
restored failed state. An unrelated active hook survives each schedule.

## Verification And Limits

The full harness rebuild succeeds, including a separate production player object.
All 107 CTest checks pass in 70.49 seconds. Six existing checks link the real hands,
focus, wall-climb, monkey-bar and both meathook assembly bridges and verify their
ABI/fallback behavior. Raw diffs and diagnostics were inspected. The existing
`/DNDEBUG` versus `/UNDEBUG` compiler warning remains; the harness scopes its
assertion option away from MASM. No normal-frame work or FPS gain is claimed.

These checks do not install player hooks in the game, establish rollback during
concurrent gameplay, or execute player callbacks through native MinHook
trampolines. The optional monkey-bar installer's own rollback remains to audit.
Presentation/revenant/DLSS ownership, persistent player state, calibration/input,
package linking and native game/headset lifecycle remain pending.
