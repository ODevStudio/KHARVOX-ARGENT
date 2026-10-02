# Stability and Performance Audit

## Scope

Base: `de22b367e1a01c5f39fba9f5651724bc2b0a6f29` (`origin/main`).
Audit started 2026-10-02. All changes remain local; no push is authorized.

Trace startup and device negotiation, game interception and SFS compilation,
frame publication, stereo resource ownership, FSR, hand/depth rendering, XR
handoff, desktop mirroring, diagnostics, input/integrations, and teardown.
Fix evidence-backed high-value stability or performance issues, retaining
required synchronization and validation. Commit substantial fixes separately
and record them here after each subsystem.

## Existing Local Commits

| Commit | Subsystem | Category | Change |
| --- | --- | --- | --- |
| `4add188` | FSR1 | Performance | Sample compatible owned UNORM stereo sources directly; retain incompatible/sRGB fallback. |
| `99377a7` | Hand rendering | Performance | Reuse matching owned scene framebuffers after GPU completion. |
| `0ae19f8` | SFS frame publication | Performance | Keep uniform memory mapped and isolate upload timing. |
| `12c9714` | SFS frame publication | Performance | Skip uniform retirement/copy for byte-identical payloads without skipping metadata progression. |
| `1d9e5f2` | SFS/XR handoff | Performance | Skip redundant source retirement only when XR confirms both wait consumption and source completion. |

Documentation checkpoints: `030b9f4`, `4554f1e`, `fa93971`, `ae0b4c2`,
and `0d2a56d`. These record audit evidence rather than changing runtime behavior.

Synthetic CPU measurements from the previous implementation work saved about
12.3 microseconds per eligible frame in total. At a CPU-bound 100 FPS this would
illustrate roughly 100.12 FPS, not a measured gameplay gain. FSR copy elimination
and framebuffer reuse have no quantified gameplay benefit yet. Changed uniform
payloads still retire GPU use before overwriting the single buffer.

## Audit Progress

| Subsystem | Status | Evidence and disposition |
| --- | --- | --- |
| Startup, launcher, device/runtime negotiation | Pending | Inspect failure handling, trust boundaries, and required capabilities. |
| Game interception, SFS shaders and command replay | In progress | Reviewed `NativeSfs.cpp`, `NativeDispatch.h`, `StereoResources.h`, and `TimestampQueries.inc`. Real GPU query aggregation, compute-state restoration, and warmed replay pass while an unrelated metadata writer is blocked. Broader shader variants and outer layer remain to verify. |
| SFS frame publication and stereo source ownership | Queue deadlock fixed; runtime verification pending | Six fresh checks pass, including bounded exhausted-acquire/present concurrency, queue-submit exclusion, fence-wait independence, publication failures, unchanged-payload metadata progression, completion-gated retirement, and real GPU source recreation. Keep changed-payload device retirement; asynchronous uniform slots need a separate lifetime design and runtime evidence. |
| FSR1 | GPU path reviewed; source retirement fixed | Fresh eight-format/path GPU checks pass direct sampling, sRGB fallback, failed source-view setup, abandoned recordings and OOM submission recovery. Isolated stereo GPU benchmark measured about 0.012 ms median savings on RTX 4090. Source destruction now requires verified queue retirement or device loss. Headset teardown remains unverified. |
| Hand/depth rendering and HUD | Upload/shutdown retirement fixed; wider HUD review pending | Verified upload and renderer destruction require queue completion or device loss. Failed command reset disables the affected model without recording. Four focused checks pass, including five isolated failure/device-loss scenarios and both stereo GPU layouts; wider HUD/input paths remain to inspect. |
| OpenXR frame loop and image handoff | Retirement failures fixed; wider lifecycle review in progress | Verify GPU retirement before releasing borrowed depth/source leases, flat-copy XR images, or shutdown resources. Three focused checks pass; frame/session state, creation failures and runtime recovery remain to inspect. |
| Desktop mirror | Fixed; headset validation pending | Stop unsafe retries after terminal acquire/record/submit/wait/present errors; preserve timeout/suboptimal behavior. Clear destroyed handles so partial recreation cannot double-destroy prior resources. Recover completion before returning a borrowed XR eye after a failed mirror wait. Preserve the existing one-shot blank and 60 FPS cadence. |
| Diagnostics and capture | Eye-readback retirement fixed; broader capture review pending | Production readback now rejects unsafe cleanup after a failed recovery wait. Six isolated call-site scenarios and real GPU pixel export pass. Disabled-path overhead and other capture lifetimes remain to inspect. |
| Input, camera/game hooks, and external integrations | Pending | Inspect state restoration, connection/error handling, and per-frame work. |
| Build, test, packaging, and end-to-end verification | Pending | Expand automated coverage and record unavailable runtime evidence explicitly. |

