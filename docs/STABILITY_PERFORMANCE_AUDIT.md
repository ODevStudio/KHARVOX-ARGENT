# Stability and Performance Audit

## Scope

Base: `de22b367e1a01c5f39fba9f5651724bc2b0a6f29` (`origin/main`).
Audit started 2026-10-02. All changes remain local; no push is authorized.

Trace startup and device negotiation, game interception and SFS compilation, frame publication, stereo resource ownership, FSR, hand/depth rendering, XR handoff, desktop mirroring,
diagnostics, input/integrations, and teardown. Fix evidence-backed high-value stability or performance issues, retaining required synchronization and validation. Commit substantial fixes separately and record them here after each subsystem.

## Existing Local Commits

| Commit | Subsystem | Category | Change |
| --- | --- | --- | --- |
| `4add188` | FSR1 | Performance | Sample compatible owned UNORM stereo sources directly; retain incompatible/sRGB fallback. |
| `99377a7` | Hand rendering | Performance | Reuse matching owned scene framebuffers after GPU completion. |
| `0ae19f8` | SFS frame publication | Performance | Keep uniform memory mapped and isolate upload timing. |
| `12c9714` | SFS frame publication | Performance | Skip uniform retirement/copy for byte-identical payloads without skipping metadata progression. |
| `1d9e5f2` | SFS/XR handoff | Performance | Skip redundant source retirement only when XR confirms both wait consumption and source completion. |

Documentation checkpoints: `030b9f4`, `4554f1e`, `fa93971`, `ae0b4c2`, `0d2a56d`, `95fde2b`, `ee72ebc`, `4476e5b`, `377d2be`, `079f5bf`, `6923f92`, `5d121be`, `ae440fb`, `a371dd1`, `498e876`, `9cf2fe1`, `f3c6090`, `4839406`,
`7b3c206`, `2a66aeb`, `fea8723`, `d529efe`, `d6589a1`, `2261177`, `b801747`, `a12aceb`, `f1c667e`, `1b2d47a`, `b21fe2d`, `093e147`, `ef4acb8`, `222a4db`, `9029efa`, `626de48`, `08473cd`, `aed7176`, `c59aae0`, `10d3bcc`, `d8b7892`, `2a747c2`, and `ca75b06`.
These record audit evidence rather than changing runtime behavior.

Synthetic CPU savings total about 12.3 microseconds per eligible frame. At a CPU-bound 100 FPS this illustrates about 100.12 FPS, not measured gameplay.
FSR and framebuffer reuse have no quantified gameplay gain; CPU/GPU timings cannot be summed.
Changed uniform payloads still retire GPU use before overwriting the single buffer.

## Audit Progress

| Subsystem | Status | Evidence and disposition |
| --- | --- | --- |
| Startup, launcher, device/runtime negotiation | Outer instance/device failures fixed; native startup pending | Four real probe subprocess scenarios, eighty-three startup/extension scenarios, fifty-eight session/swapchain scenarios, sixty-eight runtime Vulkan-creation scenarios, thirty-five outer instance scenarios and sixty-two outer device scenarios pass. Initialization publishes failure before guarded diagnostics; valid extension lists survive optional logging failures. Missing GPU-verification dispatch rejects setup before resource creation; partial session resources retire through existing shutdown. The creation callback preserves runtime-added features while copying downstream loader records. Outer instance/device registration rolls back native ownership on setup failure and caches cleanup dispatch. Device rollback retires initialized SFS state and attempted camera activation, including binding failure after registration. Complete GUI launch and native loader/runtime startup remain to verify. |
| Game interception, SFS shaders and command replay | Core/metadata/shader-cache/pipeline and allocation ownership fixed; broader hooks pending | Ninety-four core, eighty-eight linked metadata, 321 linked pipeline and 240 linked allocation cases pass, including host allocation boundaries, native failures, rollback, partial-success batches, cache reuse and protected diagnostics. Descriptor preparation precedes native allocation; publication makes no host allocations or caller-pool resets. The fixtures run real capture bookkeeping. Fourteen GPU-backed initialization cases and real multiview/query/replay checks pass. Broader shader variants and hooks remain to verify. |
| SFS frame publication and stereo source ownership | Queue deadlock and inner/outer creation rollback fixed; native lifecycle pending | All 106 source-creation and fifty-five outer-lifecycle cases pass, covering allocation/native failures, registry rollback, old-chain retirement, bounded enumeration, mirror isolation and cancellation-safe teardown. Existing queue, publication, metadata, completion-gated retirement and real GPU recreation checks pass. Keep changed-payload device retirement; asynchronous uniforms require lifetime design and runtime evidence. |
| FSR1 | GPU path reviewed; source retirement fixed | Fresh eight-format/path GPU checks pass direct sampling, sRGB fallback, failed source-view setup, abandoned recordings and OOM submission recovery. Isolated stereo GPU benchmark measured about 0.012 ms median savings on RTX 4090. Source destruction now requires verified queue retirement or device loss. Headset teardown remains unverified. |
| Hand/depth rendering and HUD | Hand/pause retirement and HUD callback/setup boundaries fixed; native HUD pending | Verified uploads and renderer destruction require queue completion or device loss. Failed command reset disables the affected model without recording. One hundred controlled HUD cases cover both build profiles, owned hook rollback, observer allocation/logging failures, native callback results and temporary GUI/POI state restoration. Existing hand GPU/layout and tutorial checks pass. Calibration polling and native HUD execution remain to inspect. |
| OpenXR frame loop and image handoff | Frame recovery and outer diagnostics fixed; native lifecycle pending | Fifty-one production outer-caller scenarios pass, preserving wait consumption, source completion, publication and Vulkan results despite optional failures. Twenty-two stereo-begin, 115 flat-present/preparation/event, ninety-nine stereo-present, eight shutdown, twenty controller-lifecycle and fifty-eight session/swapchain scenarios also pass. Native runtime events, image loss and headset shutdown remain to verify. |
| Desktop mirror | Presentation and inner/outer setup ownership fixed; native validation pending | Sixty-eight production-header setup cases pass, covering poisoned native outputs, seventeen host allocation boundaries, returned enumeration counts, clean retry and cached cleanup dispatch. Unsubmitted rollback adds no wait; published teardown retains retirement. Existing terminal-error, timeout/suboptimal, one-shot blank and 60 FPS cadence checks pass. Native WSI/headset validation remains pending. |
| Diagnostics and capture | Frame reporting, eye/camera-readback retirement and reset observations fixed; broader capture review pending | Frame timing contains diagnostic exceptions, including stack unwinding. Timeline reporting clears its bounded record window before formatting and rejects failed streams; eleven focused checks pass. Eye readback retains six isolated call-site scenarios and real GPU export. Camera readback now owns successful outputs, caches cleanup dispatch and requires verified retirement; forty-eight controlled scenarios pass. Twenty production reset-hook cases preserve metadata after native errors. The existing camera-recording check also passes. Other capture lifetimes remain to inspect. |
| Input, camera/game hooks, and external integrations | IPC ownership, controller teardown, camera callbacks and extent installation fixed; broader review in progress | Client worker lifetime and controller cleanup retain their verified policies. Camera maintenance recovers after exceptions and preserves native callbacks/FOV restoration. Camera and render-extent installation share startup exclusion, retain successful activation through logging failures and roll back only owned hooks/IAT writes with checked page protection. All 102 installation, sixty diagnostic and fifty-eight clean-release camera cases pass across both profiles, alongside native trampoline, render-control and IPC checks. Broader player/presentation/revenant/monkey-bar/DLSS hook ownership, persistent state, calibration/input and unload remain to inspect. |
| Build, test, packaging, and end-to-end verification | Fresh one-hundred-check suite passes; package and native verification pending | The full rebuild includes separate production camera and HUD compilation. All one hundred checks pass in 67.02 seconds, including 102 hook-installation cases, sixty diagnostic and fifty-eight clean-release camera cases, one hundred HUD cases, forty-eight camera readback and twenty reset-hook cases, SFS ownership, Vulkan/XR lifecycle and real GPU checks. Installed Steam build 25216728 matches the approved SHA-256. Full package linking, native headset lifecycle and gameplay benchmarks remain unverified. |

