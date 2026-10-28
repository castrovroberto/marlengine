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
| `components` | object | Optional (same as `{}`). Components of the scene itself, such as its random seed: keys are component names, values the component's fields, exactly as for an entity. |
| `entities` | array | Required, may be empty. The entities, in creation order. |
| `$schema` | string | Optional. A path or URL of the generated JSON Schema (`scene.schema.json`, next to the scenes), so an editor can check the file as you type; the engine ignores its value. |

**Entity.** An object with these keys:

| Key | Type | |
|---|---|---|
| `name` | string | Required. Unique within the scene. 1 to 64 characters, no control characters, no leading or trailing whitespace, and no `/` (reserved). |
| `components` | object | Optional (same as `{}`). Keys are component names, values are the component's fields. |

The scene's own components belong to the scene as a whole and not to any entity: they are not listed among `entities`, no entity can refer to them by name, and any registered component can be used there. They come before the entities in the file, and behaviors that run on the scene run before the entities' (see ADR-0002).

An entity is identified by its name, nothing else. There are no generated ids, counters or uids anywhere in the format; entities refer to each other by name.

**Components.** Each key in `components` must be a registered component name, each value an object, and an entity has each component at most once. Within a component:

- Keys are the component's field names. A field that is left out takes its default; a field marked required must appear.
- Values must have the field's type and be inside its range. Vectors are arrays of exactly the right length, such as `[x, y]` or `[r, g, b, a]`.
- A string field can be limited to a set of allowed values (for example the name of a key); anything else is an error that lists the allowed values and suggests the closest one. Matching is exact, so case matters. The schema describes such a field with an `enum`; when the set is long (for example key names) the error says how many values are allowed and points to the schema instead of printing them all.
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
| `Tilemap` | `texture` | string | `""` | The sprite sheet image path. |
| | `cellSize` | vec2 | `[32, 32]` | One sheet cell in pixels. Min 1. |
| | `tileSize` | vec2 | `[64, 64]` | One tile in world units. Min 0.001. |
| | `rows` | string[] | `[]` | The tiles, one string per row (see Tilemaps). |
| | `solid` | string | `""` | The tile characters that block movement, for example `"cdef"` (see Tilemaps). Each is `0`-`9` or `a`-`z`, listed once; empty means none. |
| `Collider` | `shape` | string | `"box"` | `"box"` or `"circle"`. |
| | `size` | vec2 | `[32, 32]` | A box's width and height in world units; not used by a circle. Min 0.001. |
| | `radius` | float | `16` | A circle's radius in world units; not used by a box. Min 0.001. |
| | `offset` | vec2 | `[0, 0]` | From the entity's `Transform.position` to the shape's center. |
| | `kind` | string | `"solid"` | `"solid"` stops movement; `"trigger"` blocks nothing and only reports overlaps. |
| `Random` | `seed` | int | `1` | On the scene itself (its top-level `components`). Any integer; the same seed gives the same sequence of random numbers on every run. |
| `KeyboardMove` | `speed` | float | `100` | Behavior. World units per second. 0 to 100000. |
| | `runMultiplier` | float | `2` | How many times faster while the run key is held. 0 to 100. |
| | `runKey` | string | `"LeftShift"` | A key name (see Key names), or `""` for no run key. |
| | `keys` | string | `"both"` | `"wasd"`, `"arrows"` or `"both"`. |
| | `relativeTo` | string | `""` | An entity (usually the camera) whose rotation turns the movement so W goes up its screen; `""` for the world's axes. |
| | `tintWhenRunning` | bool | `false` | Set this entity's `SpriteRenderer` tint to `runTint` while running and `idleTint` otherwise. |
| | `idleTint` | vec4 | `[1, 1, 1, 1]` | Used with `tintWhenRunning`: the sprite's normal tint. 0 to 1. |
| | `runTint` | vec4 | `[1, 0.65, 0.3, 1]` | Used with `tintWhenRunning`. 0 to 1. |
| `KeyboardRotate` | `speed` | float | `1` | Behavior. Radians per second. 0 to 100. |
| | `counterClockwiseKey` | string | `"Q"` | A key name. |
| | `clockwiseKey` | string | `"E"` | A key name; with both keys held it does not turn. |
| `WheelZoom` | `factor` | float | `1.1` | Behavior, on an entity with a `Camera`. The zoom is multiplied by this for each wheel notch up. 0.01 to 10. |
| | `minZoom` | float | `0.25` | 0.01 to 100. |
| | `maxZoom` | float | `4` | 0.01 to 100; not smaller than `minZoom`. |
| `Follow` | `target` | string | `""` | Behavior. The entity to follow (an entity reference); the entity is put at its position plus `offset` every step. |
| | `offset` | vec2 | `[0, 0]` | Where from the target, in world units. |
| `Spin` | `speed` | float | `1` | Behavior. Radians per second, counter-clockwise when positive; starts from the entity's own `Transform.rotation`. -1000 to 1000. |
| `Orbit` | `target` | string | `""` | Behavior. The entity to go around (an entity reference). |
| | `radius` | float | `100` | Distance from the target, in world units. 0 to 1000000. |
| | `speed` | float | `1` | Radians per second around the target; negative goes the other way. -1000 to 1000. |
| | `angle` | float | `0` | Where on the circle it is now, in radians (0 is to the right of the target). It advances every step and is saved with the scene; set it to start elsewhere. |
| | `rotationSpeed` | float | `0` | Radians per second the entity itself turns as it goes round. -1000 to 1000. |
| `Wander` | `speed` | float | `50` | Behavior. World units per second. 0 to 100000. |
| | `changeEvery` | float | `1` | Seconds between new directions. 0.01 to 3600. |
| | `heading` | float | `0` | The direction it is walking now, in radians; it changes by itself. |
| | `timer` | float | `0` | Seconds until the next new direction, counting down; `0` picks one on the first step. 0 to 3600. |
| `RestartOnKey` | `key` | string | `"R"` | Behavior, for the scene itself (its top-level `components`). When this key is pressed it asks the host to restart the scene; the Sandbox loads it from its file again. A key name. |
| `ClampPosition` | `min` | vec2 | `[0, 0]` | Behavior. The lower left corner of the rectangle the position is kept in. |
| | `max` | vec2 | `[100, 100]` | The upper right corner; not smaller than `min` on either axis. |

