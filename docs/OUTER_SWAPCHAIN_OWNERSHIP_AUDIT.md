# Outer Swapchain Ownership Audit

Runtime commit: `96a5716`. Category: stability.
Source-ring documentation checkpoint: `222a4db`.

## Finding

The outer Vulkan swapchain functions published native or SFS handles before
allocating image metadata and registering ownership. Allocation, enumeration or
diagnostic exceptions could escape the C API with a live, incompletely tracked
swapchain. SFS enumeration results were ignored; the native path could retain
empty or partially filled metadata. A smaller returned image count left stale
vector entries. Optional mirror allocation and non-standard exceptions could
also abort an otherwise valid source, and false mirror creation did not clean
partial resources. Cancellation exceptions could bypass source destruction.

## Fix

The production functions live in `LayerSwapchainLifecycle.inc`, included by both
the layer and the fixture. The unchanged extraction was compared byte-for-byte,
apart from line endings, before behavior was modified.

Creation clears failed outputs, validates required inputs and procedures, and
keeps the newly created native handle local until mandatory registration ends.
Native cleanup dispatch is resolved before creation. Only a successful,
non-null creation output becomes owned; poisoned failed outputs are never
destroyed. Host allocation exceptions become `VK_ERROR_OUT_OF_HOST_MEMORY`;
other setup exceptions become initialization errors.

Source enumeration must succeed with a non-empty, bounded returned count.
`VK_INCOMPLETE` retries are limited to three attempts. Metadata is resized to
the returned count, registered without overwriting another entry, and rolled
back with partial SFS image tracking on failure. Native error codes survive
enumeration failure. Non-game native creation adds no image tracking.

Mirror allocation, creation, registration and diagnostics are optional. Failed
or unpublished mirrors are destroyed without losing a valid stereo source.
Old-mirror removal follows the replacement attempt and applies only to a source
owned by this device; a foreign old handle cannot remove unrelated mirror state.
The lower-level source-ring tests independently cover old-chain retirement and
presentation of already-acquired old images.

Destruction ignores null handles, continues mandatory retirement after optional
cancellation exceptions, withdraws SFS tracking before native resource release,
and preserves native allocation callbacks. An exception during mirror or source
retirement fails fast rather than returning after unverified cleanup. Existing
GPU waits remain; no normal per-frame work or FPS optimization is added.

## Verification

`tests/layer_swapchain_lifecycle_tests.cpp` includes the production functions with
simulated native/SFS procedures and mirror ownership guards. It explicitly
permits throwing C-linkage fixture calls with `/EHs /EHc-`.

The initial thirty-six cases reproduce thirty failures on the original
functions, including escaped exceptions, failed outputs, incomplete metadata,
partial mirror ownership and interrupted teardown. All fifty-five strengthened
cases pass after the fix:

- Native and source creation, failed native output poisoning, null successful
  outputs, missing device/input/output and required dispatch.
- Ten measured allocation boundaries, four native and six source, with exactly
  one injected allocation failure per case and no abandoned handle/registry.
- Count/fill errors, shrinking image counts, a successful incomplete retry,
  persistent incomplete results and an excessive returned count.
- Partial SFS image registration, failed/throwing configuration and absent queue.
- Failed standard/non-standard mirror creation, throwing success/failure logs,
  continued cancellation-error teardown and unchanged caller create-info.
- Failed replacement removes a retired owned source's mirror, while a foreign
  old handle retains unrelated mirror ownership.
- Two bounded hidden subprocesses verify fail-fast exit `0xc0000602` when mirror
  or source retirement throws. Only owned fixture processes can be terminated.

Raw verbose `layer_swapchain_lifecycle` passes in 0.23 seconds. The full rebuild
passes, including `ArgentLayer.cpp`; all eighty-one CTest checks pass in 42.92
seconds. Full raw diffs and diagnostics were inspected. The existing
`/DNDEBUG` versus `/UNDEBUG` build warning remains.

## Limits And Next Work

The fixture simulates downstream dispatch, SFS tracking and the mirror. It does
not prove a linked Vulkan loader, actual desktop WSI or headset recreation.
`DesktopMirror::create` itself still needs allocation/native failure auditing,
including ownership of undefined outputs from failed resource creation. Core
SFS allocation wrappers, wider shaders, HUD/hooks, input and capture remain in
scope. Native game/headset lifecycle, full package linking and gameplay FPS are
unverified; this does not complete the end-to-end audit.
