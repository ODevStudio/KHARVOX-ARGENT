# Player Callback Boundary Audit

## Commits

| Commit | Category | Change |
| --- | --- | --- |
| `c3ce848` | Performance/Stability | Classify weapon names without heap allocation. |
| `6e096e4` | Stability | Preserve native callback results and restore temporary player state. |
| `cda8d14` | Stability | Preserve placement bookkeeping and nested visibility through cache failures. |

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

Root-mask ownership across interleaved hands, root destruction and pointer reuse
still needs native lifecycle evidence. The current fixture does not
comprehensively execute attachment/rest-pose or precision-bolt branches.
Native game/headset callbacks, broader context changes and DLL unload remain
unverified. Controller state/rumble IAT ownership is the next audit target.

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