**Colliders.** A `Collider` is a box or a circle centered on the entity's `Transform.position` plus `offset`, in world units; the `Transform`'s rotation and scale do not change it, and an entity with no `Transform` has no collider. Every field is written, so the one that does not fit the shape (`size` for a circle, `radius` for a box) is ignored. The decision is in `docs/adr/0003-collision-is-simple-deterministic-shapes-in-scene-data.md`; **Solid colliders stop movement.** After an entity's behaviors have run, if it has a solid `Collider` and moved, the engine moves it along x and then along y and stops it flush against every other solid collider and solid tile in its way, so it slides along a wall instead of sticking to it. This happens for any behavior that moves it (`KeyboardMove`, `Follow`, `Wander`...), right after that entity's own behaviors and before the next entity's, so an entity later in the file (a camera with `Follow`) sees where it ended up. Things that do not stop it: triggers, entities with no `Collider`, and a solid it already overlapped where the step began (so it can walk out). A mover never pushes another entity, and an entity that did not move is never moved, so walls that overlap each other are left alone. **Triggers report overlaps.** Once every entity has run in a step, the engine works out which triggers overlap which other colliders (solid or trigger, never the trigger itself; solid against solid is not reported, those block). The result is `Scene::GetOverlaps()`: the pairs touching now, and which of them *entered* (not touching in the step before), are *staying*, or *left* (including a pair whose entity was destroyed), each pair being the trigger's name and the other entity's name, in the creation order of the trigger and then of the other entity (two triggers that overlap are listed both ways round). Behaviors see the result of the step before, one step late. It is worked out again from positions every step, so it is not in a scene file or a checksum, and a scene that starts with an overlap reports it as entered on its first step. Shapes that only touch do not overlap.

