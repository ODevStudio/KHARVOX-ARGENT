# HUD Hook Boundary Audit

Runtime commit: `5d83320`.
Subsystem: Native HUD callbacks, hook installation and tutorial text.
Category: Stability.

## Changes

HUD owner registration, geometry-mask allocation and panel publication could
throw before the detours called native update or geometry submission. Diagnostics
could also escape hidden-SWF suppression, canvas sizing and objective updates.
The update and submission detours now contain optional observer failures and
call their native callbacks once, outside the observer exception boundary.
Guarded diagnostics preserve canvas submission and objective scale restoration.
The existing native callback error handling remains unchanged.

Weapon-wheel installation previously removed every listed hook on failure,
including hooks it had not created. Optional POI creation failure could remove
another owner's hook. Installation now tracks successful required creations,
rolls back only those targets and adds optional hooks after required activation.
It installs tutorial bindings only after the required HUD hooks succeed.
Weapon-wheel configuration allocation failure returns false before acquiring hooks.
Guarded status logs preserve successful installation and failure results.

Tutorial lookup retains its bounded, immutable string cache. A diagnostic failure
no longer returns native text after caching its replacement; native callers get
the same retained replacement pointer on the first and subsequent calls.
Tutorial and hit-marker installers guard refusal, failure and success logs.
Executable signatures and vtable checks remain unchanged.

## Evidence

`hud_hook_boundary_tests` includes the production weapon-wheel and tutorial
implementations and links the production hit-marker implementation. One hundred
bounded hidden child scenarios exercise both approved build profiles with
controlled MinHook results, native callbacks and allocation/logging failures.
The canvas fixture supplies the audited return-address value; it does not invoke
an actual game call site.

The initial 82-case fixture reported 70 failures. After correcting its tutorial
label assertion and adding canvas, objective, restoration and formatting checks,
the same 100-case fixture reports **76 failures** against a source snapshot of
`d8b7892` and **zero failures** against the fix.

Checks cover five required creation conflicts and activation failures, optional
POI conflicts, eight setup allocation boundaries, throwing logs, mask allocation
and publication failure, native callback results and exceptions, wheel visibility,
unchanged native indices/alpha and tutorial pointer lifetime. Normal and throwing
native render callbacks restore GUI entity/projection fields, pending HUD state
and SWF suppression. Objective callbacks restore the native entry scale.

## Verification And Limits

The full focused harness rebuild succeeds, including separate production HUD
object compilation. All **94 CTest checks pass in 52.11 seconds**, including the
existing HUD layout/tutorial checks and the 100 new production-body scenarios.
Raw diffs and diagnostics were inspected. The only build warning in this stage
concerns the existing `/DNDEBUG` and `/UNDEBUG` settings.

These fixes add no GPU wait or frame-path allocation and claim no FPS gain.
Dispatch and native callbacks are controlled fixtures. Real MinHook rollback
during concurrent game execution, calibration polling, persistent camera/player
state restoration, package linking and headset/gameplay validation remain open.
