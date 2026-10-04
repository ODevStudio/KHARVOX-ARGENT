# Calibration Configuration and Persistence Audit

Reviewed 2026-10-04. All changes remain local; no game installation was changed.

| Commit | Subsystem | Category | Change |
| --- | --- | --- | --- |
| `31b6122` | Weapon configuration publication | Stability | Reject missing string values and interrupted input before replacing the previous settings. |
| `486f435` | Support and weapon calibration persistence | Stability | Publish support presets only after successful replacement; reject unavailable paths and check final stream state after close. |
| `9ae6579` | Weapon configuration stream state | Stability | Reject already-exhausted input instead of publishing defaults. |
| `936dc41` | Controller configuration and support reload | Stability | Require complete native reads, replace support snapshots and isolate diagnostic failures after complete state publication. |

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

## Native File Reload and Diagnostic Boundaries

The unchanged production refresh method fails eight of thirteen scenarios:
replaced/empty/deleted support files leave stale presets, malformed or invalid
rows publish an earlier valid prefix, and throwing diagnostics interrupt accepted
or rejected configuration updates. The method is now included from
`XrConfiguration.inc` in both the real controller and the existing calibration
fixture, which uses the actual mapping, gesture, smoothing and support types.

Configuration reads use checked Windows `CreateFileW`/`ReadFile` calls with a
standard scope-owned handle. A native read error is distinct from successful EOF
and leaves the output string unchanged, including after a complete valid prefix.
Readers permit file replacement but refuse an active writer, so an in-progress
direct edit cannot expose a valid partial configuration. Successful complete
support input replaces the full map; empty or deleted files remove overrides.
Other open/read failures and invalid support content retain the previous map.

Accepted controls publish settings, reset transient input state and publish the
complete presentation bit mask before caching the observed text and attempting
diagnostics. Invalid controls leave that state unchanged; caching their complete
text retains the existing rejection-log suppression. Optional refresh failures
cannot escape into the outer controller update handler and clear live input.
The existing one-second polling cadence is unchanged; no GPU wait or FPS gain
is added or claimed.

Twenty final cases pass: eighteen production-refresh scenarios and two direct
native-reader checks. They cover support replacement/removal, invalid prefixes,
missing/locked paths, accepted/rejected throwing logs, both cinematic bit modes,
unchanged settings and CRLF bytes, throttling, controls/support read faults after
an initial read, and a native active writer. Direct checks verify multi-buffer
byte-exact reads, prefix-error rejection and closed handles. The first adapted
fixture incorrectly compared LF cache bytes with text-mode-written CRLF files;
binary fixture writes repair that mismatch without changing production behavior.
Actual disk/controller/headset failures and host-allocation injection remain
outside these checks.

## Verification Limits

All eleven configuration boundaries, six support persistence and twenty reload
cases pass. The final five-check focused run passes in 0.19 seconds; all 123
harness checks pass in 107.17 seconds after rebuilding production and test targets.
Full raw diffs and relevant unfiltered diagnostics were inspected. Existing build
warnings concern debug/assertion definitions and macro redefinitions.

The existing calibration test calls the same helper used by production capture.
`QuadRuntime.cpp` compiles with that call. The lifecycle fixture includes only
`XrActionLifecycle.inc`, not full action polling; it does not prove this capture
or configuration refresh through a native XR session.

The configuration fixture now exercises the same production refresh method and
real temporary files, with controlled native read faults and optional logging.
It does not include the complete controller action update or execute a native
OpenXR session. Remaining action polling and other diagnostic boundaries still
need verification.
Hand-renderer close-result handling and launcher INI flush-result handling also
remain pending. The existing 994-line hand renderer was not broadly restructured
for this bounded configuration/support change. Native game/headset, package and
combined gameplay FPS validation remain unverified; no audit completion is claimed.
