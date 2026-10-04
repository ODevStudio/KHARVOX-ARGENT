# Calibration Configuration and Persistence Audit

Reviewed 2026-10-04. All changes remain local; no game installation was changed.

| Commit | Subsystem | Category | Change |
| --- | --- | --- | --- |
| `31b6122` | Weapon configuration publication | Stability | Reject missing string values and interrupted input before replacing the previous settings. |
| `486f435` | Support and weapon calibration persistence | Stability | Publish support presets only after successful replacement; reject unavailable paths and check final stream state after close. |
| `9ae6579` | Weapon configuration stream state | Stability | Reject already-exhausted input instead of publishing defaults. |

## Configuration Publication

`readWeaponConfig` already constructed a local candidate, but four string fields
ignored extraction failure and the final publication ignored stream failure.
Missing values could silently reuse default fields; an I/O error after a complete
row could publish only the valid prefix. The first regression reproduces seven
failures in nine cases. Successful normal EOF remains required before publication.

An expanded check reproduces two further failures in eleven cases: streams
already carrying EOF, with or without failbit, were mistaken for valid empty
input and reset settings. The reader now requires a good initial stream state.
New empty input and a complete final row without a newline remain valid.
Rejected input retains the previous profile, calibration mode and handedness.

## Support Persistence

Capture previously changed `supportProfiles` before opening the temporary file
or replacing the saved file. A failed save therefore changed the next frame's
preset despite reporting failure. An unavailable configuration path also created
relative `.support` files instead of refusing the save.

The production capture now calls `saveSupportCalibration`, which writes a local
candidate, closes and checks the stream, then atomically replaces the destination
with the existing Windows API. Only successful replacement publishes the map;
the caller updates the current calibration only after that success. The weapon
pose writer likewise checks stream state after close rather than before it.
Copies and file operations occur only on a capture request, not every frame.
No new synchronization, dependency or average-FPS improvement is claimed.

The unchanged extracted capture implementation fails three of six scenarios.
The fixed helper passes all six using real temporary files and native Windows
replacement: initial save, locked-destination replacement failure, temporary
open failure, unavailable path, successful retry and complete round trip.
Failures preserve both committed map contents and destination bytes; unrelated
profiles survive a successful save. Relative-path checks run in an isolated
temporary directory. Close-only I/O failure is not fault-injected.

## Verification Limits

All eleven configuration boundaries and six support persistence cases pass.
The final three-check focused run passes in 0.10 seconds; all 123 harness checks
pass in 105.97 seconds after rebuilding affected production and test targets.
Full raw diffs and relevant unfiltered diagnostics were inspected. Existing build
warnings concern debug/assertion definitions and macro redefinitions.

The existing calibration test calls the same helper used by production capture.
`QuadRuntime.cpp` compiles with that call. The lifecycle fixture includes only
`XrActionLifecycle.inc`, not full action polling; it does not prove this capture
or configuration refresh through a native XR session.

The actual file refresh still uses `istreambuf_iterator`, loads support rows
directly into the live map and has unguarded diagnostics. Complete file-I/O
failure, reload/removal semantics and native polling remain to inspect.
Hand-renderer close-result handling and launcher INI flush-result handling also
remain pending. The existing 994-line hand renderer was not broadly restructured
for this bounded configuration/support change. Native game/headset, package and
combined gameplay FPS validation remain unverified; no audit completion is claimed.
