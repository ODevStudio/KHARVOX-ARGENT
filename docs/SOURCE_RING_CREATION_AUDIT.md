# SFS Source-Ring Creation Audit

Runtime commit: `9cd024c`. Category: stability.
Prior outer-frame documentation checkpoint: `ef4acb8`.

## Finding

`SourceRing::create` published partial ownership without protecting host
allocation boundaries. Image-registry growth and chain-map insertion could
throw after native images, dedicated memory and fences had been allocated.
Failed calls also retained stale output handles. A driver could write undefined
output values on failed creation, which the old rollback then destroyed.

Replacement retired the old source only after successful creation. Vulkan
requires an eligible old swapchain to retire even when replacement fails;
already-acquired old images remain presentable. The rule was checked against
the [Vulkan swapchain creation reference](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainCreateInfoKHR.html).

## Fix

Clear the output before work. Keep the unpublished chain in a standard
`unique_ptr` with rollback that withdraws image registration and destroys owned
native resources. Reserve image-registry capacity before publishing handles;
insert the map entry before transferring chain ownership. Only successful
native image, memory and fence outputs become owned.

Translate host allocation failure to `VK_ERROR_OUT_OF_HOST_MEMORY` and other
exceptions to `VK_ERROR_INITIALIZATION_FAILED`. Retire a valid old chain before
replacement work; reject foreign or already-retired old handles without
retiring unrelated chains. Rollback does not submit or wait for GPU work.

## Verification

`tests/sfs_source_ring_creation_tests.cpp` uses fixed fake-driver resource arrays
and an injected global allocation failure, so measured allocation boundaries
belong to the implementation rather than fixture bookkeeping.

- Fresh and replacement creation with two or five images exercises all twelve
  remaining host allocation boundaries, three per configuration.
- Eighty native failure cases cover each image, memory, bind and fence operation
  at all five slots, including poisoned failed outputs and old-chain replacement.
- Twelve unsupported-input cases cover flags, extension chains, layer count,
  empty dimensions and excessive image count, with and without replacement.
- Missing device-local memory, foreign/already-retired old chains and null
  output are rejected. An unrelated chain remains acquirable.
- Every failed replacement retires its valid old chain while preserving
  presentation of an already-acquired image. Resource and image-registry guards
  reject leaks and invalid destruction; teardown leaves no live resources.

The initial fixture reported 112 failing checks on the original implementation.
Output normalization alone left eighty failures, exposing the ownership and
retirement defects. Reserving the registry reduced measured host allocation
boundaries from eighteen to twelve; the final fixture passes all 106 checks.
The smaller final count reflects fewer implementation allocation boundaries,
not removed native failure coverage.

Creation, registry concurrency and real RTX 4090 source-ring checks passed in
0.56 seconds. The final full harness passes all eighty CTest checks in 43.53
seconds. The rebuild completed with the existing `/DNDEBUG` versus `/UNDEBUG`
warning. Full raw implementation/test diffs and focused diagnostics were read.

## Limits

This is setup-only stability work, not a measured gameplay FPS optimization.
The lower-level rollback does not prove the outer layer's source registration,
image enumeration, optional mirror setup or cancellation/destruction safe.
Those outer creation and teardown paths remain the next audit target. Native
headset recreation and full package linking remain unverified.
