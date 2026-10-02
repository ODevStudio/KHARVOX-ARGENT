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
| Game interception, SFS shaders and command replay | Pending | Inspect resource/command lifetime, replay correctness, and hot-path work. |
| SFS frame publication and stereo source ownership | Pending | Inspect changed/unchanged publication, semaphore consumption, retirement, resize, and teardown. |
| FSR1 | Pending | Inspect direct sampling, fallback, image barriers, resize, and failure cleanup. |
| Hand/depth rendering and HUD | Pending | Inspect borrowed game depth and owned framebuffers, resource reuse, and retirement. |
| OpenXR frame loop and image handoff | Pending | Inspect frame/session state, acquire/wait/release, queue/fence failures, and shutdown. |
| Desktop mirror | Pending | Inspect optional-path pacing, WSI ownership, and error recovery. |
| Diagnostics and capture | Pending | Inspect disabled-path overhead and resource/readback lifetime. |
| Input, camera/game hooks, and external integrations | Pending | Inspect state restoration, connection/error handling, and per-frame work. |
| Build, test, packaging, and end-to-end verification | Pending | Expand automated coverage and record unavailable runtime evidence explicitly. |

## New Fix Commits

| Commit | Subsystem | Category | Finding and verification |
| --- | --- | --- | --- |

## Verification Limits

The prior focused harness compiled `ArgentLayer.cpp` and `QuadRuntime.cpp` and
passed 11 GPU/lifetime checks. This audit will collect fresh evidence; those
results alone do not prove the current audit complete. Headset/gameplay and
full-package verification have not yet been performed. A real 120-300 second
combat capture with the desktop mirror disabled is required to quantify FPS,
CPU/GPU bottlenecks, and tail latency; synthetic timings are not a substitute.