**Tilemaps.** A `Tilemap` is a grid of tiles cut from a sprite sheet, so a whole map is data and not one entity per tile. The entity's `Transform.position` is the map's bottom-left corner in the world and `Transform.scale` multiplies `tileSize`; its rotation is not used. `solid` lists the tile characters that block movement, for example `"solid": "cdef"` makes every tile drawn with a `c`, `d`, `e` or `f` a wall. A solid tile is a box of `tileSize` (times the scale) exactly where the tile is drawn, so what you see is what blocks; `.` is never solid. Entities with a solid `Collider` stop at solid tiles (see Colliders).

- `rows` lists the map **top to bottom, as it looks**, one string per row. Every row has the same number of characters.
- Each character is one tile: the sprite sheet cell with that number, counting from the sheet's bottom-left cell, left to right and then up, in base 36 (`0`-`9`, then `a`-`z`; lowercase only). `.` means no tile.
- The number of cells per row of the sheet is the sheet's width divided by `cellSize.x`. A cell beyond the sheet is drawn as the error checker.

```jsonc
"Tilemap": {
  "texture": "Assets/Textures/test_sheet.tga",
  "cellSize": [32, 32],
  "tileSize": [64, 64],
  "rows": [
    "3333",
    "3.01",
    "3333"
  ]
}
```

**Random numbers.** A scene's random numbers come from one generator that starts from `Random.seed` in the scene's own components (seed 1 if there is none) and from nothing else: never the clock or the machine, so the same scene, seed and input always play out the same way (ADR-0002). A seed can be any integer, and a negative one is as good as any other. The generator is runtime state: a scene saved while it runs writes its seed, not where it had got to, so loading it starts the sequence again.

**Units.** World coordinates have Y pointing up. Angles are radians, counter-clockwise. Colors are floats from 0 to 1.

**References.** An asset is referred to by a path relative to the working directory, written with `/`, with no `..`. Anything that refers to another entity uses its `name`; such a field says so in the schema, and an empty string means no reference. Names are checked once every entity has loaded, so an entity may refer to one that comes later in the file, or to itself. A reference to something that does not exist is reported with the file and path of the reference, and the rest of the scene still loads (see Errors).

## How a scene is drawn

The engine draws a scene (`SceneRenderer`) like this, so the order and values in the file matter:

- **Draw order is file order.** Entities are drawn one after another in the order they appear, so a later entity is on top of an earlier one. Put the ground first and the things standing on it after.
- **The camera** is the first entity with a `Camera` whose `primary` is `true` (it also needs a `Transform`). The view is centered on its `Transform.position`, turned by its `rotation`, and `zoom` magnifies it. At zoom 1 the view shows as many world units as the window has pixels. With no primary camera the view is centered on the origin, and a warning is logged.
- **A sprite** (`SpriteRenderer`) is drawn at its `Transform` (position, rotation, and `scale` as its size in world units). An empty `texture` draws a flat `tint` colored quad; a texture with `cellSize` `[0, 0]` draws the whole image; with a `cellSize` it draws the sprite sheet cell at `cell`. A sprite with no `Transform` is not drawn.
- **A tilemap** (`Tilemap`) draws one quad per tile.
- A texture that cannot be loaded is drawn as the magenta and black error checker, and everything else still draws.

`Assets/Scenes/world.json` is the Sandbox's world and a complete example of all of this: a camera, a 32 by 24 tilemap whose border tiles (`"solid": "cdef"`) are walls, sprites cut from a sheet, and flat colored ones. It also has colliders: the player is a solid box, `wall_a` and `wall_b` are solid boxes, `pillar` is a solid circle (its sprite is a square, which is what the Sandbox's F4 collider outlines are good for seeing) and `zone` is a trigger. `marle-scene simulate` lists which triggers touched what; the Sandbox's F4 draws every collider as an outline (a trigger turns red while something is in it).

## Behaviors

A **behavior** is a component that does something every fixed step (60 times a second in the Sandbox): move its entity, follow another, spin, and so on. You use one like any other component, by naming it and setting its fields; the schema says which components are behaviors. Behaviors are configured, not programmed (ADR-0002).

