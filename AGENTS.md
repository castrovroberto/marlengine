# AGENTS.md

Read this first if you are an AI agent working in this repository. It says what you may change, how to check your work and where to look next. `CLAUDE.md` has the deep engine reference (architecture, every component, every API); this file does not repeat it.

Marlengine is a C++17 2D engine whose worlds are plain-text scene files. Scene files, the component registry and the deterministic simulation are the parts an agent works with most.

## What you may edit

| Area | Default |
| --- | --- |
| `Assets/Scenes/*.json` (scenes) | Edit freely, then run the verification loop. |
| `docs/*.md`, `GLOSSARY.md`, `README.md` | Edit freely. Keep them true: if you change behavior, change the doc in the same commit. |
| `Tests/**` (test code and fixtures) | Edit freely, but never edit a golden by hand (see "Golden files"). |
| `Marle/**`, `Sandbox/**`, `tools/**` (C++) | Ask first, unless the task is explicitly an engine change. |
| `docs/adr/*` | Ask first. ADRs record decisions; do not rewrite or add one without being asked. |
| `premake5.lua`, `r.sh` | Ask first. |
| `Marle/vendor/**`, `Tests/vendor/**`, `vendor/**` | Never edit. These are vendored dependencies and a tracked binary. |
| `Assets/Scenes/scene.schema.json`, `Tests/SceneTests/fixtures/{canonical.json,*.recording.json}` | Generated. Only regenerate (below). |

Commit only when asked. A milestone gets one branch and one PR at its end, not one PR per issue.

## Verification loop for a scene change

Build once (`./r.sh`, or `make config=debug`), then from the repo root:

```
./bin/Debug-linux-x86_64/SceneTool/marle-scene validate Assets/Scenes
./bin/Debug-linux-x86_64/SceneTool/marle-scene simulate Assets/Scenes/world.json --steps 600
```

Before saving a risky edit, `marle-scene diff --base <file> --proposed <file|-> [--json]` checks it as a hot reload would (strictly) and shows what it changes and how a 600-step run differs; `-` reads the proposal from stdin; add `--carry-over` to see which of a running world's state the reload would keep or lose.

Add `--json` to either command for one JSON object on stdout instead of text (same exit codes; fields in `docs/scene-format.md`, "JSON output"). Prefer it when you parse the result.

1. `validate` prints every problem with a JSON Pointer and exits 1 if there is any. Fix them all; the loader skips the offending component or entity and hot reload refuses the whole file.
2. `simulate` runs the scene headless and prints what changed, which triggers touched what and the final checksum. It refuses a scene with problems.
   `simulate --report -` prints what happened as events (triggers entered and left, movers stopped by solids) so you can assert on behavior, not only on the checksum.
3. If a recording exists for the scene, check against it: `simulate <scene> --recording <file>` exits 1 if the checksum differs from the recording's. `--expect <checksum>` does the same for a known value.
4. A changed checksum is not automatically a bug. If you meant to change how the scene plays, regenerate the goldens (below) and say so; if you did not, you broke something.
5. After touching `world.json` or anything that changes how it plays, run `SceneTests` (below).

`marle-scene format --check <files>` tells you whether files are in canonical form; `format` rewrites them.

## Running the tests

Each test is its own executable, run from the repo root as `./bin/Debug-linux-x86_64/<Name>/<Name>`. They exit non-zero on failure. There is no way to run a single check and there is no linter.

| Test | Needs |
| --- | --- |
| `SceneTests`, `InputTests`, `LayerStackTests`, `LogTests` | Nothing (headless). `SceneTests` is the one that matters for scene and simulation work. |
| `Renderer2DTests`, `ApplicationTests`, `ImGuiLayerTests` | A display and OpenGL 3.3. Skip them on a machine without one and say that you did. |

`make` builds the tests together with everything else. Re-run Premake (`./r.sh`) after changing `premake5.lua` or adding or removing source files, because sources are globbed.

## Golden files

These files are generated and compared byte for byte:

