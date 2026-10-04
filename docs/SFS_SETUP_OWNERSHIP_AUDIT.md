# SFS Setup Ownership Audit

Commit: `01c0b4c`. Subsystem: SFS initialization/registration. Category: Stability.

## Finding

`NativeSfs.cpp::initialize` published its state before its final diagnostic.
When that log threw once, initialization returned false and freed the uniform
buffer and memory, but the registry still exposed wrappers backed by that state.
The RTX 4090 regression observed `registered=1` with no live uniform allocation
and a retained query pipeline. A second throwing diagnostic could instead escape
and prevent uniform cleanup. The initial state allocation was outside the handler.

## Change

Optional capture and final setup diagnostics are guarded without changing the
successful result or registered resources. State allocation is inside the setup
handler. Failed setup unmaps and frees uniform resources, drops the unpublished
state and its query pipeline, then attempts guarded failure logging. No new
per-frame work or synchronization is added; no FPS gain is claimed.

## Verification

`sfs_initialization_gpu_tests.cpp` links the compiled production SFS runtime,
creates real Vulkan objects on RTX 4090, and intercepts selected dispatch calls
to inject failures and count ownership. Twelve of fourteen scenarios fail on
the original implementation; all fourteen pass after the fix:

- Normal setup and disabled-probe passthrough.
- One-shot/repeated final diagnostic failure, including the mono branch.
- Optional capture setup diagnostic failure.
- Uniform allocation, binding, mapping and coherent-memory-selection failures.
- Partial query-pipeline setup failure and dispatch-resolution exceptions.
- Host allocation failures at state construction and registry insertion.

Failure cases check that resources are retired before the throwing error log,
no failed state is registered, and a clean retry succeeds on the same device.
Shutdown checks matched buffer/memory/map counts and no remaining query resources.
The raw verbose fourteen-check run passed in 5.12 seconds. After strengthening
the failure-log ordering assertion and rebuilding all focused targets, the full
sixty-six-check suite passed in 42.24 seconds. The full raw diff and build
diagnostics were inspected; only the existing NDEBUG override warning appeared.

## Limits

Failures are injected; this does not simulate actual driver memory pressure or
device loss. Outer layer device registration/rollback, missing mandatory dispatch,
linked game/headset startup and broader shutdown remain to audit. Package linking
and gameplay FPS remain unverified.