- **Order is file order.** Each step runs the scene's own components first, then each entity in the order it appears in the file, and within an entity its behaviors in the order the component table lists them. So an entity that follows another must come **after** it in the file, or it follows where the other was one step ago.
- **Repeatable.** A behavior knows only the fixed step, the player's input for that step, and the scene's seeded random numbers, so the same scene, seed and input always play out the same way (on the same build and platform).
- **State lives in the scene.** What a behavior changes is ordinary component data (a `Spin` changes `Transform.rotation`), so a scene saved while it runs holds where everything got to. The one exception is the position in the random sequence, which restarts from the seed when the saved scene is loaded.
- **Drawing between steps.** The engine remembers where each `Transform` was before the last step and draws between that and the current one, so motion stays smooth when the display and the steps do not line up. That memory is never written to a file.

**The movement behaviors.** `KeyboardMove`, `KeyboardRotate`, `WheelZoom` and `ClampPosition` act on the entity they are on: they change its `Transform` (or `Camera`), so that entity needs one. Put `ClampPosition` on the same entity as `KeyboardMove`: behaviors in an entity run in the order of the component table, so the clamp always comes after the move. A complete example, a camera that turns and zooms and a player that walks relative to it and cannot leave a 1000 by 1000 area:

```jsonc
{
  "version": 1,
  "name": "Walk",
  "entities": [
    {
      // Before the player, so the player is moved by the camera's rotation of this step
      "name": "camera",
      "components": {
        "Transform": { "position": [500, 500], "rotation": 0, "scale": [1, 1] },
        "Camera": { "zoom": 1, "primary": true },
        "KeyboardRotate": { "speed": 1, "counterClockwiseKey": "Q", "clockwiseKey": "E" },
        "WheelZoom": { "factor": 1.1, "minZoom": 0.25, "maxZoom": 4 }
      }
    },
    {
      "name": "player",
      "components": {
        "Transform": { "position": [500, 500], "rotation": 0, "scale": [48, 48] },
        "SpriteRenderer": { "texture": "", "cell": [0, 0], "cellSize": [0, 0], "tint": [1, 1, 1, 1] },
        "KeyboardMove": { "speed": 300, "runMultiplier": 2, "runKey": "LeftShift", "keys": "both", "relativeTo": "camera", "tintWhenRunning": true, "idleTint": [1, 1, 1, 1], "runTint": [1, 0.65, 0.3, 1] },
        "ClampPosition": { "min": [24, 24], "max": [976, 976] }
      }
    }
  ]
}
```

A camera that should also follow the player has to come after the player in the file; its rotation is then the one from the end of the previous step when the player reads it, which is one sixtieth of a second behind and not noticeable.

**The motion behaviors.** `Follow`, `Spin`, `Orbit` and `Wander` also act on their own entity's `Transform`. `Follow` and `Orbit` name another entity, which must exist (a missing one is reported when the scene loads, and the behavior does nothing until it does) and must come **earlier** in the file, or they work from where it was one step ago; neither does anything to itself. `Orbit`'s `angle` and `Wander`'s `heading` and `timer` are the behavior's current state, kept in the scene like any other value. `Wander` takes its new directions from the scene's seeded random numbers (the `Random` component), so with the same seed it walks the same path every time, and an entity with no `Transform` draws no random numbers. On an entity with `ClampPosition`, the clamp always runs after the others. A complete example: a ground, a post, something spinning on it, a ring of lights going round the post, a follower, and a wanderer kept inside a fence.

