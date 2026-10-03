# DLSS Ownership And Recovery Audit

Subsystems: native DLSS hook installation, eye histories, and SFS cached views.
Category: Stability. All commits remain local.

| Commit | Target | Change |
| --- | --- | --- |
| `cc678f1` | Installation | Own partial setup, prepare three trampolines before activation, check target-specific activation and rollback, serialize retries, and contain diagnostics. |
| `c357c4d` | Callbacks and histories | Release an unpublished right history after failed registration, preserve native results through diagnostic failures, and use mono fallback after optional eye preparation exceptions. |
| `e7edfe7` | Cached eye views | Publish only successful, non-null native outputs; retain valid siblings and retry rejected creation. |
| `6e0b609` | Temporal recovery | Reset both eye histories after mono fallback or interrupted native evaluation. |

## Installation

The original installer applied MinHook's shared queued operations, ignored
queueing/rollback failures, and left trampoline slots pointing at removed hooks
after partial setup. It did not serialize concurrent installation. Installation
logging could terminate its `noexcept` boundary after successful activation.
Null module lookup also reached signature reads.

The installer now prepares all trampolines before activating its three targets.
Its scope owner removes only hooks created by that attempt, in reverse order,
then restores the previous trampoline slots. Failed removal produces
`0xc0000602`: continuing could leave an active hook with abandoned dependencies.
Successful hooks retain the process-lifetime policy. Other owners' queued hooks
remain untouched; matching installation retries reuse the committed setup.

The one-time fallback protects logging and AA-request writing independently.
Clean-release builds do not write the request. A successful installation retry
does not clear a previously recorded stereo failure.

## History Ownership And Callback Boundaries

After successful independent right-eye creation, map insertion could throw or
reject a duplicate left handle. The old callback leaked that right history.
Creation now checks registration and releases the unpublished right history on
failure, preserving the existing pair and native left output/result. Failed,
null, or aliasing native creation outputs do not establish independent ownership
and are not released by the adapter.

The shared right-history release path guards its diagnostic formatting/logging.
A non-successful right release records fallback and still permits the normal
left release. It does not retry uncertain cleanup or turn an NGX error into an
unconditional process crash. A failed release cannot prove resource disposal;
the fixture verifies call count, fallback, and native left progression only.

Optional SFS/eye preparation exceptions use the existing mono fallback. Native
create, release, and evaluate calls stay outside those handlers, so native
exceptions propagate without retry. Successful evaluation updates temporal
state despite logging/allocation failures. Timing reports snapshot and clear
their counters before formatting; timing-off diagnostics are also contained.
The existing mutex still covers parameter preparation and both eye evaluations.

## Cached Eye Views

Both `dlssEyeResources()` and `eyeAttachmentView()` passed cached handle slots
to `vkCreateImageView`. A driver failure could poison the slot; the next call
then treated that value as a ready view, and teardown could destroy it.

Both helpers now create into a local handle and publish only after success with
a non-null output. Failure leaves the cache slot empty. A successfully created
sibling stays available, and retry creates only the rejected eye. The DLSS and
attachment consumers continue sharing the cache. Original-view destruction
still retires the cached views through the existing lifetime path.

## Temporal Recovery

A previously synchronized pair retained `seen=true` after mono fallback or a
failed eye evaluation. Mono evaluation advances only the left history; a
successful left evaluation followed by right failure can also diverge histories.
The next eligible stereo evaluation could omit the reset.

Fallback now invalidates the pair's synchronization marker. Stereo evaluation
invalidates it before either native call and publishes synchronization only
after both succeed. This also covers native exceptions without catching them.
The next stereo evaluation resets both eyes; uninterrupted successful frames
retain the existing reset conditions and sample progression.

## Evidence

`dlss_hook_installation_tests` includes the production source and launches bounded
hidden children across Steam and Microsoft Store profiles. Both diagnostic and
clean-release variants pass 310 scenarios each: 60 installation, 222 initial
callback cases, and 28 temporal recovery cases. Callback cases cover timing
on/off, logging/formatting faults, failed/null/aliasing native outputs, duplicate
registration, release errors, optional resolution exceptions, native results,
caller storage, eye order, report-window clearing, and native exceptions.

| Group | Comparison | Before | After |
| --- | --- | --- | --- |
| Installation | Original source versus `cc678f1` | 40 failures / 60 cases | 0 / 60 |
| Initial callbacks | Original source versus `c357c4d` | 140 failures / 222 cases | 0 / 222 |
| Temporal recovery | `c357c4d` callbacks versus `6e0b609` | 28 failures / 28 added cases | 0 / 28 |

The original DLSS snapshot is byte-identical to the pre-installation audit tree
at `a55825a`; SHA-256 is
`FBD981D2A3E33637A115DF8F12BD580FC89255FF4D79DDFBC292E824721BBDCF`.
The ignored local snapshot supports the optional baseline target; normal checks
compile the committed production source without that snapshot.

`SfsEyeViewChecks.inc` adds twelve GPU-backed cases to the existing graphics
fixture: two consumers, either eye failing, and host OOM/device OOM/null success.
Successful creations use real RTX 4090 views. The resolver injects rejected
outputs. Both original consumers failed the first poisoned-output retry when
tested first; the fixed cases verify retry counts, preserved left siblings,
shared warm reuse, non-null/distinct handles, and exactly-once destruction.
Existing real multiview pixels, per-eye ranges, and mismatched image/view refusal
continue passing. These checks do not execute native NGX.

## Verification And Limits

The full harness rebuild succeeds, including the production DLSS object and
linked SFS runtime. All 115 CTest checks pass in 94.90 seconds. The NGX ABI check,
both 310-case variants, graphics/performance/capture/screen-UI GPU checks, and
existing resource/lifetime checks pass. Raw diffs and relevant diagnostics were
inspected. The existing `/DNDEBUG` versus `/UNDEBUG` warning remains.

These fixes add no GPU waits or steady-state view allocations. No gameplay FPS
gain is claimed. Controlled MinHook/NGX dispatch does not prove safe rollback
with in-flight gameplay, actual NGX behavior, other drivers, or headset output.
Full package linking and native game/headset validation remain pending, as do
the broader player-state, controller IAT, calibration/input, and capture audits.
