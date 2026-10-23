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
| `$schema` | string | Optional. A path or URL of the generated JSON Schema, for editors; the engine ignores its value. |

**Entity.** An object with these keys:

| Key | Type | |
|---|---|---|
| `name` | string | Required. Unique within the scene. 1 to 64 characters, no control characters, no leading or trailing whitespace, and no `/` (reserved). |
| `components` | object | Optional (same as `{}`). Keys are component names, values are the component's fields. |

An entity is identified by its name, nothing else. There are no generated ids, counters or uids anywhere in the format; entities refer to each other by name.

**Components.** Each key in `components` must be a registered component name, each value an object, and an entity has each component at most once. Within a component:

- Keys are the component's field names. A field that is left out takes its default; a field marked required must appear.
- Values must have the field's type and be inside its range. Vectors are arrays of exactly the right length, such as `[x, y]` or `[r, g, b, a]`.
- The built-in components and their fields are in the table below. The generated JSON Schema (issue #27) is the complete reference for every registered component, including your own.

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

**References.** An asset is referred to by a path relative to the working directory, written with `/`. Anything that refers to another entity uses its `name`. A reference to something that does not exist is reported with the file and path of the reference; a missing image is drawn as the magenta and black error checker, and the rest of the scene still loads.

## Written form

The engine reads any layout that follows the rules above. When it writes a scene file, the output is canonical, so a change to one component changes only that component's lines:

- Entities in creation order. Components in registration order. Fields in declaration order, and **every field is written**, including ones at their default.
- Two-space indentation, one entity per block, numeric vectors on a single line, LF line endings, no byte order mark, one newline at the end of the file.
- Numbers are the shortest text that reads back to the same value: `0.1`, not `0.10000000149011612`.
- Comments are not kept. Writing a scene drops them, and the engine never rewrites a file on its own, so comments in a hand-written file are only lost if you ask the engine to save over it.

## Errors

Loading a scene never fails as a whole because of one mistake. Each entity and each component is checked on its own, every problem is reported as the file plus a JSON Pointer (for example `Assets/Scenes/world.json /entities/3/components/Transform/position/1: expected a number, got a string`), the offending component or entity is skipped, and the rest of the scene loads. A misspelled field or component name comes with a suggestion and the list of valid names.

## For AI authors

1. Start from `Assets/Scenes/example.json` and change names and values; keep the same shape.
2. Give every entity a unique `name`; refer to entities by that name.
3. Write every field of a component you use, so the file shows what it is.
4. Use only the components and fields in the table above or the generated schema; guessing a name is an error with a suggestion.
5. Run the validate command (issue #28) before running the game and fix every reported path.
