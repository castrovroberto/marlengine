# Scene files

A scene file is a plain-text JSON file that defines a **scene**: its entities and their components. It is meant to be written by hand or by an AI, and to diff cleanly in git. The reasons for the design are in [ADR-0001](adr/0001-scenes-are-text-data-on-entt.md); a complete worked example is `Assets/Scenes/example.json`.

```jsonc
{
  "version": 1,
  "name": "Tiny scene",
  "entities": [
    {
      "name": "player",
      "components": {
        "Transform": { "position": [10, 20], "rotation": 0, "scale": [48, 48] },
        "SpriteRenderer": { "texture": "", "cell": [0, 0], "cellSize": [0, 0], "tint": [1, 0, 0, 1] }
      }
    }
  ]
}
```

## Rules

**File.** UTF-8 text. `//` and `/* */` comments are allowed. Trailing commas are not. Keys that are not listed here are errors, never ignored: a misspelled key must fail loudly, not silently do nothing.

**Top level.** An object with exactly these keys:

| Key | Type | |
|---|---|---|
| `version` | integer | Required. The format version, currently `1`. A file with a newer version than the engine knows is rejected. |
| `name` | string | Required. The scene's name. |
| `entities` | array | Required, may be empty. The entities, in creation order. |
| `$schema` | string | Optional. A path or URL of the generated JSON Schema (`scene.schema.json`, next to the scenes), so an editor can check the file as you type; the engine ignores its value. |

**Entity.** An object with these keys:

| Key | Type | |
|---|---|---|
| `name` | string | Required. Unique within the scene. 1 to 64 characters, no control characters, no leading or trailing whitespace, and no `/` (reserved). |
| `components` | object | Optional (same as `{}`). Keys are component names, values are the component's fields. |

An entity is identified by its name, nothing else. There are no generated ids, counters or uids anywhere in the format; entities refer to each other by name.

**Components.** Each key in `components` must be a registered component name, each value an object, and an entity has each component at most once. Within a component:

- Keys are the component's field names. A field that is left out takes its default; a field marked required must appear.
- Values must have the field's type and be inside its range. Vectors are arrays of exactly the right length, such as `[x, y]` or `[r, g, b, a]`.
- The built-in components and their fields are in the table below. The generated JSON Schema, `Assets/Scenes/scene.schema.json`, is the complete reference for every registered component, including your own (see Schema).

| Component | Field | Type | Default | Meaning |
|---|---|---|---|---|
| `Transform` | `position` | vec2 | `[0, 0]` | World position, in world units. |
| | `rotation` | float | `0` | Turn about the position, in radians, counter-clockwise. |
| | `scale` | vec2 | `[1, 1]` | Size in world units. |
| `SpriteRenderer` | `texture` | string | `""` | Image path; empty draws a flat color. |
| | `cell` | ivec2 | `[0, 0]` | Sprite sheet cell `[column, row]`, row 0 at the bottom. Min 0. |
| | `cellSize` | vec2 | `[0, 0]` | One cell in pixels; `[0, 0]` uses the whole texture. Min 0. |
| | `tint` | vec4 | `[1, 1, 1, 1]` | Color `[r, g, b, a]`, each 0 to 1, multiplied with the texture. |
| `Camera` | `zoom` | float | `1` | Larger shows less of the world. 0.01 to 100. |
| | `primary` | bool | `false` | Whether the scene is drawn through this camera. |

**Units.** World coordinates have Y pointing up. Angles are radians, counter-clockwise. Colors are floats from 0 to 1.

**References.** An asset is referred to by a path relative to the working directory, written with `/`, with no `..`. Anything that refers to another entity uses its `name`. A reference to something that does not exist is reported with the file and path of the reference, and the rest of the scene still loads (see Errors).

## Written form

The engine reads any layout that follows the rules above. When it writes a scene file (`SaveScene`), the output is canonical, so loading a canonical file and writing it again gives the same bytes, and a change to one component changes exactly one line:

- Entities in creation order. Components in registration order. Fields in declaration order, and **every field is written**, including ones at their default. An entity with no components has no `components` key.
- Two-space indentation, one entity per block, **one line per component** with its fields inside `{ ... }` and vectors as `[a, b]`, LF line endings, no byte order mark, one newline at the end of the file.
- Numbers are the shortest text that reads back to the same value: `0.1`, not `0.10000000149011612`, and a whole number has no decimal point (`1024`, not `1024.0`). Text is UTF-8, with only quotes, backslashes and control characters escaped.
- The `$schema` key is written only when the caller asks for it, since loading does not keep it.
- Comments are not kept. Writing a scene drops them, and the engine never rewrites a file on its own, so comments in a hand-written file are only lost if you ask the engine to save over it.
- Components the registry does not know are not written.