## New Audit Commits

| Commit | Subsystem | Category | Finding and verification |
| --- | --- | --- | --- |
| `aabbdb5` | Desktop mirror | Stability | Failure quarantine, verified error-path source retirement, queue-serialized destruction, and cleared handles. Failure-injection test reproduced a double-destruction on the original implementation, then passed after the fix. `ArgentLayer.cpp` compiled. |
| `08c2ae7` | SFS source ownership and queue synchronization | Stability | Move shared queue locking into actual source-ring submissions. An exhausted acquire no longer holds the queue mutex needed by present to release a slot. Bounded concurrency regression reproduced the old entry-point lock schedule, then passed after moving the lock; five GPU checks also pass. |
| `cbb995d` | Hand texture uploads and renderer teardown | Stability | Validate queue retirement before freeing upload staging or renderer resources; reject failed upload-command reset and missing upload dispatch functions. Isolated GPU-backed checks distinguish fail-fast retirement from unsafe destruction, and allow device-loss cleanup. Existing array-eye/separate-eye rendering and framebuffer reuse checks pass. |
| `31aa381` | OpenXR image handoff and source/runtime teardown | Stability | Require verified queue/device retirement or device loss before dropping live GPU resource leases. Flat-copy recovery drains the device to cover cross-queue bridge work; source retirement can no longer silently fail before caller destruction. Seven bounded failure checks pass, with both layer translation units compiled. |
| `b8ff384` | Diagnostic stereo eye readback | Stability | Check fallback queue retirement before destroying readback staging, fence or command pool. Production call-site failure injection reproduces unsafe destruction without the fix; six isolated scenarios and real GPU export pass with it. |
| `b292fd3` | Pause HUD texture upload | Stability | Verify queue retirement before image release and staging destruction; device loss disables the image without reporting completion. Five call-site scenarios use real WIC asset decoding and fake Vulkan/XR dispatch; the original unverified wait hits the unsafe-release guard. |
| `2fa1ae3` | Launcher runtime diagnostics | Stability | Replace blocking pipe reads before the ineffective timeout with bounded output collection. Stop only the owned probe on failure, preserve final output bytes and process-start errors, and prevent game launch after capture failure. Four real subprocess scenarios pass; restoring the original read/wait order reproduces the hang. |
| `523b2e2` | OpenXR copy error recovery | Stability | Retire submitted work before constructing/logging error strings so allocation failure cannot unwind live image leases first. Both layer translation units compile; lifetime/gate checks pass. Verification of handler order is source inspection, not linked XR OOM injection. |
| `e893499` | bHaptics/PSVR2 IPC clients and bridges | Stability | Drain cancelled overlapped operations before releasing stack storage, buffers and events. Stop connection waits after terminal wait errors. Four production-call-site regressions fail with the original 50 ms cleanup and pass with delayed completion; native Windows pipe cancellation also passes. |
| `6782cf1` | PSVR2 bridge worker ownership | Stability | Scope-own the pipe worker and require thread exit before releasing its context, security storage or wait handles. Normal and injected-exception delayed-worker regressions fail on the original implementation and pass after the fix; thread-start failure still returns the existing error. |
| `2b9cad1` | bHaptics/PSVR2 client worker lifetime | Stability | Serialize start, stop signaling and final handle retirement; retain a thread handle so quick session restart can verify the previous worker's exit. Publish started state only after configuration/event setup. Six stop/restart regressions reproduce the original races; lifecycle, startup-failure and existing cancellation checks pass after the fix. |
| `096a91f` | OpenXR stereo frame begin | Stability | Publish begun-frame ownership and predicted display time before SteamVR diagnostics; mark failure and cancel before guarded failure logging. Four original throwing-log scenarios fail; all twenty production-function scenarios and three existing lifetime/retirement checks pass after the fix. |
| `1bb44d5` | OpenXR flat/prepared frame presentation | Stability | Retire GPU work, release eligible images and cancel the tracked frame before guarded diagnostics; retain prepared display time and correct SteamVR begin/end flags. Original failure scenarios reproduce escaped logging and stranded native/prepared frame state. All seventy scenarios pass, including the production XR worker. |
| `07f7eef` | OpenXR stereo presentation recovery | Stability | Publish failure, retire submitted work, discard unsubmitted FSR state, release eligible eyes and cancel before guarded diagnostics. Forty-two original throwing-log regressions fail; all ninety-nine production-present scenarios and five related checks pass after the fix. |
| `73efca8` | OpenXR device teardown | Stability | Guard cancellation diagnostics and clear runtime dispatch before guarded final logging. Both original diagnostic exceptions escape teardown; eight production-shutdown scenarios pass after the fix. The full focused 47-check suite passes. |
| `11629d7` | Controller actions and XR teardown | Stability | Isolate optional haptic/trigger shutdown and action-resource dispatch exceptions; clean failed action creation before guarded diagnostics. Eight original regressions fail; twenty production-lifecycle scenarios pass, including continued outer GPU retirement and session destruction. Eight related checks pass in 1.34 seconds. |
| `d94645c` | SteamVR prepared-frame creation | Stability | Contain repeated-acquire and failure diagnostics without losing a successfully begun frame. Six original preparation regressions fail; fourteen production-preparation cases and seventy presentation cases pass on the real worker. Six related checks pass in 0.39 seconds. |
| `b9f1057` | OpenXR session events and restart | Stability | Guard state diagnostics and discard stereo/prepared frame markers after successful session end; retain ownership on end failure. Fifteen original event/restart regressions fail. All 115 flat/preparation/event scenarios pass; the full focused 48-check suite passes in 39.58 seconds. |
| `cb974b5` | Startup / XR session creation | Stability | Check selected and downstream Vulkan dispatch before UUID verification; skip optional timing when queue-property dispatch is unavailable. Four original cases exit with access violation. Twenty-six production setup/shutdown scenarios and five related checks pass; the layer compiles. |
| `4f9f6b3` | OpenXR composition swapchain ownership | Stability | Verification-only: exercise production creation/destruction through session shutdown, partial eye failures, cache reuse and recreation. Thirty-two added swapchain scenarios pass; frame fixtures confirm terminal failure isolation. Production extraction is identical, with no runtime behavior change. The full forty-nine-check suite passes. |
| `acd44d3` | Startup / Vulkan runtime creation | Stability | Guard optional creation, callback and binding diagnostics so successful native handles and resolved procedures survive logging exceptions. Genuine creation exceptions clear outputs, publish initialization failure and finish temporary callback cleanup. Seventeen original regressions fail; all fifty-eight production-function scenarios and the full fifty-check suite pass. |
| `0394b04` | Startup / OpenXR initialization and extension negotiation | Stability | Isolate optional startup and per-extension diagnostics; publish failure before guarded error logging. Catch outer manifest/worker exceptions and prevent cached partial initialization from reporting success. Twenty-six original regressions fail; all eighty-three production-function scenarios and the full fifty-one-check suite pass. |
| `d2f2e24` | Startup / runtime Vulkan feature and loader chains | Stability | Reuse the existing chain merge helper to retain runtime-added features, extensions and queues while copying downstream loader records. Catch host allocation failure at the callback boundary before native creation. Eight feature/loader cases and two allocation-boundary cases fail on the old adapter; all sixty-eight creation scenarios and the full fifty-one-check suite pass. |
| `68d0684` | Startup / outer Vulkan instance registration and teardown | Stability | Hold the native instance until registry insertion succeeds; roll back owned instance/messenger on setup exceptions, contain diagnostics, normalize failed outputs and cache cleanup dispatch. The initial fixture reports twenty-six failures on the original instance functions. All thirty-five final scenarios and the full fifty-two-check suite pass. |
| `01c0b4c` | SFS initialization / uniform and query-resource ownership | Stability | Preserve successful registration despite diagnostic exceptions; include state allocation in the handler and retire failed setup before guarded logging. Twelve original regressions fail on RTX 4090; all fourteen setup checks and the full sixty-six-check suite pass. Detailed evidence is linked below. |
| `f80b764` | Startup / outer Vulkan device registration and teardown | Stability | Keep native ownership local through registration/binding; check required dispatch, track SFS ownership before post-setup, roll back attempted camera activation and cache native destruction. Fifty-two original cases fail; all sixty-two production-entry-point scenarios pass. The full sixty-seven-check suite passes; detailed evidence is linked below. |
| `cd82f29` | Frame diagnostics / timing and presentation history | Stability | Contain destructor logging/allocation failures, reset bounded report windows before formatting and drop failed streams. Original checks reproduce process termination and retained `count=256`; four stronger cases expose incomplete output. Eleven focused checks pass, including fifty-seven timeline allocation boundaries and exception unwinding. |
| `9ca477c` | Launcher verification / probe output collection | Stability (tests) | Prebuffer more than the unchanged 1 MiB cap before the flood collection deadline; retain strict termination/time bounds and add a delayed writer. The previous fixture rejects a valid timeout after 300 ms startup delay. All five subprocess cases and the full seventy-eight-check suite pass. |
| `58c4a5a` | Outer frame acquisition / presentation | Stability | Guard optional logging, debug markers, readback, mirror handlers and tracing; cancel before checkpoint allocation and retain Vulkan results when cancellation reports failure. The initial forty-four-case fixture reproduces twenty-four failures. All fifty-one strengthened production-caller scenarios and the full seventy-nine-check suite pass. |
| `9cd024c` | SFS source-ring creation and replacement | Stability | Roll back partial native and registry ownership, clear failed outputs and retire valid old chains before replacement. The original fixture reports 112 failures; all 106 final checks pass after reducing implementation allocation boundaries. The full eighty-check suite passes; detailed evidence is linked below. |
| `96a5716` | Outer native/SFS swapchain creation and teardown | Stability | Keep native ownership local until registration, validate bounded enumeration, contain optional mirror/diagnostic failures and continue retirement after cancellation errors. Thirty initial regressions fail; all fifty-five strengthened cases and the full eighty-one-check suite pass. Detailed evidence is linked below. |
| `c492e57` | Desktop mirror setup and native ownership | Stability | Own only successful native outputs, roll back unsubmitted setup without GPU waits, cache cleanup dispatch and honor returned enumeration counts. Thirty-four original cases fail; all sixty-eight strengthened cases and the full eighty-two-check suite pass. Detailed evidence is linked below. |
| `b0079c4` | SFS core image/view/pass/framebuffer creation | Stability | Keep native outputs private until registration, roll back partial SFS/hand/capture metadata with saved cleanup dispatch, return host-OOM errors and isolate diagnostics. Seventy-four original cases fail; ninety-four final cases and all eighty-five harness checks pass. Detailed evidence is linked below. |
| `4134bd9` | SFS shader/buffer/layout/pool/query creation | Stability | Prepare shader words/layout dependencies before allocation, guard native ownership through real capture/metadata registration, keep failed outputs null and isolate query diagnostics. Forty-five baseline cases fail; eighty-eight linked cases and all eighty-six harness checks pass. Detailed evidence is linked below. |
| `67b4306` | SFS compiled-shader cache and graphics/compute pipeline ownership | Stability | Guard native modules through cache insertion and pipeline variants through metadata registration; isolate diagnostics, null incomplete outputs and retain successful batch siblings. Two hundred of 215 baseline cases fail; 321 expanded cases and all eighty-seven harness checks pass. Detailed evidence is linked below. |
| `634d678` | SFS descriptor and command allocation batches | Stability | Prepare descriptor writes/capture nodes before native allocation, remove unused per-set registries and publish without host allocation; own command buffers/pools through registration and roll back with saved dispatch and caller allocator. The initial fixture reports 130 failures in 140 cases; 240 expanded cases and all eighty-eight harness checks pass. Earlier batches survive failure without a recovery pool reset. Detailed evidence is linked below. |
| `c5afcf8`, `a40a55c` | Camera GPU readback and outer command-reset observations | Stability | Scope-own successful readback resources and mapping, cache cleanup dispatch, verify recovery retirement before destruction and contain diagnostics; protect the shared retirement-gate log and observe resets only after native success. The follow-up keeps report logging from dropping later buffer exports. The original readback fails twenty-two of forty-eight cases and reset hooks fail eight of twenty. Both fixed fixtures and existing camera recording pass; ninety-one harness checks are verified after fixture isolation. Detailed evidence is linked below. |
| `5d83320` | Native HUD callbacks, hook installation and tutorial text | Stability | Preserve native update/submission through observer failures, guard diagnostics and roll back only owned hooks; defer optional installation until required HUD activation succeeds. Retain consistent cached tutorial pointers despite logging failure. The final fixture fails seventy-six of one hundred cases against the original snapshot and passes all one hundred with the fix. All ninety-four harness checks pass; detailed evidence is linked below. |
| `710b005` | Camera callbacks, render controls and animation FOV | Stability | Reset maintenance reentrancy state on scope exit, contain optional maintenance/discovery and diagnostic failures, and register FOV restoration before changing engine fields. Forty-eight of sixty pre-fix scenarios fail; all sixty diagnostic and fifty-eight clean-release cases pass across Steam/Store profiles. Existing native trampoline and render-control checks pass; all ninety-nine harness checks pass in 54.80 seconds. Detailed evidence is linked below. |
| `14cbfa9` | Camera activation and render-extent hook/IAT ownership | Stability | Serialize installation, retain successful core activation through diagnostic failures and remove failed optional hooks only after owned creation. Scope-own extent hooks and compare-exchanged IAT writes, verify original page protection, restore still-owned output fields on refresh failure and fail fast if rollback cannot finish. The final pre-fix fixture fails forty-two of 102 cases; all fixed cases and one hundred harness checks pass. Installed Steam executable matches the approved hash. Detailed evidence is linked below. |