```jsonc
{
  "version": 1,
  "name": "Motion",
  "components": { "Random": { "seed": 2024 } },
  "entities": [
    {
      "name": "post",
      "components": {
        "Transform": { "position": [500, 500], "rotation": 0, "scale": [32, 32] },
        "SpriteRenderer": { "texture": "", "cell": [0, 0], "cellSize": [0, 0], "tint": [0.8, 0.8, 0.8, 1] },
        "Spin": { "speed": 0.5 }
      }
    },
    {
      "name": "light",
      "components": {
        "Transform": { "position": [0, 0], "rotation": 0, "scale": [12, 12] },
        "SpriteRenderer": { "texture": "", "cell": [0, 0], "cellSize": [0, 0], "tint": [1, 0.9, 0.2, 1] },
        "Orbit": { "target": "post", "radius": 80, "speed": 2, "angle": 0, "rotationSpeed": 6 }
      }
    },
    {
      "name": "shadow",
      "components": {
        "Transform": { "position": [0, 0], "rotation": 0, "scale": [32, 32] },
        "SpriteRenderer": { "texture": "", "cell": [0, 0], "cellSize": [0, 0], "tint": [0, 0, 0, 0.5] },
        "Follow": { "target": "post", "offset": [6, -6] }
      }
    },
    {
      "name": "rabbit",
      "components": {
        "Transform": { "position": [300, 300], "rotation": 0, "scale": [20, 20] },
        "SpriteRenderer": { "texture": "", "cell": [0, 0], "cellSize": [0, 0], "tint": [1, 1, 1, 1] },
        "Wander": { "speed": 60, "changeEvery": 0.75, "heading": 0, "timer": 0 },
        "ClampPosition": { "min": [100, 100], "max": [400, 400] }
      }
    }
  ]
}
```

**Key names.** Fields that name a key take one of these, spelled exactly (letters are upper case):

```keynames
A B C D E F G H I J K L M N O P Q R S T U V W X Y Z
0 1 2 3 4 5 6 7 8 9
Space Up Down Left Right Enter Escape Tab Backspace Insert Delete
F1 F2 F3 F4 F5 F6 F7 F8 F9 F10 F11 F12
LeftShift RightShift LeftControl RightControl LeftAlt RightAlt LeftSuper RightSuper
NumPad0 NumPad1 NumPad2 NumPad3 NumPad4 NumPad5 NumPad6 NumPad7 NumPad8 NumPad9
NumPadDecimal NumPadDivide NumPadMultiply NumPadSubtract NumPadAdd NumPadEnter NumPadEqual
Apostrophe Comma Minus Period Slash Semicolon Equal LeftBracket Backslash RightBracket GraveAccent
CapsLock ScrollLock NumLock PrintScreen Pause Menu
```

## Written form

The engine reads any layout that follows the rules above. When it writes a scene file (`SaveScene`), the output is canonical, so loading a canonical file and writing it again gives the same bytes, and a change to one component changes exactly one line:

