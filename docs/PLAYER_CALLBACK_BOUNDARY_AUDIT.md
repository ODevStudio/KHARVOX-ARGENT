# Player Callback Boundary Audit

## Commits

| Commit | Category | Change |
| --- | --- | --- |
| `c3ce848` | Performance/Stability | Classify weapon names without heap allocation. |
| `6e096e4` | Stability | Preserve native callback results and restore temporary player state. |

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

Persistent root-mask and idle-pose maps still need allocation/lifetime review.
Restoring the collection flag does not prove nested hands updates preserve the
pending visibility requests. The current fixture does not comprehensively
execute attachment/rest-pose or precision-bolt branches.
Native game/headset callbacks, context changes and DLL unload remain unverified.