## Subsystem Evidence

SFS setup ownership: [detailed GPU-backed evidence](SFS_SETUP_OWNERSHIP_AUDIT.md).
Outer device setup/teardown: [ownership and failure-injection evidence](DEVICE_SETUP_OWNERSHIP_AUDIT.md).
Frame diagnostics: [bounded-buffer and exception recovery](FRAME_DIAGNOSTICS_AUDIT.md).
Probe fixture: [scheduling regression and deterministic byte-cap check](PROBE_FIXTURE_AUDIT.md).
Outer frame callers: [diagnostic isolation and retirement evidence](OUTER_FRAME_CALLER_AUDIT.md).
Source-ring creation: [allocation rollback and replacement retirement](SOURCE_RING_CREATION_AUDIT.md).
Outer swapchain lifecycle: [registration, mirror isolation and teardown](OUTER_SWAPCHAIN_OWNERSHIP_AUDIT.md).
Mirror internal setup: [native ownership, rollback and enumeration](MIRROR_SETUP_OWNERSHIP_AUDIT.md).
SFS core creation: [image/view/pass/framebuffer rollback and result boundaries](SFS_CORE_CREATION_AUDIT.md).
SFS metadata creation: [shader/buffer/layout/pool/query ownership and linked checks](SFS_METADATA_CREATION_AUDIT.md).
SFS shader cache and pipelines: [variant ownership, partial batches and diagnostic isolation](SFS_PIPELINE_CREATION_AUDIT.md).
SFS allocation batches: [pool-safe descriptor preparation and command registration rollback](SFS_BATCH_ALLOCATION_AUDIT.md).
Camera capture: [GPU readback retirement, scoped ownership and reset-result gating](CAMERA_CAPTURE_RETIREMENT_AUDIT.md).
Native HUD hooks: [callback boundaries, owned rollback and state restoration](HUD_HOOK_BOUNDARY_AUDIT.md).
Native camera hooks: [maintenance recovery, callback boundaries and FOV ownership](CAMERA_HOOK_BOUNDARY_AUDIT.md).
Hook installation and render extent: [activation, owned rollback, IAT protection and concurrent installation](HOOK_INSTALLATION_OWNERSHIP_AUDIT.md).

