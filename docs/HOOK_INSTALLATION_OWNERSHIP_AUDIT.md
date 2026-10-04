# Hook Installation Ownership Audit

Runtime commit: `14cbfa9`.
Subsystem: Camera activation and render-extent hook/IAT ownership.
Category: Stability.

## Changes

The camera installer could activate its required detour, then return false after
an optional status log threw. It also left a newly created discovery hook behind
when activation failed. Guarded diagnostics now preserve the successful core
result and allow later optional installation. Failed FOV/discovery activation
removes only a hook created by that attempt. Foreign creation conflicts retain
their hooks. Store-profile diagnostic failure no longer interrupts successful
contract validation. The executable hash and code-signature gates remain intact.

Render-extent installation could leave live accessors and a patched
`GetClientRect` import after a refresh or diagnostic exception. It ignored failure
to restore the import page's original protection. A stack owner now records
successful hook creations, import ownership and temporary write access. It rolls
back incomplete setup and resets unpublished extent state. If refresh throws,
it restores output fields that still contain its written dimensions while
preserving native changes to those fields.

Installation snapshots the original import and publishes its native callback
before compare-exchanging the detour. A concurrent foreign replacement rejects
setup without overwriting the replacement. Rollback compare-exchanges only our
detour, leaving a later foreign owner intact. Original page protection must
return before hook activation. A one-shot restoration failure undoes the patch
and retries restoration during cleanup; failure to restore protection or remove
an owned hook fails fast instead of continuing with partial ownership.

Both installers share a startup mutex. Concurrent callers wait for the first
attempt and observe its committed state rather than creating overlapping hooks
or clearing another caller's dimensions. Extent setup rejects null/recursive
imports and dimensions outside Win32's positive `LONG` range. Matching repeated
requests reuse live installation; mismatched dimensions return false.

`RenderExtentHooks.inc` holds the existing extent detours and their setup owner,
keeping the camera source below the repository's file-size limit. Successful
hooks retain their existing process-lifetime policy. We do not disable unrelated
hooks, uninitialize MinHook or remove live trampolines during `stop()`.

## Evidence

`camera_hook_installation_tests` includes the production camera implementation.
Bounded hidden children supply synthetic Steam/Store PE images, controlled
MinHook results and callbacks, a controlled Steam digest and real Windows memory
protection/pointer operations. Store cases retain the production contract checks.
The fixture does not load the approved game image or install real extent hooks.

The final **102-case** fixture reports **42 failures** against the pre-stage
`ca75b06` source snapshot and **zero failures** against the fix. An earlier
78-case fixture failed 22 cases. Strengthening adds rollback failures, concurrent
installation and native output-field restoration rather than weakening checks.

Checks cover required creation conflicts and activation failures, initialization
and pinning refusal, optional FOV/discovery cleanup, throwing logs and formatting
allocation, invalid signatures/hash results, import replacement races, original
read-only page protection, failed refresh, external output-field changes and
clean retry. Separate child schedules block the first creation and launch a
second installer, verifying one set of hooks and successful results for both.
Six fatal schedules per build profile verify the expected `0xc0000602` status
when rollback cannot restore protection or remove an owned hook.

Successful extent cases preserve the original import chain, virtualize the
audited swapchain caller and retain real dimensions for an unaudited caller.
These checks substitute the return address; native game call sites remain to
verify. MinHook's checked-in `MH_RemoveHook` disables an enabled owned hook before
freeing its trampoline; the new rollback uses that operation for each owned target.

## Verification And Limits

The full harness rebuild succeeds, including the separate non-testing production
camera object. All **100 CTest checks pass in 67.02 seconds**, including 102
installation cases, 60 diagnostic and 58 clean-release camera callback cases,
the existing real MinHook camera trampoline and Steam/Store render controls.
Raw diffs and diagnostics were inspected. The existing `/DNDEBUG` and `/UNDEBUG`
warning remains. The installation mutex and rollback bookkeeping add no GPU wait
or normal-frame work; no FPS gain is claimed.

The installed Steam executable at
`C:\steamlibrary\steamapps\common\DOOMEternal\DOOMEternalx64vk.exe` is 77,207,984
bytes. Manifest build `25216728` has SHA-256
`69dc13e88d1c19133ead7950dc64ebcbd4a5a3f6bd6f9c336ebffe56df6a1c11`, matching
the production Steam allowlist. This establishes binary identity, not native
execution of the new hooks.

The fixtures do not establish MinHook rollback during concurrent gameplay,
arbitrary third-party IAT cooperation, native refresh recovery beyond the owned
output fields, linked optional player/presentation/revenant installers, package
linking or headset composition. Broader hook ownership, persistent state,
calibration/input and native gameplay lifecycle remain to audit.
