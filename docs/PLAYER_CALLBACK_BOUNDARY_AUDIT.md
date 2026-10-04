# Player Callback Boundary Audit

## Commits

| Commit | Category | Change |
| --- | --- | --- |
| `c3ce848` | Performance/Stability | Classify weapon names without heap allocation. |
| `6e096e4` | Stability | Preserve native callback results and restore temporary player state. |
| `cda8d14` | Stability | Preserve placement bookkeeping and nested visibility through cache failures. |
| `506e343` | Stability | Retain per-root masks across interleaved hands and invalidate observed native identity changes. |
| `c17eccb` | Stability | Honor the native attachment boolean result and share calibration/rest-pose presentation eligibility. |
| `e570396` | Performance/Stability | Bulk-read bounded native names with page-edge fallback and reject incomplete identities. |

## Callback Findings

The crucible event observer could throw after the native call completed.
Haptic logging could interrupt weapon-kind and timestamp publication.
Visibility, equipment, laser and handoff diagnostics could interrupt completed
native updates. Meathook diagnostic exceptions could turn an applied pose into
a failed query result.

The callbacks now guard diagnostic formatting and logging without moving native
calls into those handlers. Haptic state publication precedes diagnostics.
Crucible event bookkeeping remains optional after the native result.
Swimming restores the six-float native basis on scope exit. Item animation
restores its thread-local item/hands context, and hands updates restore the
visibility-collection flag when the native callback throws.
Native exceptions still propagate.

## Checks

`tests/player_callback_boundary_tests.cpp` includes the production
`EternalPlayerHooks.cpp`. It reads owned native-layout objects through Windows
memory reads and substitutes controlled native callbacks and camera providers.
The fixture reserves an image and commits executable stubs as RX pages after
writing them; it does not leave writable executable pages.

All 112 bounded hidden-child cases pass across Steam/Store profiles and timing
on/off. Cases cover three crucible event types, swimming, animation, hands,
haptic publication, mesh-mask restoration, equipment transform/throw/fire and
meathook target/candidate queries. Enabled diagnostics exercise throwing logs
and formatting allocation failures. Native swimming, animation and hands
exceptions must escape with temporary state restored.

The allocation-free classifier passes eighteen cases under global heap
allocation rejection, including an input over 10,000 bytes. It retains ASCII
case folding, separator removal, substring matching and classification priority.
Native-sized names use a stack buffer; longer input uses a normalized matcher.
Existing PSVR2 trigger-policy and weapon-pose calibration checks also pass.

A clean Release harness rebuild and the full 119-check suite passed in
98.60 seconds. The harness compiles the production player translation unit and
executes the existing installation and assembly bridge checks.

## Performance

The classifier removes temporary normalized-string allocation from its callers.
An earlier isolated twelve-name benchmark measured 279.740 ns/call before and
206.257 ns/call after, with allocations falling from one to zero per call.
That roughly 73 ns saving does not establish a gameplay FPS improvement.
The callback restoration and diagnostic guards add no GPU waits.

## Remaining Scope

Interleaved and shared-root mask ownership now has controlled fixture coverage.
Root destruction and identical-identity address reuse still need native lifecycle
evidence. Attachment calibration and crucible rest-pose callbacks have controlled
coverage; hammer identity, native locators and precision-bolt branches remain.
Native game/headset callbacks, broader context changes and DLL unload remain
unverified. Controller state/rumble setup is recorded in
[the IAT ownership audit](XINPUT_IAT_OWNERSHIP_AUDIT.md); native coexistence remains pending.

## Persistent-State Follow-Up

The first expanded fixture reproduced twenty failures in 132 cases against
`6e096e4`: showing an untracked root allocated, mask reservation failure escaped,
nested hands updates replaced their caller's request list, and idle cache
allocation interrupted bookkeeping after applying a controller pose.

MSVC also allocates the map sentinel during construction. Root-mask storage now
constructs on the first hide inside an allocation handler. Reservation must
succeed before native mask modification. Showing an untracked root needs no
map, and completed restoration removes the saved entry. Native surface calls
remain outside allocation handlers, retaining native exception propagation.

Idle-map construction and insertion now share the cache handler. On failure,
an eligible tracked pose still publishes its placement timestamp and visibility
request. Inactive/authored contexts discard the root's cached idle pose without
creating a replacement entry. A later successful tracked call can cache it.

Hands updates now retain the enclosing collection flag, count and requests
through the entire nested callback, including native exceptions. Collection
covers that invocation's native update rather than its pre/post processing.

The final fixture passes 134 cases. Two additional quiet-mode cases reject
initial root-map construction and verify retry. Controlled skeleton/joint stubs
exercise real controller placement, idle cache retry, tracking loss and authored
animation invalidation. Nested callbacks cover success and inner native failure.
The production player object compiles. After an incremental rebuild following
the clean rebuild above, all 120 harness checks pass in 99.27 seconds, including
the existing player-mechanics policy check.