### Outer Vulkan Instance Ownership

After native success, dispatch lookup, messenger diagnostics or registry allocation
could escape the Vulkan entry point without retiring the instance. Creation now
keeps native ownership local until registration commits, returns typed allocation
or initialization errors, and leaves failed outputs null. Optional logs cannot
change results. Only successful messenger outputs become owned; messenger creation
requires a cleanup procedure. Teardown uses cached dispatch; unavailable mandatory
cleanup fails fast rather than returning with an abandoned owned instance.

The unchanged instance functions were extracted identically before testing.
The original thirty-two-case fixture reports twenty-six failures, including
invalid-input and failure-output cases. All thirty-five final cases pass, adding
messenger-backed registry OOM and transient cleanup-resolution coverage. Five
related checks pass in 1.11 seconds; the final full suite passes in 37.49 seconds.
Native dispatch and game/XR helpers are simulated; linked loader/headset creation
remains unverified. No per-frame optimization or FPS gain is claimed.

### Runtime Vulkan Feature Chains

The creation adapter copied the runtime's `VkDeviceCreateInfo`, then replaced its
entire `pNext` with the downstream chain. This discarded runtime-added features
and exposed original loader records to nested consumption. The existing
`RuntimeDeviceCreateChain` helper already preserves the runtime chain and copies
loader records, but production did not use it.

The callback now uses that helper. It retains the runtime's extension list and
queue configuration, while the existing outer guard restores shared application
feature links. Merge allocation failure returns `VK_ERROR_OUT_OF_HOST_MEMORY`
with a null output before native creation. No per-frame behavior or FPS gain is
claimed.

Ten added production-call scenarios fail on the old adapter: eight feature/loader
cases and two allocation-boundary cases across Steam/non-Steam routing. The
expanded fixture passes all sixty-eight cases on the real XR worker. It checks a
runtime-added timeline feature, application multiview, extension/queue retention,
copied callback/link records, original loader ownership, relink restoration,
native failure/null-success and throwing diagnostics. A thread-local allocation
injection rejects the helper's first vector allocation without native creation.
Five related raw verbose checks pass in 0.10 seconds; the strengthened multiview
assertion also passes, and the full fifty-one-check suite passes in 36.16 seconds.
The affected runtime translation unit compiles. Native loader/runtime negotiation,
outer layer registration and headset binding remain unverified.

### OpenXR Initialization and Extensions

The original production functions failed twenty-six of eighty-three scenarios.
Optional path, identity, completion and worker diagnostics could abort usable
startup. Failure diagnostics could escape before `failed` changed, leaving a
created instance eligible for a cached success despite incomplete initialization.
The outer manifest classification also escaped without recording an attempt.
Per-extension logging could truncate a valid extension list and disable XR.

