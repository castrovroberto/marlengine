# Marlengine 🧜‍♀️

## In Loving Memory

Marlengine is a personal game engine project, born from a journey of learning and as a heartfelt tribute to my mother. This project began as a way to explore game engine development, following along with [The Cherno's series](https://youtu.be/JxIZbV_XjAs?si=23SZOyihA8f2OLz5_), and became a source of focus and creativity during a difficult time.

The name "Marlengine" and the mermaid theme are a nod to her memory and the sense of wonder I wish to capture with this engine.

## About The Project

Marlengine is a 2D game engine in C++17 where the world is plain text. Scenes, their entities and what those entities do are described in JSON files that a person or an AI can write and git can diff; the engine checks them, runs them the same way every time from a seed and a recording of the input, and reloads them while the game runs. It is a work in progress (see [the roadmap](docs/roadmap.md)).

There are two parts: `Marle`, the engine (a shared library), and `Sandbox`, a demo game that loads `Assets/Scenes/world.json`.

### Where to start

- **An AI agent?** Read [`AGENTS.md`](AGENTS.md) first: what you may change, how to check it, how to regenerate the generated files.
- **Using Claude Code?** [`CLAUDE.md`](CLAUDE.md) is the detailed engine reference it reads.
- **Writing a scene?** [`docs/scene-format.md`](docs/scene-format.md) is the file format and the list of components; `Assets/Scenes/example.json` is a worked example.
- **Adding a behavior?** [`docs/adding-a-behavior.md`](docs/adding-a-behavior.md).
- **Why is it built this way?** [`docs/adr/`](docs/adr/) has the decisions, [`GLOSSARY.md`](GLOSSARY.md) the vocabulary and [`docs/roadmap.md`](docs/roadmap.md) the order of the milestones.

### Quick start

You need a C++17 compiler, `make`, OpenGL 3.3 and, for glm, `git submodule update --init`.

```
./r.sh                                              # generate with Premake, clean, build (debug)
./bin/Debug-linux-x86_64/Sandbox/Sandbox            # run the demo, from the repo root
./bin/Debug-linux-x86_64/SceneTool/marle-scene validate Assets/Scenes
```

In the Sandbox, WASD or the arrows move, Shift runs, Q and E turn the camera, the wheel zooms, F1 to F5 toggle panels and recording, and Esc quits. Editing `world.json` while it runs reloads the world.

### Commands

`marle-scene` checks and explores scene files without a window:

| Command | What it does |
|---|---|
| `validate <file\|dir>...` | Reports every problem in scene files. |
| `simulate <file> [--steps N] [--recording <file>]` | Runs a scene headless and prints what changed and its checksum. |
| `diff --base <file> --proposed <file>` | Previews an edit as a hot reload would take it. |
| `describe [<Component>]` | Shows what a component and its fields are. |
| `format`, `schema`, `reference` | Canonical form, the JSON Schema and the component table. |

Add `--json` to `validate`, `simulate`, `diff` and `describe` for output a program can read. All of them are described in [`docs/scene-format.md`](docs/scene-format.md).

### Tests

Each test is its own program in `Tests/<Name>/src/`, built by `make` and run from the repo root as `./bin/Debug-linux-x86_64/<Name>/<Name>`; they exit non-zero on failure. `SceneTests`, `InputTests`, `LayerStackTests` and `LogTests` need no display; `Renderer2DTests`, `ApplicationTests` and `ImGuiLayerTests` need a display and OpenGL 3.3.