## New Fix Commits

| Commit | Subsystem | Category | Finding and verification |
| --- | --- | --- | --- |
| `aabbdb5` | Desktop mirror | Stability | Failure quarantine, verified error-path source retirement, queue-serialized destruction, and cleared handles. Failure-injection test reproduced a double-destruction on the original implementation, then passed after the fix. `ArgentLayer.cpp` compiled. |
| `08c2ae7` | SFS source ownership and queue synchronization | Stability | Move shared queue locking into actual source-ring submissions. An exhausted acquire no longer holds the queue mutex needed by present to release a slot. Bounded concurrency regression reproduced the old entry-point lock schedule, then passed after moving the lock; five GPU checks also pass. |
| `cbb995d` | Hand texture uploads and renderer teardown | Stability | Validate queue retirement before freeing upload staging or renderer resources; reject failed upload-command reset and missing upload dispatch functions. Isolated GPU-backed checks distinguish fail-fast retirement from unsafe destruction, and allow device-loss cleanup. Existing array-eye/separate-eye rendering and framebuffer reuse checks pass. |
| `31aa381` | OpenXR image handoff and source/runtime teardown | Stability | Require verified queue/device retirement or device loss before dropping live GPU resource leases. Flat-copy recovery drains the device to cover cross-queue bridge work; source retirement can no longer silently fail before caller destruction. Seven bounded failure checks pass, with both layer translation units compiled. |
| `b8ff384` | Diagnostic stereo eye readback | Stability | Check fallback queue retirement before destroying readback staging, fence or command pool. Production call-site failure injection reproduces unsafe destruction without the fix; six isolated scenarios and real GPU export pass with it. |

## Subsystem Evidence

### Desktop Mirror

`desktop_mirror` now tracks live handles and injects partial pool/fence creation,
acquire, fence-status, recording, submission, source-wait, and present failures.
It verifies no retries after terminal errors, exactly-once resource destruction,
queue-idle error recovery before releasing a borrowed source, safe handling of
out-of-date images, retryable not-ready/timeout, and successful suboptimal frames.
An unverified device-idle result retains resources instead of destroying live
objects; a lost device permits teardown. Existing aspect ratio, independent XR
output extent, cadence, per-image presentation semaphores, and no eye reads on the
disabled mirror path remain covered. The mirror becomes unavailable until source
swapchain recreation after a terminal failure. This is a stability fix, not a
claimed FPS improvement. If source retirement fails with a non-device-loss error
as well, the process fails fast rather than returning an in-use XR image.

### SFS Publication and Source Ring

Fresh raw diagnostics: `sfs_source_ring_gpu`, `sfs_graphics_gpu`,
`sfs_performance_gpu`, `sfs_capture_hooks_gpu`, and `sfs_screen_ui_gpu` all passed
on an RTX 4090. Tests exercise real GPU pixels, logical timestamp slots, 32/64-bit
occlusion aggregation, compute binding restoration, mapped-buffer failures, and
stereo/mono command replay. The source-completion shortcut must continue to
require both semaphore consumption and verified completion.