The fix guards optional diagnostics, including message construction, and records
failure before error logging. The outer wrapper catches manifest and worker-call
exceptions and records failure under the existing state mutex. Failed queries return an
empty list; optional logging failures preserve both parsed extension names.
Loader search flags, runtime path selection, application identity, Steam/VDXR
worker routing and the force-enable1 compatibility setting stay unchanged.
This startup fix adds no per-frame work or measured FPS benefit.

The fixture includes `XrStartup.inc` and runs Steam-backed initialization on the
production `XrWorker`. All eighty-three scenarios pass, including five runtime
classifications, enable1/enable2 availability, compatibility overrides, missing
loader/export, extension enumeration, manifest failures, native setup errors,
throwing diagnostics and legacy-extension gates. Cached calls neither retry
failed setup nor accept partial initialization. The full focused build succeeds;
the raw verbose suite passes all fifty-one checks in 35.97 seconds.

The fixture simulates loader/export lookup, manifests and XR calls. It exercises
the wrapper's manifest exception boundary but does not inject worker thread or
queue allocation failure. Native loader procedure caching, malformed extension
responses, the outer Vulkan entry points and headset startup remain unverified.

### Vulkan Runtime Creation

The original production functions failed seventeen of fifty-two scenarios.
Diagnostics after successful native creation could clear the output handle while
leaving `VK_SUCCESS`, losing ownership of the created instance or device.
Diagnostic exceptions could also escape runtime procedure callbacks or interrupt
temporary callback cleanup after creation failure.

Optional creation, adapter-arming, callback and binding diagnostics now contain
their own exceptions, including string construction. Genuine creation exceptions
clear output handles and publish `VK_ERROR_INITIALIZATION_FAILED` before guarded
failure logging. The existing callback cleanup and loader-link restoration remain
in place. GPU UUID verification, result normalization and runtime routing are
unchanged. No per-frame optimization or FPS gain is claimed.

The new fixture includes the extracted production functions and uses the real
`XrWorker` for cross-thread runtime device callbacks. All fifty-eight scenarios
pass, covering native XR/Vulkan errors, null-success outputs, throwing native
calls and diagnostics, UUID mismatch, missing loader/properties, simulator
dispatch, KHR property fallback, Steam/non-Steam routing, binding and mediation
gates. It verifies output ownership, exact results, temporary pointer cleanup and
restoration of mutated downstream chain links. The full raw verbose suite passes
all fifty checks in 35.57 seconds; the affected layer translation unit compiles.

XR/Vulkan creation, handles, dispatch and resources are simulated. Those original
fifty-eight cases did not cover runtime-added feature chains; the follow-up above
adds that coverage. Outer layer creation/registration, native loader startup and
headset session binding remain separate audit work.

### Composition Swapchain Ownership

Thirty-two additional scenarios execute production swapchain creation and
destruction after production session setup, followed by production shutdown.
They cover one-layer, separate-eye and array-layer layouts; format enumeration
and unsupported formats; left/right creation and image-enumeration failures;
throwing completion diagnostics; cached reuse; resize, source-format,
display-encoding and layout changes; FSR output/fallback and mirror usage flags.
Every created native handle remains owned until cleanup, and repeated shutdown
does not destroy it twice. Matching cache keys retain hand resources; recreation
releases them before replacing the old composition images.

The existing flat-frame fixture now verifies that a later usable setup cannot
retry after a terminal swapchain error. The stereo-begin fixture adds both native
and SteamVR setup failures and verifies no wait/begin or repeated creation after
failure. All twenty-two stereo-begin cases and 115 flat/preparation/event cases
pass. Partial creation can leave incomplete cached resources, but these callers
already quarantine the runtime and do not reuse them. Adding retry machinery or
changing cache publication is not justified by current callers.

The swapchain functions moved unchanged into `XrSwapchainCreation.inc` for direct
coverage. Exact comparison confirms the extraction is identical after line-ending
normalization. The session target now passes all fifty-eight cases, including the
earlier twenty-six session checks. The full focused build and forty-nine-check raw
verbose CTest run pass in 35.96 seconds. No runtime fix or FPS gain is claimed.

Native handles, XR dispatch, completion, hand ownership and FSR output selection
are simulated in this fixture. Earlier real GPU tests cover FSR rendering and
hand resources, but do not verify native composition-swapchain destruction,
runtime image loss, allocation exceptions or headset recreation. The current
tests do not establish recovery from unavailable destruction procedures or failed
native destruction. Those limits remain explicit rather than being treated as
passing hardware coverage.

### Session Creation and Partial Cleanup

Session creation called the selected Vulkan loader procedure and downstream
instance dispatch before checking either pointer. Missing module, missing export
and missing downstream dispatch each reproduced an access violation
(`0xc0000005`). Optional diagnostic timing also called an unresolved queue-family
procedure and reproduced the same crash.

The selected and downstream procedures are now checked before UUID lookup.
Unavailable verification still rejects setup before native session/resource
creation; UUID identity and dispatch ownership are unchanged. Missing optional
queue-family dispatch skips timing initialization rather than rejecting an
otherwise usable session. The simulator retains its downstream-only path even
when the unused public loader module is absent. No per-frame work is added.

All twenty-six production-function scenarios pass. They cover enable1/enable2,
cached device selection, requirements/selection failures, UUID mismatch,
unavailable dispatch, native session failure, format enumeration, reference
spaces, pool/command/loader-data/fence failures, timing and throwing diagnostics.
Every tested partial resource set already retires through production shutdown,
with exactly-once destruction and repeated-shutdown safety. No additional
rollback mechanism is justified. The six-check raw verbose run passes in 0.21
seconds, and `QuadRuntime.cpp` compiles.

XR/Vulkan dispatch, loader lookup, GPU completion, actions and pause resources
are simulated. The fixture includes the production creation/shutdown functions,
not a linked game startup or native runtime. Separate earlier tests cover pause
upload and action ownership. SteamVR binding overrides, composition swapchain
creation, native retry and headset teardown remain to verify. No FPS gain is
claimed.

### Session Events and Restart

The event loop consumed a session-state event, then logged before clearing
unfocused input or calling `xrBeginSession`/`xrEndSession`. A diagnostic exception
could therefore lose a transition that the runtime would not resend. Successful
session end also retained stereo/prepared frame markers. A later READY event
could restart the session while presentation still referenced a frame prepared
during the prior run.

