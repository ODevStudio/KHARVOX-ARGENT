# Outer Frame Caller Audit

Runtime fix: `58c4a5a` (Stability).

## Findings and Fix

Optional preparation logs could throw before uniform publication, leaving an XR
frame open after successful begin. Mode logs could also interrupt preparation
before begin. Debug marker formatting and file/path allocation had the same
failure surface. Those diagnostics now contain exceptions without changing the
normal pose, shader policy or upload path.

After failed uniform upload, checkpoint queue-list allocation ran before camera
stop and frame cancellation. An injected allocation failure bypassed both cleanup
steps. Cleanup now runs first, checkpoint reporting is guarded, and cancellation
failure does not replace the original Vulkan result. Projection rejection and
missing-pair cancellation likewise cannot escape through the outer caller.

Presentation miss logging could bypass source retirement. Readback errors, mirror
handler logging and render tracing could also escape the outer frame boundary.
They are now isolated from required source retirement and completion publication.
The existing GPU completion gates, wait-consumption/source-completion flags and
native present wait stripping are unchanged. A failed optional sample may drop;
required GPU submission/retirement work is not wrapped in a suppressing handler.

The existing entry points moved into `LayerFramePreparation.inc` and
`LayerFramePresentation.inc`, matching the existing instance/device includes.
The fixture compiles those same functions instead of reimplementing the caller.
There is no new runtime abstraction, configuration or dependency.

## Verification

The initial 44-case fixture reproduces 24 failures on the unchanged functions:
escaping preparation/presentation diagnostics, bypassed publication or retirement,
checkpoint allocation before cleanup and cancellation error propagation.

All 51 strengthened scenarios pass in 0.08 seconds. They cover mode/cinematic/
recenter/hook/camera/UI/shader-policy/debug-marker logs, standard and nonstandard
exceptions, failed begin/upload, device loss and checkpoint OOM, both acquire
entry points, native/virtual/mono routing, suboptimal/not-ready/device-mask results,
missing pairs, incomplete XR handoff, optional readback/mirror/trace failures,
and native presentation. Checks verify pose and shader-policy publication,
exact cleanup counts, source-completion flags, unchanged caller present info,
native semaphore consumption and preserved aggregate/per-swapchain results.

The test uses `/EHs /EHc-` to unwind deliberately injected exceptions across its
C-linkage entry points; MSVC's default `/EHsc` assumes those calls do not throw.
This is test-only. The production compiler exception policy is unchanged.
The target is registered in the root CMake project and the focused harness.

The affected production layer compiles. All 79 focused CTest checks pass in
43.03 seconds. Full raw implementation/test diffs and diagnostics were inspected;
the existing `/DNDEBUG` versus `/UNDEBUG` warning remains.

These caller checks use stubbed XR/SFS/mirror/diagnostic dependencies, not a game
or headset. Separate existing checks cover their GPU/runtime components. Core
allocation failures, outer swapchain registration/destruction, broader interception
and capture lifetimes still need review. Root-package linking and native headset
lifecycle remain unverified. No average-FPS or end-to-end performance gain is claimed.
