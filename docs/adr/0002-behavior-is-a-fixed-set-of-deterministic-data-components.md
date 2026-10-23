# Behavior is a fixed set of data components, run deterministically

Entity behavior (moving, following, spinning, orbiting, wandering) is a fixed, engine-provided set of **behaviors**: components with typed fields in scene files, configured and not programmed, so an AI adds behavior by writing text and the engine checks it like any other scene data. A scene may carry behaviors of its own in an optional top-level `components` object. The engine runs behaviors in the fixed-timestep update in a deterministic order, with a seeded random generator per scene and a snapshot of the input per step, so a scene, a seed and an input recording always give the same result on the same build and platform. There is no scripting language for now.

## Considered Options

- **An embedded scripting language (Lua is the likely candidate).** Deferred, not rejected: it is the most expressive option and models write it fluently, but a script is only fully checked when it runs, it needs a sandbox and instruction limits, and keeping it deterministic is extra work. The fixed set covers what the Sandbox does today, so the cost is not yet justified. Reconsider when a game needs logic the fixed set cannot express; behaviors, the registry and the determinism rules below are what a script API would build on.
- **A declarative rules engine (triggers and actions) now.** Deferred to its own decision: interactions and dialog are naturally data and are the next step after behaviors, but they need their own vocabulary and should not be designed in the same breath.
- **C++ only.** Rejected: behavior that needs a rebuild fails the plain-text, no-rebuild goal of ADR-0001.

## Consequences

- A behavior is registered like any component (name, fields, defaults, ranges, descriptions) plus an update function, so loading, writing, validation, the schema and `marle-scene` cover behaviors with no extra machinery. Adding a behavior means adding C++ and its documentation; the set grows deliberately.
- Behaviors write their results into ordinary components (a `Spin` changes `Transform.rotation`), so rendering and saving need nothing new. A scene saved mid-run holds the state it had.
- **Order is part of the file.** Scene-level components run first, then entities in file order, and within an entity behaviors in registration order. An entity that follows another must come after it in the file.
- A reference from one entity to another is a name. A reference to an entity that does not exist is a problem reported with its file and path, like a missing asset.
- **Determinism scope.** The same scene, seed and input recording give identical results on the same build and platform; floating-point results are not promised to match across platforms or compilers. To keep it so: the update step is always the fixed step (never wall-clock time), random numbers come only from the scene's generator (our own algorithm, not the standard library's distributions) seeded from the scene file, behavior code never depends on iteration over unordered containers or on pointer values, and input reaches behaviors only as a per-step snapshot that a recording can replace. Updating a scene needs no window or GL context, so it can be run and checked headless.
- Rendering interpolates between the previous and current step (as the Sandbox does today); that previous state is runtime-only and never written to a scene file.
- Hot reload keeps simulation state as in M5; a recording is tied to the scene it was made with and is not valid across a reload.