The [OpenXR session specification](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/session.adoc)
requires runtime frame state to reset when a session starts running again. It
also requires the application to stop its frame loop before ending the session
and avoid frame/input/haptic calls afterward. The event loop now guards its
state diagnostic and clears the three frame markers after successful session
end. A failed end still propagates with running/frame ownership retained. The
fix adds no frame-end call or rendering wait. Production events moved into
`XrSessionEvents.inc` for direct fixture coverage.

Fifteen new cases reject the original implementation. Thirty-one event checks
now pass: READY/STOPPING and duplicate states, focus changes, begin/end failures,
foreign sessions, exit/loss propagation, profile/reference-space scope and
STOPPING-to-READY restart with a prepared frame. The restart check executes
production preparation and presentation on the real frame worker and requires
a new wait/begin for the restarted run. All 115 flat/preparation/event scenarios
pass. The full focused build and 48-check raw verbose run pass in 39.58 seconds.
After tightening the failed-end ownership assertion, the rebuilt 115-scenario
target passes again in 0.06 seconds.

Event delivery, native session/frame results, input state and GPU dispatch are
simulated. These checks verify the production control flow and cache lifetime;
they do not verify headset event delivery, compositor stop/restart or acquired
image recovery during native session loss. Partial session/swapchain creation
and complete native shutdown remain to audit. No FPS gain is claimed.

### Prepared SteamVR Frames

After a successful Vulkan source acquire, `prepareSteamFrame` could throw from
its repeated-acquire log or failure diagnostic. The Vulkan entry points do not
catch that exception. Successful preparation had already published frame
ownership before logging; the fix preserves that ownership for presentation
instead of cancelling a valid frame because its diagnostic failed.

Repeated-acquire diagnostics now run within the existing preparation exception
guard, and the failure diagnostic has its own guard. Normal wait/begin counts,
the captured display time and retry behavior are unchanged. The production
function moved into `PreparedFrame.inc` so the existing flat-frame fixture can
exercise preparation followed by production presentation.

Six new cases reject the original implementation. Fourteen preparation cases
now pass, including normal/no-render frames, wait/begin/event failures, success
and failure diagnostics, and repeated acquires. They verify one wait/begin and
one presentation for each successfully prepared frame, with matching worker
thread and predicted display time. All seventy earlier presentation scenarios
also pass. The six-check focused run passes in 0.39 seconds; the layer target
compiles with the production include.

GPU/XR dispatch and event delivery remain simulated. This closes the earlier
fixture's manually seeded preparation gap but does not verify actual session
events, headset scheduling or a linked game's Vulkan acquire call. No new
rendering wait or quantified FPS gain is added.

### Controller Action Ownership

Controller destruction called optional haptic and trigger shutdown before either
IPC stop request or input clearing. An exception there skipped mandatory cleanup
and escaped through `shutdownXRImpl` before GPU retirement. An exception during
action-space or action-set dispatch also interrupted outer teardown. Failed
creation logged before cleanup, so a diagnostic allocation failure retained
partial actions or active integrations.

Controller destruction now isolates the optional calls and each action-resource
cleanup attempt. It retains the existing session-end trigger command, IPC stop
requests and input/filter reset. Failed creation cleans up before guarded
diagnostics. Healthy initialization and shutdown retain their previous operations;
normal rendering adds no work. The production lifecycle methods moved into
`XrActionLifecycle.inc` for direct testing.

Eight scenarios reject the original implementation. All twenty fixed scenarios
pass: normal/partial shutdown, optional failures, each action-space cleanup
exception, action-set cleanup exception, path/action/space/attachment creation
failures, either integration start failure, startup/failure log exceptions and
successful creation of eleven actions and four spaces. Destruction scenarios
execute production `shutdownXRImpl` and verify input/IPC cleanup before its GPU
retirement and session destruction. Repeated cleanup does not retry retired
handles. The new target and seven related checks pass in 1.34 seconds, and the
layer compile target builds.

XR/Vulkan dispatch, hook installation, optional haptic methods and IPC state are
simulated. These checks verify cleanup attempts and control flow, not successful
native destruction after an unavailable procedure or physical controller output.
The separate IPC checks cover production worker restart/cancellation. Complete
integration unload, actual procedure-resolution failures and headset shutdown
remain unverified. No FPS gain is claimed.

### Shutdown Diagnostics

Shutdown caught a cancellation exception, then attempted unguarded diagnostics
before stopping controller actions or retiring GPU work. Throwing diagnostics
could abort cleanup. The final `XR_SHUTDOWN` log could also escape before clearing
the device/procedure pointers used by runtime dispatch, interrupting the outer
`vkDestroyDevice` call.

Cancellation diagnostics now have an exception guard. Shutdown clears runtime
dispatch before guarded final logging and retains its prior checked device-idle
wait and destruction order. `shutdownXRImpl` moved into an include for direct
production-function testing.

The two original throwing-log scenarios fail. Eight fixed scenarios cover
normal and partial handles, cancellation failure, both diagnostic failures,
device-loss teardown, unrelated/unbound device isolation and repeated shutdown.
The new check plus lifetime/retirement checks pass in 1.22 seconds. The full
focused build and 47-check raw verbose run pass in 35.60 seconds.

The fixture models cancellation, controller actions, GPU completion and resource
destructors. It verifies the production shutdown function's ordering and handle
state, not native compositor shutdown. Native procedure-resolution failures,
actual partial session creation, and teardown with a headset remain
to verify. This change adds no per-frame work or FPS gain.

### Stereo Presentation Recovery

The stereo-present error handler retired submitted GPU work, then logged before
discarding unsubmitted FSR state, releasing eligible eye images, cancelling the
frame or publishing failure. A diagnostic exception bypassed those later steps
and prevented the caller from receiving the consumed binary-wait result. The
caller uses that result to avoid waiting twice on the same semaphore.

The handler now publishes failure first and preserves the checked GPU retirement
gate. It completes FSR/hand cleanup, attempts each eligible eye release and
frame cancellation, then guards its diagnostic. Each eye-release attempt has
its own exception guard so failed procedure resolution cannot skip the other
eye or frame cancellation. Healthy frames retain their submission and wait
counts. `presentStereoImpl` moved into an include for production-function tests.

Ninety-nine scenarios exercise conventional and Virtual Desktop early-release
schedules, SteamVR end dispatch on the production worker, projection/menu output,
invalid pairs, per-eye acquire/wait failures, reset/FSR/hand/submit/fence failures,
device loss, release/end/mirror errors, and throwing success/failure logs.
Forty-two scenarios fail on the original throwing-log handler. The fixed six-check
run passes in 1.08 seconds, including the new check, stereo begin, flat present,
native release, borrowed-image lifetime and GPU retirement. The layer compile
target also builds.