These changes add no GPU waits. Allocation avoidance has no quantified gameplay
FPS benefit; native player/root lifecycle verification remains pending.

## Root Visibility Ownership

The cache previously cleared every saved mask whenever the hands pointer changed.
Interleaved updates lost distinct roots' restore state; two hands using one root
could replace its original mask with the already-hidden zero mask.
The first five added scenarios failed in all four profile/timing combinations:
twenty failures in 154 production-source cases.

Masks now belong to the root rather than the last hands callback. Each entry
records its native vtable, render-entity pointer and render-model pointer using
guarded reads. A changed identity discards the old mask before any restoration;
an unreadable or rejected identity also drops that address's cached state.
Repeated hiding keeps the original mask, and successful restoration releases
the entry. Existing allocation-failure recovery remains covered.

All 154 cases pass, including distinct/shared interleaved roots, changed render
entities/models and an observed invalid identity followed by address reuse.
The production player source compiles; six focused player/caller checks pass in
7.81 seconds. The complete 123-check suite passes in 104.12 seconds.
Raw diffs and diagnostics were inspected. No FPS gain is claimed.

This is not native object-generation tracking. Reuse with the same address,
vtable and model pointers cannot be distinguished by these reads. Entries for
destroyed roots that never restore can remain until that address is observed
again; cleanup requires verified native lifecycle evidence rather than silently
discarding live restore ownership. Native destruction, complete cache retirement
and game/headset transitions remain unverified.

## Attachment Outcomes and Presentation

The hook declared the native locator helper's return as `uintptr_t` and applied
calibration without checking native success. A failed joint query could therefore
modify unproduced output and consume visual rest-pose state. Undefined upper
return-register bits could also make a native false value appear successful.

Read-only disassembly of the installed Steam image establishes the ABI:
RVA `0x1981bc0` calls `0x19819d0`; the latter tests `AL` at `0x1981a8b`, returns
immediately on failure and writes `AL=1` at `0x1981bb1` after successful output.
The attachment caller at `0x138a649` matches the hook's verified return address
`0x138a64e`. The hook and trampoline type now return `bool`. Native false returns
before observer reads, native state queries or output/cache changes. Native
exceptions still propagate.

Calibration previously checked pose/placement freshness but not current camera
ownership. A fresh preceding VR pose could alter an authored attachment after
gameplay ended or an animation took ownership. Calibration and rest-pose
replacement now share the existing native-camera, scripted/drone/monkey-bar,
context, primary-root, weapon-profile and pose/placement eligibility checks.
No native state, event, damage or parent-root transform is replaced.

The first attachment fixture used exact equality for computed float transforms.
Eight successful calibration cases were false failures; those computed outputs
now use a `0.0001` tolerance. The repaired baseline reproduces twelve real
failures in 218 cases: native false, false with nonzero upper register bits and
failed attachment after actual rest-pose capture. After the ABI/result fix,
six additional presentation scenarios reproduce 24 failures in 242 cases.

All 242 final cases pass across Steam/Store profiles and timing modes. The
fixture drives the production callback, settled idle capture, native swing
events and controller-relative rest replacement. It also checks changed context,
manual trigger, native exceptions, calibration diagnostic/formatting failures,
foreign caller/owner/item/root, stale pose/placement and profile mismatch.
Failure stubs emulate the native `AL` ABI without assuming cleared upper bits.
Six focused player/caller checks pass in 9.31 seconds; the complete 123-check
suite passes in 106.19 seconds. The production player source compiles, and raw
diffs and diagnostics were inspected.

There are no added GPU waits or claimed FPS gains. This fixture does not execute
the real native locator resolver, actual hammer RTTI or precision-bolt show and
render publication. Native disassembly evidence is from Steam, not a Store
installation. Full game/headset transitions and complete root-cache lifecycle
remain unverified.

## Native Weapon Identity Reads

The native name probe issued one Windows memory read for every byte through the
terminator. For the normal crucible name, the complete identity lookup used 31
reads. An unreadable, unterminated name exactly matching the crucible prefix
could also inherit the initialized buffer's zero byte and enable its profile.

The probe now attempts one bounded 255-byte read into its existing stack buffer.
A failed bulk read clears partial output and retains the guarded byte fallback,
so a valid terminator immediately before an unreadable page remains accepted.
A successful read without an observed terminator is rejected. Accepted names
retain zero-filled trailing bytes; rejected names are cleared before kind or
crucible selection. Generation, declaration-vtable and independent hammer RTTI
checks remain in place. No cache, heap allocation or GPU wait is added.

The expanded baseline reproduces twelve failures in 274 cases: unnecessary
byte reads, unterminated page-edge crucible selection and retained overlong
source bytes, each across both profiles and timing modes. All 274 final cases
pass, including real `PAGE_NOACCESS` boundaries, valid page-edge fallback,
unreadable/overlong names, mismatched generation/vtable and heap rejection.

An optional `identity-benchmark` mode in the existing callback fixture runs
2,000 warm-up lookups and five batches of 10,000 lookups through real Windows
memory reads on owned native-layout objects. A checksum preserves classification
work; the fixture counts memory calls. Sequential Release measurements:

