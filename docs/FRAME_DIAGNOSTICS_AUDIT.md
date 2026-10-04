# Frame Diagnostics Failure Audit

Runtime fix: `cd82f29` (Stability). Reviewed `FrameTiming.h` and
`PresentAnalysis.h` while tracing outer acquisition and presentation.

## Findings and Fix

The original `FrameTiming` destructor let allocation/logging exceptions escape
its implicit `noexcept` boundary. Two injected failures terminated the process
with `0xc0000409`. It now contains reporting exceptions and resets its window
before formatting. It preserves an exception already unwinding through the scope.

The timeline destructor caught report allocation failures after filling its
256-record buffer, but retained `count=256`. The next append would index beyond
the array. The original fixture reproduced that state at allocation boundary 10;
a timed-report failure also retained its pending record. Reporting now snapshots
the count and clears the window before formatting while holding the same mutex.
The next frame can append to the empty buffer even after report failure.

Stream growth can set `badbit` instead of throwing. Four strengthened checks
reproduced incomplete timeline/CPU output after the initial reset fix. Reporting
now rejects failed streams rather than emitting partial measurement lines.
Normal cadence and log fields remain unchanged. Failed optional samples can drop.

## Verification

Eleven focused checks pass in 0.22 seconds: existing on/off coverage plus nine
failure/recovery modes. Timeline checks inject each measured allocation boundary
for timed/full windows, with performance fields off/on: 9, 14, 11 and 23 cases.
They verify reset, next-frame progress, reporting cadence and complete output.
Frame checks cover log failure, formatting OOM and exception unwinding. Disabled
diagnostics perform no report allocations, logging or history updates.

The focused harness rebuilds the layer. The final full run passes 77 of 78 checks
in 44.96 seconds; `launcher_probe_output` fails its flood output-limit assertion.
A 200 ms deadline race is suspected but unconfirmed at this checkpoint. Raw diffs and
diagnostics were inspected; the existing `/DNDEBUG` versus `/UNDEBUG` warning
remains. No gameplay FPS gain or native game/headset verification is claimed.