The fixture uses simulated GPU/XR dispatch and modeled FSR, hands and cancellation;
it exercises the actual presentation function, policy and borrowed-image lifetime.
The separate stereo-begin check covers production cancellation. Actual headset
composition, GPU errors, successful FSR/hand rendering in this call path and
procedure-resolution exceptions remain outside this fixture. Existing GPU-backed
renderer checks cover their components. No additional FPS gain is claimed.

### Flat and Prepared Frame Presentation

The flat-present handler logged before releasing waited images and cancelling
the native frame. Throwing diagnostics could escape without publishing failure
or returning whether binary present waits had already been consumed. SteamVR
error cancellation also left `steamFrameBegun` set, and a failed begin set that
flag despite no native frame being open. A prepared frame's display time was
assigned after its diagnostic, and a swapchain failure before taking the
prepared frame did not cancel it at all.

The handler now publishes failure first, retains the existing checked device
retirement for submitted work, attempts eligible image release and frame
cancellation separately, then guards error logging. Cancellation also takes an
unconsumed prepared frame and its display time. SteamVR's begun flag is set only
after successful begin and cleared after error-path end. Normal submissions,
waits and presentation ordering are unchanged. `presentQuadImpl` was moved into
an include so the fixture executes production logic.

Seventy scenarios cover SteamVR local/prepared frames and the non-Steam path,
with normal/no-render and swapchain/wait/begin/acquire/image-wait/recording-reset/
submission/fence/release/end failures. Each also runs with failure logging
throwing; prepared-consumption logging can throw independently. Original
behavior fails the escaped-exception and stranded-frame guards. Fixed checks
verify predicted display time, exactly-once frame end, no release of unwaited
images, retirement before release, consumed-wait reporting and SteamVR counters.
The final fixture uses the production `XrWorker`, verifying wait/begin/end stay
on its real worker thread; all seventy pass in 0.04 seconds. Both layer
translation units compile. An earlier six-check targeted run, before replacing
the inline worker fixture, passed in 1.29 seconds and included real GPU queue
bridge, stereo-begin, lifetime and retirement checks.

Vulkan/XR dispatch, session events, swapchain creation and pause layers are
simulated. Prepared state is seeded, not produced by `prepareSteamFrame`.
The real cross-queue bridge is verified separately, not inside this fixture.
Actual device loss, a runtime rejecting recovery release/end, compositor
behavior and full shutdown remain unverified. No FPS gain is claimed.

### Stereo Frame Begin

After successful `xrBeginFrame`, SteamVR diagnostics previously ran before
publishing `stereoPending.begun` and its predicted display time. A logging or
string-allocation exception could therefore leave a native frame untracked.
The exception handler also logged before cancellation; a second logging failure
could escape without either retiring the frame or marking the runtime failed.

Frame ownership is now published immediately after successful begin, before
diagnostics or action updates. On failure, the handler marks the runtime failed,
attempts cancellation, then guards its diagnostic against exceptions. Normal
wait/begin/end ordering and wait counts are unchanged. The two production
functions were moved into an include so the fixture executes their actual code.

Twenty Steam/non-Steam scenarios cover normal frames, no-render, invalid view
tracking, wait/begin/action/locate/end errors, begin-log failure, failure-log
failure, and both logs failing. Four scenarios reject the original behavior:
SteamVR begin logging abandons a begun frame, and failure logging escapes on
both runtime paths. After the fix, all twenty pass, verifying exactly-once
cancellation, display time and terminal/retryable state. The new check plus
`native_xr_release`, `game_image_lifetime`, and `gpu_retirement` pass in 1.03
seconds; the production layer compile target also builds.

XR dispatch and peripheral actions are simulated, and worker invocation is
inline in this fixture. It does not establish real worker/headset behavior,
flat/prepared frame recovery, session events or partial creation. This is an
exception-path stability fix, not an FPS optimization.

### IPC Cancellation Lifetime

Both IPC clients and both bridge servers previously requested `CancelIoEx`,
waited at most 50 ms, then released the operation's event and stack storage.
Cancellation is only a request; Windows can still access the `OVERLAPPED` and
buffer after that interval. Six sites now use one checked completion-event drain
before cleanup. A failed completion wait fails fast rather than returning live
storage. A cancellation request that races completion is still drained. Connection
loops also stop after terminal wait failures rather than spinning while the parent
remains alive.

The same check is compiled against all four production translation units. Seven
scenarios cover synchronous success, pending success, immediate broken pipe,
timeout, stop/parent exit, wait failure and cancellation returning `ERROR_NOT_FOUND`.
Bridge variants cover reads and connects. The fixture delays cancellation
completion by 120 ms and detects any early return or event/pipe closure. All four
original implementations fail the lifetime guard; all four fixed targets pass.
Native Windows tests also cancel pending connects and reads, and drain an already
completed connect. The targeted run passes in 3.18 seconds; all four unmocked IPC
translation units compile.

These tests do not exercise complete bridge processes with physical devices,
nonresponsive kernel drivers, or failure of the completion-event wait itself.
Retaining storage can extend worker shutdown if cancellation is slow; no new wait
is added to a successful transfer or the rendering thread. No FPS gain is claimed.

### PSVR2 Bridge Worker Ownership

The bridge previously closed the worker handle after an unchecked two-second
wait, then released the shutdown event, parent handle and stack context. An
exception after worker creation bypassed shutdown/join entirely and unwound the
context and current-user security storage. Either path could leave the pipe
worker reading invalid storage. The worker now has a noncopyable scope owner
that requests shutdown and verifies thread exit before cleanup. Normal shutdown
explicitly stops it before closing its wait handles; exception unwinding stops
it before destroying the earlier-declared context and security object.

Three checks call the production bridge entry point with a replacement real
Windows thread that waits 2.2 seconds after shutdown, an injected loop exception,
and thread-creation failure. Normal and exceptional shutdown both fail the
original lifetime guard. Fixed checks pass in 4.48 seconds, with exactly one
worker-handle close and the existing return codes. The production PSVR2 bridge
translation unit compiles; its IPC connect/read check also passes in the
four-check targeted run (5.53 seconds).

The replacement worker and absent backend directory deliberately avoid touching
physical controllers. These checks establish entry-point shutdown ownership,
not full hardware delivery or actual pipe-worker exception recovery. Joining a
slow cancellation can extend shutdown; it does not add per-frame rendering work
or a claimed FPS gain.

### IPC Client Start/Stop Ownership

`ControllerActions::create` and `destroy` start and asynchronously stop both
clients across XR session lifetimes. Previously, final worker cleanup published
`workerStarted=false` before retiring its event, allowing a new start to overlap
old cleanup. A stop caller could also load an event and then signal it after the
worker closed it. A start while the previous worker was still stopping simply
returned, losing that session's restart.