What is written always reads back. If the scene holds something a file cannot (an entity name the rules forbid, a number that is not finite, a value outside its field's range, an unsafe asset path, text that is not valid UTF-8), nothing is written, every reason is reported, and any existing file is left as it was. Files are written to a temporary file beside the target and renamed into place, so a failed save never leaves a half-written scene.

## Errors

Loading never fails as a whole because of one mistake. Each entity and each component is checked on its own, every problem is reported as the file plus a JSON Pointer to where it is, and only the part that is wrong is skipped:

```
Assets/Scenes/world.json /entities/3/components/Transform/position: expected an array of 2 numbers, got a string; the component is skipped
```

| Mistake | What happens |
|---|---|
| A component with any wrong field (unknown field, wrong type, out of range, missing required field) | The whole component is skipped, every problem in it is reported. The entity and its other components load. |
| An unknown component name | That component is skipped. |
| An unknown key on an entity or at the top level | Reported; everything else loads. |
| An entity that is not an object, has no valid name, or repeats an earlier name | The entity is skipped. |
| A key repeated inside an entity | The entity is skipped. (A plain JSON parser would silently keep the last one.) |
| An asset path that is absolute, uses `\`, or contains `..` | The component is skipped, so a scene file cannot point at files outside the working directory. |
| An asset file that does not exist | Reported, but the component loads, so the entity still shows up (a missing image is drawn as the magenta and black error checker). |

Some mistakes make the file unusable, and then no scene is loaded and the problem says why: the file cannot be opened or parsed (the message names the line and column), the top level is not an object, `version` is missing, not an integer or not supported, or a key is repeated outside an entity. A missing `name` is only a problem (the scene is called `Untitled`), and so is a missing or malformed `entities`.

Problems come back as a list: repeated keys first (they are found while parsing), then the rest in file order. A misspelled field, component or key name comes with a suggestion and the list of valid names.

## Schema

`Assets/Scenes/scene.schema.json` is a JSON Schema (draft-07) generated from the component registry, so it lists exactly the components and fields the engine knows, each with its type, default, range and description. Editors use it for completion and checking, and an AI can read it to learn what is allowed. It is committed, and a test fails if it is out of date after a component changes; regenerate it (and the canonical test fixture) with `MARLE_UPDATE_GOLDEN=1 ./bin/Debug-linux-x86_64/SceneTests/SceneTests`, from the repository root.

The schema accepts and rejects the same files as the loader, with three exceptions that a schema cannot express: two entities with the same name, an asset file that does not exist, and a key repeated within an object. The loader is the authority. A test checks the agreement against every fixture file and against the name and asset path rules.

## Command line

`marle-scene` checks and tidies scene files without opening a window. Build it with `make SceneTool` (it lands in `bin/<Config>-<system>-<arch>/SceneTool/marle-scene`) and run it from the repository root, since asset paths are relative to the working directory.

| Command | What it does |
|---|---|
| `marle-scene validate <file\|dir>...` | Loads each scene file and prints every problem as `file /pointer: message`, then `OK` or `FAIL` with counts. A directory means every `.json` file in it and below, except `*.schema.json`. |
| `marle-scene format [--check] [--drop-comments] [--schema <path>] <file\|dir>...` | Rewrites files in canonical form and keeps an existing `$schema` key. It refuses a file that has problems (writing it would drop what failed to load) and leaves a file with comments alone unless you pass `--drop-comments`. `--check` only reports what would change. `--schema` adds a `$schema` key. |
| `marle-scene schema [--output <file> \| --check <file>]` | Prints the JSON Schema, writes it to a file, or checks that a file is up to date. |
| `marle-scene help` | Shows the usage. |

Exit status: `0` everything is fine, `1` problems were found (or, with `--check`, files differ), `2` the command line is wrong. That makes `validate` and the `--check` modes usable in CI, and as the feedback loop for an AI: edit, validate, fix the reported paths, repeat.

The tool knows the built-in components. A game with components of its own builds its own tool with its own registry: `Marle::RunSceneTool(args, registry, std::cout, std::cerr)` is the whole program (see `tools/marle-scene/src/main.cpp`).

## For AI authors

1. Start from `Assets/Scenes/example.json` and change names and values; keep the same shape.
2. Give every entity a unique `name`; refer to entities by that name.
3. Write every field of a component you use, so the file shows what it is.
4. Use only the components and fields in the table above or in `scene.schema.json`; guessing a name is an error with a suggestion.
5. Run `marle-scene validate <file>` before running the game and fix every reported path (see Command line).
