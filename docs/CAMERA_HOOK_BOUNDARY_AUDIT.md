# Camera Hook Boundary Audit

Runtime commit: `710b005`.
Subsystem: Camera callbacks, render controls and animation FOV.
Category: Stability.

## Changes

Render-control maintenance set its thread-local reentrancy flag without a scope
reset. A setter, allocation or diagnostic exception could leave that thread's
maintenance disabled. It also checked reentrancy after attempting the mutex.
The hook now checks the flag before locking and resets it on scope exit.
Individual diagnostic boundaries let later required controls continue after a
logging failure. The polling and camera detours contain optional maintenance
failures; polling also contains optional discovery failures. They call native
callbacks outside those exception boundaries, preserving native exceptions.

Actor-transition and body-yaw diagnostics could throw through `noexcept` camera
APIs. Those call sites now contain both message allocation and logging failures.
Camera entry/exit diagnostics no longer interrupt the native setter.

The animation-FOV detour constructed an allocating thread-local map before
calling the native finalizer. It also changed engine fields before allocating
the restoration record. A failed allocation could skip native finalization or
leave protected FOV values without restoration ownership. The map now initializes
on demand after native finalization. The detour computes protected values in
local storage, registers their restoration, then writes engine fields. Failed
ownership allocation leaves native FOV unchanged. A diagnostic failure after
publication retains the restoration record.

Restoration still runs on the next valid callback for that camera and preserves
external field changes. `stop()` does not write retained camera pointers or call
native CVar setters from the Vulkan/XR thread. Temporary bloom and diagnostic
light-culling scopes retain engine-callback restoration across VR/menu transitions.

## Evidence

`camera_hook_boundary_tests` includes the production camera implementation and
runs bounded hidden children with controlled native callbacks, CVar memory,
discovery and allocation/logging failures. Each scenario exercises both Steam
and Microsoft Store build profiles. The fixture does not install real hooks.

The initial fixture reported 32 failures in 44 cases against the pre-fix source.
After adding maintenance, formatting, restoration and ownership checks, the
60-case fixture reported **48 failures** against that source and **zero failures**
against the fix. The clean-release variant passes **58 cases**; it omits the
diagnostic-only performance-report scenario for each build profile.

Checks cover maintenance recovery and reentrancy, independent diagnostics,
continued polling after discovery failure, temporary CVar restoration, three
FOV ownership allocation boundaries, unchanged-FOV allocation avoidance,
diagnostic allocation failure after FOV publication, external FOV changes,
native callback results and exceptions, actor transitions and body-yaw logging.

## Verification And Limits

The five-check targeted run passes in 2.80 seconds, including the existing real
MinHook camera trampoline and Steam/Store render-control checks. A separate
non-testing production camera object compiles. The full harness rebuild succeeds;
all **99 CTest checks pass in 54.80 seconds**. Raw diffs and diagnostics were
inspected. The existing `/DNDEBUG` and `/UNDEBUG` warning remains.

These fixes add no GPU wait and claim no FPS gain. Controlled callbacks do not
establish native game execution, concurrent hook activation/rollback, IAT patch
ownership, persistent player-state restoration or complete DLL unload. Package
linking, calibration/input and headset/gameplay validation remain open.
