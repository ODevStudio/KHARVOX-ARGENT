# Launcher Probe Fixture Audit

Test-only fix: `9ca477c` (Stability / verification).
Production `LauncherProbeOutput.h` is unchanged.

## Finding and Correction

The flood case required `reason=output-limit` while starting a 200 ms collection
deadline immediately after launching its writer. Startup and pipe scheduling
could instead produce a valid timeout before 1 MiB arrived. The earlier full run
failed its reason assertion without printing the actual reason. Thirty reruns
passed, so that historical failure's specific cause remains unconfirmed.

A new writer delayed by 300 ms reproduces the fixture design flaw: unchanged
collection stops after 203 ms with `reason=timeout` and exit code 2, but the test
requires an output-limit result. The production collector behaved correctly.

Flood tests now request a larger pipe and verify that more than the production
1 MiB cap is available before starting collection. Pipe size is a hint, not the
readiness check. Preparation has a separate bounded deadline and terminates the
child on failure before closing owned handles. Both flood cases require the exact
capped payload plus failure marker, child exit code 2 and the original collection
time bound. Normal output and silent/closed-pipe timeouts retain their checks.
Diagnostics print before assertions so any future failure exposes its reason.

## Verification

All five real subprocess scenarios pass in 0.91 seconds. Normal output retains
81,951 characters and child exit code 7. Silent and closed-pipe children stop at
about 203-204 ms with timeout markers. Both flood cases begin with 2,101,248 bytes
available and return the exact 1,048,618-character capped result in 0-15 ms.

The full suite passes all 78 checks in 44.41 seconds after this correction.
Full raw test diff and CTest diagnostics were inspected. The existing build
warning about `/DNDEBUG` versus `/UNDEBUG` remains. This test-only correction
does not change launcher timeout policy, its byte cap or gameplay performance.
