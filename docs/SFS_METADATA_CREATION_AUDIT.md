# SFS Metadata Creation Audit

Commit: `4134bd9`. Category: stability. Reviewed 2026-10-03.
Baseline: `08473cd`.

## Scope And Reproduction

I traced shader modules, captured buffers, descriptor-set layouts, pipeline
layouts, descriptor pools and query pools through native allocation and metadata
registration. I also inspected their destruction paths and callers in shader
compilation, descriptor allocation and query replay.

The baseline linked fixture failed 45 of 57 scenarios. After native success,
host allocation failures left live shader, buffer, layout or query ownership.
Missing pipeline-layout metadata caused lookup failure after native allocation.
Query diagnostics could turn a registered pool into a failed creation result.
Failed native calls could publish poisoned outputs.

The fixture links the complete SFS runtime. Each case initializes fresh SFS
state on an RTX 4090 Vulkan device, then intercepts the target native allocations.
It runs the real capture bookkeeping and public `wrapProc` entry points.

## Changes

- Keep native ownership private until SFS and capture registration completes.
  Clear the caller's output before lookup and reject null successful outputs.
- Prepare shader words and pipeline-layout dependency counts before allocation.
  Preserve the projection binding and per-set dynamic descriptor counts.
- Guard shader, buffer, layout and query ownership with saved native destruction.
  Remove partial metadata before cleanup and preserve caller allocator identity.
- Give buffer creation the shared exception handler without adding a shared
  SFS metadata lock; capture bookkeeping retains its own mutex.
- Isolate query diagnostics after registration. Retain timestamp/occlusion
  expansion, pipeline-statistics passthrough and existing overflow rejection.
- Keep descriptor-pool outputs local through the result check. Pool preparation
  remains before allocation; flags and projection descriptor reservation are
  unchanged, with no allocating registration after native success.

I grouped the creation/destruction wrappers in `MetadataResources.inc` and
verified the extraction against the implementation before the move. Query
creation remains beside its replay logic. Published destruction, descriptor
pool retirement and pipeline-layout cache generations retain their behavior.

## Verification

The final linked fixture passed 88 scenarios. It covers 24 measured post-native
host allocation boundaries, eight preparation OOM cases with throwing diagnostics,
capture enabled/disabled, native failures, poisoned outputs, null successful
outputs, invalid top-level arguments and clean retry after rejected creation.
It also checks allocator identity, projection descriptors, missing layout
metadata, empty pipeline layouts, query policies and pool/query overflow.

Focused verbose CTest passed in 0.58 seconds. The final rebuild succeeded, and
all 86 harness checks passed in 44.49 seconds. I inspected the full raw diff and
raw diagnostics from the linked fixture and the real SFS graphics, performance,
capture-hook and screen-UI checks. Existing `/DNDEBUG`/`/UNDEBUG` warnings remain.

## Limits And Next Work

The fixture simulates target Vulkan allocation results; it does not exhaust
driver memory or submit its synthetic handles. Real-GPU integration checks cover
successful rendering and query replay. Private metadata containers have no test
accessor; the fixture checks native ownership, results and retry, while source
inspection establishes the rollback erasures.

I have not fixed descriptor-set or command-buffer allocation batches, internal
compiled-shader cache insertion, or graphics/compute pipeline batch ownership.
Descriptor sets from pools without the free-set flag cannot use unconditional
individual rollback. Pipeline batches also need their partial-success contract.
These remain high-value audit targets rather than covered cases.

HUD/hooks, input, broader capture lifetimes, package linking and native game/
headset validation remain in scope. This creation-time fix introduces no GPU wait
and has no measured gameplay FPS benefit.
