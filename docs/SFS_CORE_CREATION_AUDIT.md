# SFS Core Resource Creation Audit

Commit: `b0079c4`. Category: stability. Reviewed 2026-10-03.

## Scope And Reproduction

I traced SFS image, image-view, render-pass and framebuffer creation through
native allocation, the image registry, hand-depth tracking and capture metadata.
I extracted the eight creation/destruction functions and result-handler macros
into production includes, then verified both sections against `626de48` before
changing behavior. The fixture includes those production sections and the real
`HandSceneDepthTracker.cpp` implementation.

The original code failed 74 of 83 scenarios. Failures included native leaks after
host allocation errors, published outputs after unsuccessful creation, late
discovery of missing cleanup dispatch and exceptions escaping through diagnostics.
The framebuffer logger could report failed creation after registration succeeded.

## Changes

- Keep successful native allocations under stack ownership until registration
  completes. Publish the caller's output at the end; clear it before lookup.
- Resolve cleanup dispatch before allocation and preserve the caller's allocator
  for rollback. Ignore poisoned outputs from unsuccessful native calls.
- Roll back image-registry insertion at `Images::create`, then remove SFS, hand
  and capture metadata on subsequent registration failure.
- Destroy both unpublished render-pass variants on partial setup failure.
  Preserve multiview masks, source-ring layout promotion and framebuffer policy.
- Bypass published-resource retirement during rollback. The native objects have
  not entered command recording; cleanup needs no GPU wait or lifetime entry.
- Return `VK_ERROR_OUT_OF_HOST_MEMORY` for host allocation failure. Contain
  standard and non-standard exceptions even when formatting or logging fails.
- Contain optional mixed-framebuffer diagnostics after required registration.

The shared result-handler change also affects other `RESULT_END` wrappers.
I have not established allocation rollback for those remaining wrappers.
Published-resource destruction retains its existing synchronization and cache
generation updates.

## Verification

The final fixture passed 94 scenarios, including 48 measured host allocation
boundaries across the four resource types with capture enabled and disabled.
It checks native failures at both render-pass allocations, poisoned failed
outputs, null successful outputs, missing creation/cleanup dispatch, invalid
top-level arguments and lookup exceptions with throwing diagnostics.

After rejected creation, the fixture checks native ownership and metadata,
then creates and destroys a replacement through the same state. Successful
checks cover stereo depth promotion, shader views, extension-chain metadata,
mixed/all-stereo framebuffer selection and caller allocator identity.

Focused raw CTest verification passed three checks in 0.38 seconds:
`sfs_core_creation`, `sfs_resource` and `sfs_multiview_gpu`. The GPU check wrote
both array layers through one multiview pass on an RTX 4090.

The complete harness rebuild succeeded. All 85 CTest checks passed in
43.76 seconds, including the real SFS graphics, performance, capture-hook and
screen-UI checks. I inspected their raw diagnostics and the full runtime diff.
The existing `/DNDEBUG` versus `/UNDEBUG` build warning remains.

## Limits

The allocation fixture uses simulated Vulkan dispatch and capture bookkeeping.
It runs the real image registry, hand tracker, resource wrappers and exception
handler. Real-GPU integration checks cover successful capture-enabled operation;
they do not inject host memory exhaustion into the full capture implementation.
Native game/headset lifecycle and full package linking remain unverified.

This creation-time stability fix adds no per-frame work or measured FPS gain.
Next: remaining SFS allocation wrappers, shader/hook ownership, HUD, input and
capture lifetimes.