Start, stop signaling and final handle retirement now share one lifetime mutex
per client. Workers retain their thread handles until cleanup. A start that finds
a stopping worker duplicates its handle under the mutex, releases the mutex,
waits for verified exit, then retries startup. The duplicate prevents cleanup
from invalidating the wait handle. Configuration and event setup finish before
publishing started state, so an early C++ exception cannot leave a nonexistent
worker marked active. Event/thread creation failures preserve their prior
optional-integration behavior.

Six production-call-site schedules reproduce the original stop-signal,
retirement/restart and pre-retirement rapid-restart races across both clients.
The fixture uses real Windows events and actual client worker threads, pausing
specific API boundaries; pipe discovery is deliberately unavailable. Six more
checks cover event creation failure, thread creation failure and an injected
configuration-read exception. All twelve lifecycle checks plus the two existing
client cancellation checks pass in 1.98 seconds. Both unmocked client translation
units compile.

Normal rumble/trigger submission remains atomic-only and unchanged. A session
restart may wait for an already-stopping worker, including slow cancellation;
the wait does not hold the lifetime mutex and is not added to normal frames.
No FPS gain is claimed. Complete DLL unload, resource-exhaustion failure of
`DuplicateHandle`, real controller delivery and XR session recreation with
hardware remain unverified.

### Launcher Runtime Probe

`processOutput` originally blocked in `ReadFile` until the child closed its output,
then applied a 15-second process wait. A hung OpenXR probe could therefore freeze
the launcher indefinitely before that timeout was reached. The collector now
reads only available pipe bytes and observes one deadline across reading and
process exit. It bounds captured output to 1 MiB of input bytes and stops only
the exact child process handle created by the launcher; termination failures are
reported rather than hidden. Normal exit drains remaining output without killing
the child. Pipe setup and process-start failures retain their actual Win32 error.
The launcher rejects start/capture failure before launching DOOM.

`launcher_probe_output` launches four hidden real children: normal output larger
than the pipe capacity with final metadata, a silent hang, a closed-pipe hang, and
output flooding. The test uses a short deadline and an independent 3-second child
watchdog. Restoring the original blocking read/wait order fails at the watchdog;
the fixed hang paths finish in about 203 ms and return the expected child exit.
The four-scenario check passed in 0.58 seconds. The launcher translation unit
compiles with its production Unicode definitions; an existing numeric-setting
`wchar_t` to `char` conversion warning remains. These checks exercise the actual
collector but not the complete GUI launch or a real hanging OpenXR runtime.
This setup-only stability change does not affect gameplay FPS.

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

Follow-up `523b2e2` closes an error-path ordering gap: the stereo and flat-copy
handlers constructed an allocating log message before reaching the retirement
gate. A host allocation failure there could bypass retirement and unwind borrowed
leases. Retirement now runs first; a subsequent logging exception cannot abandon
live GPU work. Normal-path work is unchanged. The layer compiles, and the existing
retirement/lifetime checks pass; actual XR entry-point logging OOM is not injected.

### Broader Shader and Negotiation Checks

Seven additional existing checks passed: `sfs_bindless_gpu`,
`sfs_fragment_bindless_gpu`, `sfs_sampling_gpu`, `water_robustness_gpu`,
`runtime_vulkan_dispatch`, `sfs_capability`, and `queue_bridge`. The targeted run,
including three retirement/lifetime checks, passed all ten in 3.00 seconds.
GPU checks verify divergent sampled descriptors, mono/stereo layer selection,
implicit/explicit LOD, gradients, integer fetch, scissor edges, shared-memory
barriers, and protected out-of-range UBO/SSBO/image reads without changing valid
reads. The queue bridge verifies changing pixels and binary-semaphore reuse
across transfer family 5 and graphics family 0.

Negotiation checks exercise vendor-neutral limits, immutable multiview feature
chains, API-version/extension alternatives, and downstream/runtime device command
routing. These are controlled fixtures, not proof of every captured Eternal
shader, other GPU vendors, a linked game-layer launch, or headset composition.
No new runtime optimization is justified by this passing coverage alone.

### Pause HUD Texture Upload

The setup-only static texture upload freed staging and released a waited XR image
after an unsuccessful queue-idle wait. It now uses the shared retirement gate:
success permits image release, device loss tears down the unavailable texture
without releasing an image as completed, and other wait errors fail fast before
resource release. No extra wait, upload or decode is added to normal frames.

The expanded `gpu_transfer_retirement` check includes the production
`PauseBindings.inc` and decodes the checked-in PNG through WIC. Five additional
bounded hidden children cover normal upload and reuse, unverified retirement,
device loss, submission rejection and mapping failure. Live-resource and XR image
guards verify staging cleanup, availability, wait counts, no second upload on
reuse and exactly-once swapchain destruction. Restoring the original unchecked
wait hits the unsafe-release guard (`0x56`); the fixed path produces the expected
fail-fast status (`0xc0000602`). All eleven readback/upload scenarios passed.
Dispatch is simulated here; headset compositor and real device loss remain
unverified. `QuadRuntime.cpp` compiles with the production include.

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

The harness compiles both layer units, SFS, hands, FSR, launcher, IPC, camera capture and native HUD/camera sources. All 100 checks pass in a fresh 67.02-second run, including 102 hook-installation, 60 diagnostic and 58 clean-release camera, 100 HUD and 48 camera readback cases. Historical camera fixture repair evidence remains in the linked subsystem audit.
Raw checks cover ninety-four core, eighty-eight linked metadata, 321 linked pipeline creation and 240 linked allocation, sixty-eight mirror setup and 106 source creation; fifty-five outer swapchain, fifty-one outer frame, eleven diagnostics, thirty-five outer instance, sixty-eight runtime creation and sixty-two outer device scenarios. Coverage
includes queue concurrency, GPU-backed shader/rendering checks, GPU lifetime gates, readback/pause upload, launcher subprocesses, IPC cancellation and worker ownership,
controller action lifecycle, stereo begin/present, flat/prepared presentation, session events, partial session/swapchain creation and shutdown diagnostics, fourteen GPU-backed SFS setup checks, resource registry and multiview pixels. Frame/session fixtures cover 115 flat/preparation/event and fifty-eight setup/teardown scenarios.
Raw diffs and diagnostics were inspected; existing warnings concern `/DNDEBUG`/`/UNDEBUG` and a synthetic mirror handle. Previous package attempts lacked glslang optimizer libraries and the bHaptics SDK DLL; current full package and native validation remain pending.
A 120-300 second mirror-disabled combat capture must measure FPS, CPU/GPU bottlenecks and tail latency; synthetic timings are not a substitute.
Next: broader player/presentation/revenant/monkey-bar/DLSS hook rollback, persistent player state, calibration/input and remaining capture/shader paths. Native package/game/headset validation also remains; no completion is claimed.