The outer layer previously held the shared queue mutex throughout a blocking
source acquire. When every slot was leased, acquire released only the ring mutex
while waiting for present, which needed the still-held queue mutex. A bounded
fake-driver regression using this original entry-point lock schedule failed
because present could not progress before acquire timed out. An infinite acquire
would deadlock. The fix removes both outer acquire/present locks and reuses the
same device mutex inside `SourceRing::submit`, preserving queue exclusion without
holding it across source-slot or fence waits. Removing just one outer lock would
leave an opposing ring/queue lock order.

`sfs_source_ring_registry` now verifies exhausted acquire/present progress,
acquire and retirement submissions blocked by an independent queue user, and
an independent queue user progressing during a blocked retirement-fence wait.
The six-check post-fix run passed; `ArgentLayer.cpp`, `QuadRuntime.cpp`, and the
SFS runtime compiled. Production call sites were inspected for opposing outer
locks; the concurrency test exercises the ring and modeled caller schedule,
not a linked game-layer or headset integration. No new average-FPS claim follows
from this stability fix.

### Hand Rendering and Resource Retirement

The texture upload path previously freed staging immediately when a successful
submission was followed by a failed queue-idle wait. Renderer shutdown likewise
ignored the wait result before destroying command pools, framebuffers, depth
copies and assets. Both now use the same checked retirement path. Success allows
normal cleanup; device loss permits teardown but does not mark an upload usable.
A different retirement error fails fast before destroying potentially live
resources. This deliberately prevents unsafe continuation rather than claiming
recovery from an unretired GPU submission. Production initialization/destruction
already hold the shared device queue mutex. The normal number of waits is unchanged.

The upload command reset result is now checked before recording, and required
upload/reset/wait functions are validated before common resources are created.
The affected model is disabled on a reset failure; other usable models retain
the existing partial-availability behavior.

`hand_renderer_retirement` runs five bounded, hidden child checks: upload wait
failure, shutdown wait failure, command-reset failure, upload device loss, and
shutdown device loss. The original upload code hit the unsafe-destruction guard
and failed the new check; shutdown/reset guards also rejected the original paths.
After the fix, both non-device-loss retirement errors produce the expected
fail-fast status before destruction, while reset/device-loss checks exit normally.
The injected errors follow a real successful GPU wait, so no actual device loss
or memory-pressure recovery is claimed. `hand_renderer_gpu`,
`hand_renderer_separate_eyes_gpu`, and `hand_scene_framebuffers` also pass, covering
both eye layouts, scene-depth preservation, reverse depth, HUD masking/placeholder
pixels, laser visibility, cached framebuffer reuse and matched destruction.
The hand renderer and both outer layer translation units compile in the focused
harness. This fix adds no per-frame work or quantified FPS gain.

### FSR1 GPU Path

Reviewed `Fsr1Upscaler.cpp`, `FsrRuntime.inc`, source capability negotiation, and
the current copy caller. Direct descriptors are limited to sampled owned UNORM
sources and the matching eye layer. Incompatible/sRGB sources retain the byte-copy
fallback. The direct source is restored to the copy caller's transfer-source
layout. Cached output includes image, layer, revision, rectangle and output extent;
discarded or unsubmitted work invalidates layout/revision bookkeeping. Fresh
`fsr1_gpu` checks all four supported formats with and without sampled sources,
including partial source-view failure and host/device OOM submission rejection.

An isolated GPU timestamp benchmark on RTX 4090 used two 1280x1280 source eyes,
2560x2560 output per eye, 160 frames per path and 140 post-warmup samples. Times
below sum the two FSR eye intervals, excluding source clears and output readback.

| Format | Copy Median (ms) | Direct Median (ms) | Saved (ms) | Copy p99 (ms) | Direct p99 (ms) |
| --- | --- | --- | --- | --- | --- |
| RGBA8 UNORM | 0.239616 | 0.226880 | 0.012736 | 0.716800 | 0.712672 |
| BGRA8 UNORM | 0.239616 | 0.228064 | 0.011552 | 0.719584 | 0.694528 |

