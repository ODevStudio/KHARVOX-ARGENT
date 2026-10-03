# Outer Vulkan Device Ownership Audit

Production entry points: `src/LayerDeviceCreation.inc`, included by
`ArgentLayer.cpp`. Runtime fix: `f80b764` (Stability). Previous checkpoint:
`f1c667e`. The extraction matched the original functions before modification.

## Confirmed Failures

The original device boundary let setup exceptions escape after native creation.
The production-call fixture reproduced an allocation failure in post-SFS
configuration with one simulated device and one SFS state still live. State and
queue-mutex allocation, family vectors, profile setup, instrumentation and
registry insertion had no common rollback. Registration preceded XR binding, so
a binding exception also left the device published.

Optional creation logs could abort setup or escape after successful registration.
Missing mandatory dispatch could reach null calls. Normal destruction resolved
its native procedure through an allocating cache after removing registry state.

## Fix

Creation keeps the output null and native ownership local until registration and
XR binding succeed. It checks required query/create dispatch before native
creation and caches native destruction before allocating setup state. SFS
ownership starts as soon as initialization succeeds. Rollback erases published
state, advances the dispatch generation, stops attempted camera activation,
retires owned SFS resources and destroys the native device with its allocator.
Camera trampolines retain their existing process lifetime.

Optional diagnostics cannot change creation results. Allocation exceptions return
`VK_ERROR_OUT_OF_HOST_MEMORY`; other setup exceptions return initialization
failure. Native failure outputs and null-success handles remain unpublished.
Auxiliary devices preserve downstream passthrough and the cached-create fallback.
Destruction uses cached dispatch. Missing mandatory cleanup or a teardown
exception fails fast before unsafe continuation.

## Verification

`layer_device_creation` runs 62 bounded hidden-child scenarios. The unchanged
functions failed 52 of these cases; the fixed entry points pass all 62. Coverage
includes non-game, game, mono/VR SFS, auxiliary/XR/deferred routing, loader
callbacks, extensions, multiview, throwing diagnostics, setup/registry OOM,
partial camera activation, binding failure, cleanup resolution and teardown.
Fail-fast cases use a distinct unsafe-destruction exit code to reject incorrect
cleanup. The strengthened fixture passes in 1.67 seconds.

Six related verbose checks pass in 2.72 seconds. The focused harness rebuilds
the layer and passes all 67 checks in 43.45 seconds, followed by the strengthened
device fixture. The build reports the existing `/DNDEBUG` versus `/UNDEBUG`
override. Full raw diffs and relevant diagnostics were inspected.

The fixture simulates driver, SFS and hook operations; it uses the real base
device allocation and profile configuration. Separate GPU-backed SFS tests pass,
but they do not prove linked loader/game/headset lifecycle behavior. Package
linking and gameplay FPS remain unverified. This setup/teardown fix adds no
per-frame work and carries no measured FPS claim.
