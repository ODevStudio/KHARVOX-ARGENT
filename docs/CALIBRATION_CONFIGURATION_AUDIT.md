# Calibration Configuration and Persistence Audit

Reviewed 2026-10-04. All changes remain local; no game installation was changed.

| Commit | Subsystem | Category | Change |
| --- | --- | --- | --- |
| `31b6122` | Weapon configuration publication | Stability | Reject missing string values and interrupted input before replacing the previous settings. |
| `486f435` | Support and weapon calibration persistence | Stability | Publish support presets only after successful replacement; reject unavailable paths and check final stream state after close. |
| `9ae6579` | Weapon configuration stream state | Stability | Reject already-exhausted input instead of publishing defaults. |
| `936dc41` | Controller configuration and support reload | Stability | Require complete native reads, replace support snapshots and isolate diagnostic failures after complete state publication. |
| `80c79ef` | Hand calibration save completion | Stability | Check final stream state after close before replacing the file or publishing the draft. |
| `bd22fd1` | Launcher INI persistence verification | Stability | Verify native zero-return flush semantics, disk contents and snapshot replacement; no runtime change. |

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

## Hand Calibration Save Completion

The hand writer checked stream success before close, unlike the corrected weapon
and support writers. A close-only failure could therefore replace the saved file,
publish the candidate map and clear pending edits while reporting success.
`saveCalibration` now closes before checking the final stream state. Replacement
and draft publication remain conditional on success, with no additional frame work.

The production method is included from `HandCalibrationSave.inc` in both the
renderer and a bounded fixture using the real `Draft<HandCalibration>` type.
The original method fails the close-only scenario; all eight final scenarios pass:
initial save, round trip, write/flush/close faults, real locked-destination and
temporary-open failures, and retry. Rejected saves preserve destination bytes,
committed profiles and pending edits. Successful saves preserve unrelated profiles.
Stream faults are controlled status injection, not actual disk failure.

The unchanged public renderer methods move to `HandRendererRuntime.inc` to keep
the touched renderer below the file-size limit: 681 lines in the main file and
304 in the include. Their content matches the original after newline normalization.
Both eye-layout GPU checks, retirement, framebuffer and weapon-calibration checks
pass: six focused checks in 19.92 seconds. Both renderer users and layer units
compile. Full keyboard polling and the public configure/apply acknowledgement
through a native session are not exercised by this save fixture.

## Launcher INI Flush Semantics

Microsoft's `WritePrivateProfileStringW` documentation explicitly permits a zero
return when flushing the cached INI. An all-null flush cannot be checked like an
ordinary key write. The isolated native check observes zero and last error 2
despite complete UTF-16 disk contents and successful atomic replacement/reload.
Unknown keys survive copying and updating the snapshot; a normal key write to an
unavailable parent correctly fails. Requiring a nonzero flush return would reject
successful saves, so the production launcher is deliberately unchanged.

Source: [Microsoft API return-value contract](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-writeprivateprofilestringw).
`launcher_profile_flush` passes in 0.08 seconds. It exercises Windows APIs with
isolated files, not the complete GUI save path or real disk/cache faults.
The old controls-file read, unknown-key preservation after failed input, and
broader launcher save/apply boundaries remain to inspect.

## Verification Limits

All eleven configuration boundaries, six support persistence, twenty reload and
eight hand-save cases pass. The final hand/calibration six-check run passes in
19.92 seconds; all 125 harness checks pass in 106.44 seconds after a full
incremental rebuild, including the native launcher flush contract check.
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
Hand/model calibration load validation, full hand polling, launcher controls-file
reads and broader save/apply completion remain pending. Native game/headset,
package and combined gameplay FPS validation remain unverified; no audit
completion or FPS improvement is claimed for these persistence changes.