This single sequential benchmark indicates roughly 5% less isolated FSR GPU work,
not 5% more gameplay FPS. Clock state, real shader content, resolution and the
CPU/GPU critical path can change the result. CPU and GPU savings cannot simply
be summed into an end-to-end FPS claim. No further per-frame FSR change is
justified here. Lazy fallback texture allocation could save VRAM but would add
descriptor/failure-path complexity without demonstrated frame-time benefit.

The source-retirement finding is fixed by `31aa381`: the layer caller can continue
source destruction only after successful queue retirement or device loss. Other
wait errors fail fast instead of silently returning. The compute checks above
do not prove full runtime teardown safe.

### OpenXR Retirement Failures

Stereo error recovery previously returned after both fence and queue waits failed,
dropping borrowed-depth leases without verified completion. Source retirement
likewise returned on a failed queue wait while its caller destroyed source images.
Shutdown ignored device-idle failure, and flat-copy error cleanup could release an
XR image while submitted copy work was still live. The shared retirement gate
accepts success or device loss and fails fast for other errors before lifetime
release. Device loss permits resource teardown but is not reported as successful
source completion. Logging/allocation failure cannot unwind through the gate.

Flat-copy recovery waits for the entire device, not just the XR queue: an earlier
cross-queue bridge submission can consume the original waits before the final XR
submission fails. Normal frame submission and wait counts remain unchanged.
Shutdown's wait and destruction use the shared queue mutex. Error recovery retains
XR image ownership when device loss leaves completion unverified.

Fresh `gpu_retirement`, `game_image_lifetime`, and `native_xr_release` checks passed
in 0.97 seconds. The new check launches seven bounded hidden children using real
borrowed-image leases and concurrent retirement: success/device loss allow cleanup;
host/device OOM, initialization failure, timeout and not-ready produce the expected
fail-fast exit without releasing the lease. It tests the shared gate and lifetime
primitive, not linked XR frame entry points or real driver failures. Both outer
layer translation units compile. Session/events, partial creation and full
headset shutdown remain outside this verification.

### Diagnostic Eye Readback

The readback resource destructor ignored a failed queue-idle result after a failed
fence wait, then freed resources still referenced by submitted work. It now reuses
the retirement gate before unmapping or destruction. Successful readback still
uses only its existing fence wait; recovery adds no normal-path work.

`gpu_transfer_retirement` exercises the production inline readback function with
fake dispatch and live-resource guards in six bounded hidden children: normal
output, failed fence with successful recovery, device-loss teardown, unverified
retirement, rejected submission and failed mapping. The original wait behavior
exits through the unsafe-destruction guard (`0x56`); the fix fails fast instead
(`0xc0000602`). Device loss permits teardown but does not export pixels. The
combined two-check run passed in 0.86 seconds, including `sfs_graphics_gpu` with
real RTX 4090 pixels and stereo PPM eye/row/channel validation. The outer layer
also compiles. These checks do not simulate a real GPU hang or memory pressure.

## Verification Limits

The current focused harness build compiled `ArgentLayer.cpp`, `QuadRuntime.cpp`,
SFS, hand rendering and FSR. Its final raw CTest run passed all 14 checks in
23.31 seconds, including the new queue-concurrency and hand-retirement checks.
Full raw implementation diffs and diagnostics were inspected. Existing diagnostics
are the harness's `/DNDEBUG` versus `/UNDEBUG` override and a synthetic mirror
handle conversion warning. These focused results do not prove the full audit
complete. Root-package linking remains unverified: the focused harness uses local
glslang without optimizer libraries, and the full package also requires the
external bHaptics SDK DLL. Headset/gameplay verification is still missing.
A real 120-300 second
combat capture with the desktop mirror disabled is required to quantify FPS,
CPU/GPU bottlenecks, and tail latency; synthetic timings are not a substitute.