- The scene's own components (when it has any) between `name` and `entities`, written like an entity's. Entities in creation order. Components in registration order. Fields in declaration order, and **every field is written**, including ones at their default. An entity with no components has no `components` key.
- Two-space indentation, one entity per block, **one line per component** with its fields inside `{ ... }` and vectors as `[a, b]`, LF line endings, no byte order mark, one newline at the end of the file. The exception is a component with a list of strings (a `Tilemap`'s `rows`): it is written as a block, one field per line and one string per line, so a change to one row is a change to one line.
- Numbers are the shortest text that reads back to the same value: `0.1`, not `0.10000000149011612`, and a whole number has no decimal point (`1024`, not `1024.0`). Text is UTF-8, with only quotes, backslashes and control characters escaped.
- The `$schema` key is written only when the caller asks for it, since loading does not keep it.
- Comments are not kept. Writing a scene drops them, and the engine never rewrites a file on its own, so comments in a hand-written file are only lost if you ask the engine to save over it.
- Components the registry does not know are not written.

What is written always reads back. If the scene holds something a file cannot (an entity name the rules forbid, a number that is not finite, a value outside its field's range, an unsafe asset path, a reference to an entity that does not exist, text that is not valid UTF-8), nothing is written, every reason is reported, and any existing file is left as it was. Files are written to a temporary file beside the target and renamed into place, so a failed save never leaves a half-written scene.

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
| A key repeated inside the scene's own `components` | Those components are skipped; the entities load. |
| Scene `components` that is not an object, or a bad scene component | Reported with the same rules and paths as an entity's (for example `/components/Camera/zoom`); the entities load. |
| An asset path that is absolute, uses `\`, or contains `..` | The component is skipped, so a scene file cannot point at files outside the working directory. |
| An asset file that does not exist | Reported, but the component loads, so the entity still shows up (a missing image is drawn as the magenta and black error checker). |
| A reference to an entity that does not exist (an entity that was skipped does not exist either, and neither does the scene's own entity) | Reported with a suggestion when a name is close, after the other problems; the component still loads, but nothing uses the reference until it is fixed. |

Some mistakes make the file unusable, and then no scene is loaded and the problem says why: the file cannot be opened or parsed (the message names the line and column), the top level is not an object, `version` is missing, not an integer or not supported, or a key is repeated outside an entity and the scene's `components`. A missing `name` is only a problem (the scene is called `Untitled`), and so is a missing or malformed `entities`.

Problems come back as a list: repeated keys first (they are found while parsing), then the rest in file order. A misspelled field, component or key name comes with a suggestion and the list of valid names.

## Schema

`Assets/Scenes/scene.schema.json` is a JSON Schema (draft-07) generated from the component registry, so it lists exactly the components and fields the engine knows, each with its type, default, range and description. Editors use it for completion and checking, and an AI can read it to learn what is allowed. It is committed, and a test fails if it is out of date after a component changes; regenerate it (and the canonical test fixture) with `MARLE_UPDATE_GOLDEN=1 ./bin/Debug-linux-x86_64/SceneTests/SceneTests`, from the repository root.

The schema accepts and rejects the same files as the loader, with exceptions that a schema cannot express: two entities with the same name, an asset file that does not exist, a key repeated within an object, and rules that span fields (tile rows of equal length, tile characters that exist), and whether an entity a reference names exists. The loader is the authority. A test checks the agreement against every fixture file and against the name and asset path rules.

## Reloading while the game runs

The Sandbox watches `Assets/Scenes/world.json` and reloads it a few times a second whenever the file's modification time or size changes, so you can edit the file (or have an AI edit it) and watch the world change without restarting. Reloading is **strict**: a reload is applied only if the file has no problems at all. If it has any, including a typo in one field or an image that is not there yet, the scene that is running stays exactly as it was, every problem is logged and shown in the Scene panel (F3), and the file is tried again the next time it changes. The first load at startup is the one exception: with nothing running yet, it keeps whatever loaded.

A successful reload keeps what the simulation had worked out. That state lives in the scene's components (where the player is, how far the camera has turned, where a satellite has got to in its orbit), so the engine compares each component with the file as it was last loaded: a component the edit left alone keeps the value it has reached, and one the edit changed takes the file's value (so moving the player's spawn point in the file does move it). Entities added in the file start as the file has them, removed ones are gone, a renamed entity is a new one, and the random sequence carries on unless the seed was changed (`CarryOverState` in the engine). Pressing R (the scene's `RestartOnKey` behavior) is different: it loads the file afresh, so everything goes back to how the file has it. A file that is briefly missing, as when an editor saves by renaming, is waited for and is not an error. The engine side is `SceneReloader` (`Scene/SceneReloader.h`), which needs no window or GL context.

## Command line

`marle-scene` checks and tidies scene files without opening a window. Build it with `make SceneTool` (it lands in `bin/<Config>-<system>-<arch>/SceneTool/marle-scene`) and run it from the repository root, since asset paths are relative to the working directory.

| Command | What it does |
|---|---|
| `marle-scene validate [--json] <file\|dir>...` | Loads each scene file and prints every problem as `file /pointer: message`, then `OK` or `FAIL` with counts. A directory means every `.json` file in it and below, except `*.schema.json`. `--json` prints one JSON object instead (see below). |
| `marle-scene format [--check] [--drop-comments] [--schema <path>] <file\|dir>...` | Rewrites files in canonical form and keeps an existing `$schema` key. It refuses a file that has problems (writing it would drop what failed to load) and leaves a file with comments alone unless you pass `--drop-comments`. `--check` only reports what would change. `--schema` adds a `$schema` key. |
| `marle-scene schema [--output <file> \| --check <file>]` | Prints the JSON Schema, writes it to a file, or checks that a file is up to date. |
| `marle-scene simulate <file> [--steps N] [--recording <file>] [--seed N] [--expect <checksum>] [--json]` | Runs a scene headless, with no window or GL context, and prints what changed (field by field, first 40 lines) and the final checksum. A scene with any problem is refused first, as `validate` would report. `--steps` defaults to the recording's length, else 600; input comes from `--recording` (see `docs/input-recording.md`; one made for another version of the scene or another seed is refused), else there is none. `--seed` replaces the scene's seed, and then the recording's seed is not compared. `--expect` makes it a CI check: exit 1 if the checksum differs. A recording that carries a checksum must reproduce it when played whole. The checksum is the same on every run and equals the one the Sandbox reaches replaying the same recording. `--json` prints one JSON object instead (see below). |
| `marle-scene describe [--json] [<component>]` | Shows what a component is and each of its fields: type, default, range, whether it is required, and whether it is an asset path or an entity reference (names are exact case). It reads the registry, so a game's own tool describes its own components too. Without a name it lists every component. An unknown name gets a did-you-mean, and exit 1. `--json` gives `{ "version", "ok", "name", "description", "behavior", "fields": [ { "name", "description", "type", "default", "required", "asset_path", "entity_reference", "min", "max", "one_of" } ] }` (`min`, `max` and `one_of` only when the field has them). |
| `marle-scene help` | Shows the usage. |

Exit status: `0` everything is fine, `1` problems were found (or, with `--check`, files differ), `2` the command line is wrong. That makes `validate` and the `--check` modes usable in CI, and as the feedback loop for an AI: edit, validate, fix the reported paths, repeat.

#### JSON output

`validate` and `simulate` take `--json` and then print exactly one JSON object on stdout instead of text, for programs to read. Exit codes are the same as without it, and a bad command line (exit 2) is still text on stderr. Every object has `version` (now `1`; it goes up when a field is renamed or removed, new fields can appear without it) and `ok`.

`validate --json`:

```json
{ "version": 1, "ok": false,
  "files": [ { "file": "a.json", "ok": false, "entities": 10, "components": 7,
               "problems": [ { "pointer": "/entities/2/components/Transform/position", "message": "expected an array of 2 numbers, got a string; the component is skipped" } ] } ],
  "summary": { "files": 1, "failing": 1, "problems": 1 } }
```

`entities` and `components` are what loaded, and are left out when nothing could be loaded. `pointer` is a JSON Pointer into the file, and is empty for a problem with the file as a whole.

`simulate --json`:

```json
{ "version": 1, "ok": true, "file": "Assets/Scenes/world.json", "steps_run": 890, "seed": 1,
  "input": { "recording": "run.recording.json", "steps": 890 },
  "checksum": "831d51694e824a58",
  "changes": [ { "kind": "field_changed", "entity": "player", "component": "Transform", "field": "position", "before": [1024, 518], "after": [1960, 88], "field_gone": false } ],
  "overlaps": [ { "trigger": "zone", "other": "player", "first_step": 246, "last_step": 316, "steps_touching": 71, "times_entered": 1 } ],
  "recording_checksum": "831d51694e824a58", "matches_recording": true }
```

- `input` is `null` when there is no recording. `changes` is complete (the text stops at 40 lines); `kind` is one of `field_changed`, `component_added`, `component_removed`, `entity_added`, `entity_removed`, and `field`, `before`, `after` appear only for `field_changed`.
- `recording_checksum` and `matches_recording` appear when the recording carries a checksum and the run is its whole length with its seed; `expected_checksum` and `matches_expected` appear with `--expect`. `ok` is false if either does not match.
- A scene with problems is not run: `ok` is false, `error` says so and `scene_problems` lists them as in `validate`, with no `checksum`. A recording that cannot be read, or that was made for another scene, seed or step rate, gives `ok` false, `error` and `recording_problems`.

The tool knows the built-in components. A game with components of its own builds its own tool with its own registry: `Marle::RunSceneTool(args, registry, std::cout, std::cerr)` is the whole program (see `tools/marle-scene/src/main.cpp`).

## For AI authors

1. Start from `Assets/Scenes/example.json` and change names and values; keep the same shape.
2. Give every entity a unique `name`; refer to entities by that name. Entities are drawn in file order, later ones on top.
3. Write every field of a component you use, so the file shows what it is.
4. Use only the components and fields in the table above or in `scene.schema.json`; guessing a name is an error with a suggestion.
5. Run `marle-scene validate <file>` before running the game and fix every reported path (see Command line).