- `Assets/Scenes/scene.schema.json`
- the component table in `docs/scene-format.md`, between its `components:begin` and `components:end` lines (never edit between them by hand)
- `Tests/SceneTests/fixtures/canonical.json` (the example scene without comments)
- `Tests/SceneTests/fixtures/world_run.recording.json` and `world_collide.recording.json` (the determinism goldens: scripted runs of `world.json` with their final checksums)

Regenerate after changing a component, `example.json`, or anything that changes how `world.json` plays (any edit to `world.json` changes its scene hash):

```
MARLE_UPDATE_GOLDEN=1 ./bin/Debug-linux-x86_64/SceneTests/SceneTests
```

Then run `SceneTests` again without the variable, and look at `git diff` on the goldens. Explain every changed golden in your summary. Do not regenerate just to make a failing test pass before you know why it failed.

## Footguns

- `marle-scene format` leaves a file that has comments alone; `--drop-comments` deletes them for good. Do not pass it on files whose comments matter (`example.json` and `world.json` have them).
- Scene files are strict: unknown fields, repeated JSON keys, a wrong type, an out-of-range value, a missing entity reference are all errors. Read the did-you-mean in the message.
- Asset paths are relative to the working directory and must stay inside it. Run tools and the Sandbox from the repo root.
- Behaviors must be deterministic: no clock, no unseeded randomness, no polling `Input`. Only the fixed step, the `InputFrame` and the scene's seeded generator. Nothing in the tests enforces this except the golden checksum, so it is a review item.
- Behavior order matters: on one entity behaviors run in registration order (`ClampPosition` last), and a camera that follows the player must be later in the file than the player.
- `Platform/macOS` is not compiled or tested on Linux. Windows has no backend.
- stdout is block-buffered when piped; use `stdbuf -oL` to see Sandbox logs live.
- The engine never throws or asserts. Misuse is logged, so read the log, not only the exit code.

## What do you want to do?

- **Add or change an entity in a world**: edit `Assets/Scenes/world.json`, run the loop above, regenerate the goldens if it changes how the world plays. Format: `docs/scene-format.md`; worked example: `Assets/Scenes/example.json`.
- **Check a scene file**: `marle-scene validate`, then `simulate`.
- **Find out what a component or field does**: `marle-scene describe <Component>` (add `--json` to parse it; no name lists them all), or `docs/scene-format.md`, or `Assets/Scenes/scene.schema.json` (each field has a description). Component registrations are in `Marle/src/Marle/Scene/` (`Components.h`, `ComponentRegistry.cpp` for `RegisterBuiltinComponents`, `BuiltinBehaviors.cpp`).
- **Add a behavior or component** (C++): ask first. Read ADR 0002 (`docs/adr/0002-...md`) and the "Scenes" section of `CLAUDE.md`. Register it in the component registry, make it a behavior with `ComponentBuilder::Behavior`, add a test in `Tests/SceneTests`, regenerate the goldens, document it in `docs/scene-format.md`.
- **Add a test**: put it in the matching `Tests/<Name>/src/` project; a new project is declared with `testProject` in `premake5.lua` (ask first). Test scenes and fixtures go in `Tests/SceneTests/fixtures/`.
- **Record or replay input**: `docs/input-recording.md`; the Sandbox takes `--record <file>` and `--replay <file> [--exit-when-done]`.
- **Change rendering, windowing or ImGui**: engine C++, ask first; read the Rendering and Layers sections of `CLAUDE.md`.
- **Add sound, dialog or interactions**: not built yet; see `docs/roadmap.md` for the milestone order and do not invent an API.
- **Understand a term**: `GLOSSARY.md`. **Understand a decision**: `docs/adr/`.

## Where things live

- `Marle/`: the engine (shared library). `Sandbox/`: demo client. `tools/marle-scene`: the scene tool. `Tests/`: the test projects.
- `Assets/`: scenes, shaders, fonts. `docs/`: format, recording format, roadmap, ADRs.
