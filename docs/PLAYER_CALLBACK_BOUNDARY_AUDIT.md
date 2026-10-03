# Player Callback Boundary Audit

## Commits

| Commit | Category | Change |
| --- | --- | --- |
| `c3ce848` | Performance/Stability | Classify weapon names without heap allocation. |
| `6e096e4` | Stability | Preserve native callback results and restore temporary player state. |
| `cda8d14` | Stability | Preserve placement bookkeeping and nested visibility through cache failures. |
| `506e343` | Stability | Retain per-root masks across interleaved hands and invalidate observed native identity changes. |
| `c17eccb` | Stability | Honor the native attachment boolean result and share calibration/rest-pose presentation eligibility. |

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
