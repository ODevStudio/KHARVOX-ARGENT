# Desktop Mirror Setup Ownership Audit

Runtime commit: `c492e57`. Category: stability.
Previous outer-swapchain documentation checkpoint: `9029efa`.

## Findings

`DesktopMirror::create` wrote native creation outputs directly into owning
members. Vulkan failed outputs are undefined; poisoned values could reach
destruction. Exceptions after successful native creation retained partial
ownership and left `needsFrame()` enabled. A second creation overwrote live
resources. Setup diagnostics could also reject a valid mirror.

Enumeration used allocated vector lengths rather than returned counts. When a
format list shrank, unwritten `VK_FORMAT_UNDEFINED` entries falsely enabled the
requested sRGB format. Unwritten present-mode entries similarly represented
Immediate mode. Shrinking image lists retained unnecessary per-image semaphores;
incomplete enumeration and host allocation failures stranded partial resources.

## Fix

Require the device/queue context and creation/cleanup procedures before acquiring
resources. Cache cleanup and device-retirement dispatch while it is available.
Only successful, non-null native outputs become owned; command buffers remain
pool-owned. Keep presentation disabled until the complete setup succeeds.

On setup exceptions, release owned, unsubmitted resources through the common
cleanup method before propagating the error. This adds no GPU wait: setup has
not submitted commands, acquired WSI images or presented. Published mirror
destruction retains the existing device-retirement check and queue lock. Each
released handle is cleared, and repeat destruction remains harmless.

Reject creation over a live mirror without changing its ownership. Guard optional
diagnostics and consume only returned enumeration entries. Retry incomplete
format, mode and image enumeration at most three times; reject empty/excessive
image results. Unsupported sRGB and zero-size desktop extents remain optional
mirror failures. The one-shot blank and 60 FPS pacing behavior is unchanged.

## Verification

`tests/desktop_mirror_creation_tests.cpp` includes the production header and uses
fixed fake-driver resource arrays, avoiding fixture allocations during injection.
The initial forty-four cases reproduce thirty-four failures on the old header.
All sixty-eight strengthened cases pass after the fix:

- Eighteen native failures cover chain, pool, command, loader, all four semaphore
  positions and fence setup, with ordinary and poisoned failed outputs.
- Seventeen measured host allocation boundaries cover blank and eye setup,
  with exactly one injected failure per case. Optional diagnostic allocation
  failures may still succeed; mandatory failures retain no native ownership.
- Thirteen enumeration cases cover shrinking lists, count/fill errors, zero or
  excessive image counts, successful retries and bounded persistent failures.
- Diagnostics, creation over a live object, clean retry after failed setup and
  destruction after the resolver becomes unavailable preserve ownership.
- Eleven missing procedures and five missing device/queue/context fields are
  rejected before native creation.

Three raw verbose checks pass in 0.28 seconds: mirror creation, existing mirror
presentation/pacing and the fifty-five outer-swapchain scenarios. The full
rebuild passes, including `ArgentLayer.cpp`; all eighty-two CTest checks pass
in 43.36 seconds. Full raw diffs and diagnostics were inspected. Existing
`/DNDEBUG` versus `/UNDEBUG` and synthetic `VkImage` conversion warnings remain.

## Limits

Native WSI procedures are simulated here. No linked desktop window, headset
recreation, real device loss or gameplay FPS gain is verified by these checks.
This is setup stability work, not an average-FPS optimization. Core SFS resource
allocation, broader shaders, HUD/game hooks, input and capture remain in scope;
the complete end-to-end audit and native package validation are unfinished.
