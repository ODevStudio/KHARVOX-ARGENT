# SFS Descriptor And Command Allocation Audit

Runtime commit: `634d678`.
Subsystem: SFS descriptor-set batches, command-buffer batches and command pools.
Category: Stability.

## Findings And Fix

The original descriptor wrapper allocated native sets before allocating projection
writes and capture/SFS bookkeeping. A host allocation failure stranded the native
batch. Individual frees would violate pools without the free-descriptor-set flag;
resetting a pool would invalidate unrelated sets the application still owns.

The wrapper now prepares output storage, projection writes, binding lookups and
capture map nodes before native allocation. Capture publication inserts C++17 map
node handles without another host allocation. Failed preparation leaves null
application outputs and does not call the driver. Native failures retain their
result and do not publish poisoned outputs. Projection bindings remain unchanged.

The removed `setDynamicCounts` and SFS `setPools` registries had no binding readers.
`bindSets` uses pipeline-layout counts through the existing descriptor-count cache.
Capture retains its own pool ownership and dynamic-offset metadata; the shared
`PoolMembers` helper and camera-capture registries remain unchanged.

Command-buffer allocation and command-pool creation previously published native
outputs before registry insertion. The wrappers now hold successful native
outputs until registration completes. Rollback removes partial command metadata
and frees only the new non-null buffers. Pool rollback uses the saved native
destroy procedure and the caller's allocation callbacks. Earlier registered
batches survive allocation and registration failure.

No dependency or GPU wait is added. Removing unused bookkeeping reduces host
allocation work, but these checks establish no additional gameplay FPS gain.

## Failure-Injection Evidence

`tests/sfs_batch_allocation_tests.cpp` links the complete SFS runtime and real
capture bookkeeping. Setup uses a real RTX 4090 device, descriptor layouts and
descriptor/command pools. Target allocations return synthetic handles; a live
resource ledger checks release, pool ownership and allocator pairing.

Before runtime/header edits, the initial linked fixture reported **130 failures
in 140 scenarios** against `c59aae0`. It reproduced post-native registration
leaks and poisoned failure-output publication. The expanded fixed fixture passes
**240 scenarios with zero failures**.

The fixture covers capture enabled/disabled, freeable/non-freeable descriptor
pools and primary/secondary command buffers:

- Every discovered preparation and registration allocation boundary, including
  throwing diagnostics and failures with an earlier live three-object batch.
- Native allocation errors, all-null successful outputs, missing layout metadata,
  null top-level arguments, clean retry and application-owned release.
- Partially null malformed successes, preserving the earlier batch and avoiding
  null command-buffer frees or projection writes to invalid sets.

For a three-set descriptor batch, the fixture counts two preparation allocations
without capture and twenty-five with capture, then **zero post-native host
allocations** in both pool modes. Three command buffers use one preparation and
six registration allocations; command-pool registration uses one allocation.
The fixture discovers later-batch registration boundaries independently.

## Verification And Limits

The full harness rebuild passes. All **88 CTest checks pass in 56.59 seconds**,
including the 240-scenario allocation check and linked graphics, performance,
screen-UI and capture-hook GPU checks. Both outer layer translation units compile.
Raw staged diffs and build/test diagnostics were inspected. Changed source files
remain below 800 lines; the audit ledger remains at 800 lines.

Vulkan guarantees valid handles for a successful allocation batch. If a broken
driver instead reports descriptor success with partially null outputs, the wrapper
rejects the batch without resetting the live pool. Valid siblings remain pool-owned
until the caller resets or destroys that pool, including on a freeable pool because
the wrapper does not retain its creation flags. The fixture verifies this limit
instead of claiming complete recovery from a driver contract violation.

Synthetic handles and injected host OOM do not establish real driver-OOM recovery,
every captured Eternal shader, headset composition or gameplay performance.
Broader hooks/capture, HUD/input, full package linking and native game/headset
validation remain in the end-to-end audit.