| Path | Median (us/lookup) | Min | Max | Reads/lookup |
| --- | --- | --- | --- | --- |
| Original byte probe | 15.5774 | 15.3518 | 15.7414 | 31 |
| Bounded bulk probe | 5.90984 | 5.83664 | 6.03792 | 9 |

This isolated lookup saves about 9.7 microseconds, roughly 62% of its CPU time.
It is not a 62% gameplay improvement. Actual calls per frame, page-edge frequency,
clock state and the CPU/GPU bottleneck remain unmeasured; no combined FPS claim
follows. Seven focused checks pass in 10.02 seconds, the production player source
compiles, and all 123 harness checks pass in 107.06 seconds. Raw diffs, relevant
diagnostics and both benchmark outputs were inspected. Native gameplay and
concurrent game-object lifetime remain outside this fixture.

## Zoom and Precision Bolt Publication

Zoom preparation cleared cached hands/weapons but left the preceding publication
timestamp active after inactive setup, meathook exclusion or a declaration
fault. A blend fault after publication also left an active timestamp. Preparation
now clears the timestamp both at entry and in the existing SEH rollback.

The shared zoom gate requires a nonzero fresh timestamp, a nonzero camera
context and no scripted movement or monkey-bar ownership alongside its existing
gameplay, synchronization, native-animation and drone gates. Ineligible blend
and mode callbacks retain their native arguments/results and dispatch counts.

Precision Bolt detection previously reused its published weapon after the
current primary handle changed or became invalid. It now reuses the established
generation/cached-generation, invalid-sentinel and cached-pointer checks before
calling the native declaration getter. Rejected handles never invoke that getter.
The selected native mode and exact terminated declaration name remain required.

The expanded baseline reproduces 48 failures in 362 cases across Steam/Store and
timing/quiet variants. All 362 fixed cases pass, covering failed/inactive
preparation, post-publication blend SEH, context eligibility, generation/pointer
changes and native declaration/mode/name refusal. Owned RX stubs exercise child
render publication only after native item animation, restored animation context,
diagnostic/formatting failures and propagated native publication exceptions.

Six focused checks pass in 13.81 seconds; the complete 123-check suite passes in
108.28 seconds. The fixture and production player source compile; raw diffs and
relevant diagnostics were inspected. This is stability work without a claimed
FPS improvement or additional GPU wait. Actual engine resolvers, render-world
publication, Store disassembly and complete root/game/headset lifecycle remain
unverified.

## Native Pointers, Hammer RTTI and Laser Locators

The common pointer reader ignored the read outcome and returned its output even
after failure or an incomplete result. Identity lookup also retained that output
for independent Hammer classification after a failed primary-pointer read.
The reader now returns null unless the complete read succeeds; identity lookup
reuses it without adding reads. Valid Hammer RTTI remains independent of the
weapon declaration name/vtable, and existing locator selection is unchanged.

The installed Steam executable is still 77,207,984 bytes at build `25216728` and
matches SHA-256 `69dc13e88d1c19133ead7950dc64ebcbd4a5a3f6bd6f9c336ebffe56df6a1c11`.
Read-only binary inspection finds the exact terminated Hammer name at RVA
`0x4214140`, type descriptor `0x4214130`, complete-object locator `0x357cc20`
and vtable `0x2e10910`. Locator fields are signature `1`, offsets `0/0`,
type `0x4214130`, hierarchy `0x357cc48` and self `0x357cc20`. This establishes
the existing image-relative RTTI layout rather than just guessing a class name.
Native `0x1981bc0` forwards root position/basis at `+0x158/+0x164` and mode to
`0x19819d0`; the latter checks `AL` and returns `AL=1` only after success.
No native code or game installation was changed.

The first fixture used `hammer` instead of the existing `sentinel_hammer`
profile key, causing eight false failures. The repaired baseline reproduces
sixteen real failures in 478 scenarios: poisoned failed/short pointer outputs
can select a Hammer profile or reach the native laser transform. Fault injection
performs a real read before reporting failure or a six-byte result; it tests
output rejection, not a claim that every Windows partial read writes this way.

All 478 cases pass across Steam/Store and timing/quiet variants. Owned read-only
RTTI pages cover valid detection, invalid signatures/self/type/name/vtable,
declaration independence and rejected generations/null handles. Locator cases
cover exact `_info`/`muzzle` selection past unrelated groups and `muzzle_light`,
bounded counts, missing models/definitions, hidden roots/items, zero masks,
native false/SEH, invalid position/basis and validated transform arguments.
Seven focused checks pass in 13.71 seconds; all 123 checks pass in 110.06 seconds.
The fixture and production player source compile; raw diffs and diagnostics were
inspected. No cache, heap allocation, GPU wait or FPS gain is added or claimed.
Static Steam evidence and owned dispatch are not live game/Store/headset proof;
actual native model resolution and complete object lifetimes remain unverified.
