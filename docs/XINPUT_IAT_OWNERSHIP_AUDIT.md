# XInput IAT Ownership Audit

## Commit

| Commit | Subsystem | Category | Change |
| --- | --- | --- | --- |
| `b3efecc` | Controller state and rumble IAT hooks | Stability | Bound PE reads, serialize setup, pin callbacks and verify owned patch/protection recovery. |

## Findings

Both installers dereferenced import metadata without image bounds checks.
The state installer used an unsynchronized success flag. The rumble installer
treated a native function pointer as proof of successful installation and did
not pin its callback module.

Both installers could overwrite a competing hook between discovery and patching.
They also ignored failure to restore the IAT page's original protection.
The first controlled fixture reproduced 159 failures in 210 scenarios, including
access violations, foreign-hook replacement, concurrent setup and writable pages
after reported success.

## Changes

`XInputIatHook.h` provides the common implementation for the two existing hooks.
It retains resolved-export matching, ordinal and bound imports, and the three
supported XInput DLL names with case-insensitive matching.

Reads check integer ranges, committed memory and matching allocation ownership
through `VirtualQuery` and `ReadProcessMemory`. Header checks cover PE64 machine,
optional-header size/magic, directory availability and declared image/header
bounds. Import descriptors, names and matching DLL thunks must terminate inside
their bounds before installation can modify an IAT slot.

One setup mutex serializes both exports, including writes on their shared page.
Each installer pins its callback module, publishes atomic native dispatch before
exposing the callback, and compare-exchanges only the expected export address.
A prepared native pointer alone no longer reports installation success.

Failed protection restoration triggers owned rollback and another restoration
attempt. Rollback leaves a competing pointer untouched. An unrecoverable
restoration failure fails fast instead of continuing with a writable IAT page.
Native dispatch remains valid for callbacks that entered during a failed
attempt. Successful hooks remain process-lifetime resources.

Matching retries verify the main module and still-owned slot without another
patch or pin. Replaced hooks report refusal and preserve the foreign owner.

## Checks

`tests/xinput_hook_installation_tests.cpp` includes both production headers.
All 258 final bounded hidden-child cases pass: two exports, three DLL names and
43 scenarios. Coverage includes invalid/unreadable metadata, missing exports,
foreign ownership before/during/after setup, rollback, retry, module identity,
callback dispatch and terminal protection failure.

Eight-thread cases exercise matching retries and concurrent state/rumble setup
on one IAT page. A worker invokes the callback during failed protection
restoration; callback delivery also succeeds after rollback. Normal scenarios
use real Windows allocation, memory reads and page protection. Failure cases
substitute selected API results. Concurrency cases emulate protection calls to
check setup counts without letting the pre-fix fixture race real protection.
Terminal restoration cases expect `0xc0000602`.

The incremental Release rebuild recompiles affected production player, camera,
HUD, runtime, DLSS and revenant sources. All 123 harness checks pass in
105.58 seconds, including existing controller-input and XInput haptic policies.
Raw diffs and relevant diagnostics were inspected.

## Installed Steam Image

The current `C:\steamlibrary\steamapps\common\DOOMEternal\DOOMEternalx64vk.exe`
is 77,207,984 bytes. Manifest build `25216728` still matches the approved SHA-256:
`69dc13e88d1c19133ead7950dc64ebcbd4a5a3f6bd6f9c336ebffe56df6a1c11`.

Native `dumpbin` reports PE32+ x64, a `0xf0` optional header, `0x400` header
extent and `0x7431000` image extent. Its import directory is at `0x3885900`
with size `0x21c`. `XINPUT1_3.dll` imports ordinals 2 and 3 through IAT
`0x2a1c8a0` in read-only `.rdata`. These fields fit the new validation policy.
No game launch or native IAT modification was performed.

## Limits

This setup-only change adds no GPU waits and has no claimed FPS gain.
Native game/headset input, real third-party hook coexistence, Store image
metadata and complete DLL lifecycle verification remain pending. Successful
callback pinning intentionally prevents unloading the module during process life.
