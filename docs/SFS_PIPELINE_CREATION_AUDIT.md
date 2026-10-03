# SFS Shader Cache And Pipeline Creation Audit

Runtime commit: `67b4306`.
Subsystem: SFS compiled-shader cache and graphics/compute pipeline ownership.
Category: Stability.

## Findings And Fix

The original `compiledModule` allocated a native shader before inserting it into
the cache. Host allocation failure during insertion leaked that module. The cache
could not destroy it during device shutdown because registration never completed.

Graphics and compute creation wrote application outputs before registering the
pipeline and its internal variants. Registration failure left incomplete outputs
and leaked native ownership. Throwing diagnostics could interrupt graphics
failure cleanup or turn completed creation into an error. Compute cleanup also
treated a failed native eye output as an owned pipeline.

The fix keeps compiled modules under a local ownership guard until cache insertion
completes. Registered modules retain their existing device-lifetime ownership.
Cache identity, shader transforms and projection/mono/indirect-eye policy remain
unchanged. Optional shader and compilation diagnostics cannot reject creation.

Each pipeline variant stays under a local ownership guard until mandatory SFS,
hand-depth and enabled capture registration completes. Rollback removes partial
metadata and destroys only successful native allocations, using the caller's
allocator. Guards run after the driver scope reacquires the metadata lock.
Failed or unattempted application outputs remain null. A null native success
returns initialization failure without registering a null handle.

The wrappers preserve the existing stop-on-first-failure batch policy and native
pipeline result, including `VK_PIPELINE_COMPILE_REQUIRED`. Already published
successful siblings retain their handles and metadata. They are not rolled back
when a later entry fails. Native compiled-shader failures retain the existing
initialization-failure translation.

Optional checkpoint, material-cache, driver-time and pipeline-build diagnostics
cannot unwind successful ownership. Pipeline destruction clears per-handle
checkpoint metadata as well as variant/capture metadata. Checkpoint label text
retains its existing device lifetime for submitted diagnostic references.

The implementation follows the existing private `.inc` organization:
`CompiledShaders.inc` contains compilation/cache ownership and
`PipelineCreation.inc` contains pipeline creation/destruction and build reporting.
`NativeSfs.cpp` is below the 800-line limit. No new dependency or GPU wait is added.
This creation/error-path fix claims no additional gameplay FPS.

## Failure-Injection Evidence

`tests/sfs_pipeline_creation_tests.cpp` links the complete SFS runtime and real
hand-depth/capture bookkeeping. It uses a real RTX 4090 Vulkan device, real
original shader modules, descriptor/pipeline layouts and mono/multiview render
passes. Target compiled modules and pipelines return synthetic native handles;
live-resource guards check their ownership and allocator pairing.

The baseline uses the unchanged `NativeSfs.cpp` from `aed7176`, compiled as an
isolated ignored harness target without reverting the working tree. The bounded
creation fixture reports **200 failures in 215 scenarios**. It reproduces module
cache-insertion leaks, post-native registration leaks, poisoned pipeline output
handling, incomplete batch outputs and diagnostic rejection of completed creation.

The expanded fixed fixture reports **321 scenarios, zero failures**. Coverage
includes graphics mono/multiview, shared compute and stereo compute with two
indirect-eye variants, each with capture disabled and enabled:

- Discovered post-pipeline host allocation boundaries and three post-module
  boundaries, with rollback or successful optional diagnostic recovery.
- Failure at each native pipeline variant, null successful native outputs,
  poisoned native failures and pipeline compilation-required results.
- Successful batch siblings retained after a later native or registration failure,
  including failure of later entries' internal variants with throwing diagnostics.
- Clean retry, compiled-cache reuse across a three-entry batch and a later request,
  and exactly-once variant/module destruction through device shutdown.
- Null top-level input/output pointers and throwing shader/build diagnostics.

The fixture warms the global hand tracker before counting allocations because its
cleared hash table retains capacity across SFS sessions. A 3 ms simulated native
pipeline delay exercises the existing timing-report thresholds; these fixture
durations are not performance measurements.

## Verification And Limits

The full harness rebuild passes. All **87 CTest checks pass in 52.63 seconds**.
After trimming only blank lines to keep `NativeSfs.cpp` under 800 lines, the
rebuild and six linked SFS checks pass in **9.12 seconds**. The final pipeline
fixture again reports 321 scenarios with zero failures. Both layer translation
units compile; real GPU graphics, performance, screen-UI and capture checks pass.
Raw staged diffs and relevant build/test diagnostics were inspected.

The fixture does not inject actual driver OOM or inspect private metadata tables.
Synthetic failure outputs test defensive handling, not a real driver violation.
Existing GPU checks cover rendered stereo pixels and replay, but not every
captured Eternal shader or native headset composition. Derivative pipeline batch
remapping remains unsupported as before. Material-cache failure recovery has
source review here; the existing standalone material-cache check does not prove
the entire production capture path under OOM.

Descriptor-set and command-buffer allocation batches remain to audit. A descriptor
pool without individual-free support needs pool-safe rollback. Full package
linking, game/headset lifecycle and gameplay performance measurements also remain
unverified; this subsystem does not complete the end-to-end audit.
