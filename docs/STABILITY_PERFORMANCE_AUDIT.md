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
| SFS frame publication and stereo source ownership | Reviewed; cross-path verification pending | Fresh five-test GPU run passes publication/map failures, unchanged-payload metadata progression, completion-gated retirement, consumed/unconsumed waits, submission failure, exhaustion, and recreation. Keep changed-payload device retirement; asynchronous slots need a separate lifetime design and runtime evidence. |
| FSR1 | Pending | Inspect direct sampling, fallback, image barriers, resize, and failure cleanup. |
| Hand/depth rendering and HUD | Pending | Inspect borrowed game depth and owned framebuffers, resource reuse, and retirement. |
| OpenXR frame loop and image handoff | Pending | Inspect frame/session state, acquire/wait/release, queue/fence failures, and shutdown. |
| Desktop mirror | Fixed; headset validation pending | Stop unsafe retries after terminal acquire/record/submit/wait/present errors; preserve timeout/suboptimal behavior. Clear destroyed handles so partial recreation cannot double-destroy prior resources. Recover completion before returning a borrowed XR eye after a failed mirror wait. Preserve the existing one-shot blank and 60 FPS cadence. |
| Diagnostics and capture | Pending | Inspect disabled-path overhead and resource/readback lifetime. |
| Input, camera/game hooks, and external integrations | Pending | Inspect state restoration, connection/error handling, and per-frame work. |
| Build, test, packaging, and end-to-end verification | Pending | Expand automated coverage and record unavailable runtime evidence explicitly. |

## New Fix Commits

| Commit | Subsystem | Category | Finding and verification |
| --- | --- | --- | --- |
| `aabbdb5` | Desktop mirror | Stability | Failure quarantine, verified error-path source retirement, queue-serialized destruction, and cleared handles. Failure-injection test reproduced a double-destruction on the original implementation, then passed after the fix. `ArgentLayer.cpp` compiled. |

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
stereo/mono command replay. No additional uniform/source lifetime change is
justified by this inspection alone. The source-completion shortcut must continue
to require both semaphore consumption and verified completion.

## Verification Limits

The prior focused harness compiled `ArgentLayer.cpp` and `QuadRuntime.cpp` and
passed 11 GPU/lifetime checks. This audit will collect fresh evidence; those
results alone do not prove the current audit complete. Headset/gameplay and
full-package verification have not yet been performed. A real 120-300 second
combat capture with the desktop mirror disabled is required to quantify FPS,
CPU/GPU bottlenecks, and tail latency; synthetic timings are not a substitute.
