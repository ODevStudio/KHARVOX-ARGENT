# Camera Capture Retirement And Reset Audit

Runtime commits: `c5afcf8`, `a40a55c`.
Subsystem: Camera GPU readback, shared retirement diagnostics and reset observations.
Category: Stability.

## GPU Readback

The original camera/decal readback cleanup ignored a failed queue-idle result
after fence-wait failure, then destroyed resources referenced by pending GPU work.
Error and completion logging could throw before cleanup. A late dispatch-resolver
exception could interrupt cleanup, and failed native creation could leave poisoned
handles that cleanup treated as owned.

`CameraCapture::readback` now holds successful buffer, memory, pool and fence
outputs in a local resource guard. The guard also owns the mapping and saved
cleanup procedures. It retires submitted work before unmapping or destruction,
using the existing gate: success and device loss permit cleanup; another result
fails fast before releasing potentially live resources. Failure before verified
GPU completion exports no pixels, including on device loss.

The function serializes recording, submission and cleanup with the device's
recursive queue mutex. Its production caller already holds that same mutex.
Resource cleanup runs before guarded error logging; non-standard diagnostic
exceptions cannot escape the optional readback call. The shared retirement gate
now guards its own diagnostic formatting/logging so a logging exception cannot
replace the required fail-fast path.

The follow-up guards each per-buffer report log. A throwing report can no longer
drop later buffer exports after successful GPU completion. File-write errors keep
their existing failure handling.

Empty request lists retain the existing early return. Successful requested
readback still uses one submission, one fence wait and no queue-idle wait.
Error recovery uses the existing fallback queue wait. No extra wait or capture
work enters ordinary frames, and no gameplay FPS improvement is claimed.

## Reset Observations

The outer command-buffer and command-pool reset hooks invoked camera callbacks
even when the driver returned an error. Render tracing already required native
success. Both hooks now apply the same success condition to camera bookkeeping,
preserving the returned Vulkan result and forwarded reset arguments.

This keeps camera observation aligned with the native operation. It does not
claim that a driver preserves executable command state after an allocation error
or that a lost device can resume rendering.

## Evidence

`camera_readback_retirement_tests` links the production `CameraCapture.cpp`.
Forty-eight bounded hidden child scenarios cover both ordinary and probe request
queues. Fake native handles and live-resource guards detect invalid destruction,
mapping before completion, escaped exceptions and unreleased resources.

Before the fix, **22 of 48 cases fail**. Unverified retirement hits the unsafe
destruction guard (`0x56`); throwing diagnostics and late resolution strand
ownership or escape. Poisoned buffer/memory/pool/fence failures also hit the guard.
The fixed check passes **48 of 48**, including creation/recording/submission/map
errors, loader failure, host OOM, output failure, device loss and throwing logs.
Unverified retirement uses the required fail-fast result (`0xc0000602`) even when
logging throws. Normal and throwing report-log cases verify both copied byte
ranges and unchanged wait counts.

`camera_reset_hooks_tests` includes the production `RenderTraceHooks.inc` with
controlled state, dispatch and observer callbacks. The original hooks fail
**8 of 20** cases; the fixed hooks pass all twenty. Cases cover command and pool
resets, game/non-game devices, success, host/device OOM, initialization failure
and device loss. The existing `camera_recording` check also passes real camera
metadata, dynamic offsets, mapped constants and parallel recording behavior.

## Verification And Limits

The full harness rebuild passes. Its 91-check run passed ninety checks in
50.45 seconds and failed one normal probe fixture scenario. PID-only temporary
directory names allowed reuse of older outputs. The fixture now creates each
directory exclusively with a PID and monotonic timestamp. After that test-only
repair, `ctest --rerun-failed -V` passes all forty-eight readback scenarios in
1.19 seconds. Runtime source remains unchanged between those runs; the ninety
passing checks include reset hooks, camera recording and shared retirement users.
The final logging follow-up passes the forty-eight scenarios with stronger
assertions that both files retain their bytes despite report-log exceptions.
That targeted run also takes 1.19 seconds.
Raw diffs and build/test diagnostics were inspected; sources remain below 800 lines.

Readback dispatch, native resource lifetime and device loss are simulated here.
The reset fixture verifies callback gating, not the full loader/device registry.
These checks do not prove real camera GPU readback, game projection discovery,
headset composition or gameplay performance. Broader capture handoff, HUD/input,
game-hook state restoration and native package/game/headset validation remain.
