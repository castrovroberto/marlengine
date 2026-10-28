# Adding a behavior component

A behavior is a component whose update function runs every fixed step (ADR-0002). It is data in a scene file plus a few lines of C++ that read and change components. This page is the whole recipe, with a worked example you can copy. It was checked by adding the example to the engine, passing the tests and taking it out again.

Adding a behavior to the engine is a C++ change: ask before doing it unless the task says to. A game that wants a behavior of its own does the same in its own code with its own registry (steps 1, 2 and 6 below; see "Your own game").

## The rules a behavior keeps

- It gets a `BehaviorContext` and nothing else: the `Scene`, `Self` (the entity, or the scene's own entity for a scene-level behavior), the step's `InputFrame`, the scene's `RandomGenerator` and the fixed `Dt`.
- No clock, no unseeded randomness (`std::rand`, `std::random_device`, `std::` distributions), no polling `Input`, no static or global state. The same scene, seed and input must give the same run, and the golden checksums are what notices when they do not.
- It may change components. It must not add or remove components or create or destroy entities.
- Everything it needs to remember between steps is a field of its component, so it is saved with the scene, carried over a hot reload and part of the checksum. A counter or timer that lives anywhere else breaks all three.
- It must cope with missing things: no `Transform`, a target that does not exist. Do nothing; do not log every step.

## Checklist

1. [ ] The component struct, in `Marle/src/Marle/Scene/Components.h`. A plain struct with a default for every field.
2. [ ] Register it with its fields and the behavior function, in `RegisterBuiltinComponents` (`Scene/BuiltinBehaviors.cpp`), **before `ClampPosition`**, which must stay the last registration so it runs last on an entity. Behaviors run on an entity in registration order, so put yours after whatever it depends on.
3. [ ] Update the two tests that list the registration order by hand, in `Tests/SceneTests/src/SceneTests.cpp` (search for "the built-in components register in order" and "components are described in registration order"), and the comment above `RegisterBuiltinComponents` in `ComponentRegistry.h`.
4. [ ] Regenerate the generated files and look at the diff:
   `MARLE_UPDATE_GOLDEN=1 ./bin/Debug-linux-x86_64/SceneTests/SceneTests` rewrites `Assets/Scenes/scene.schema.json` and the component table in `docs/scene-format.md`. Do not edit either by hand. If you also changed `world.json` or anything that changes how it plays, it rewrites the determinism goldens too; a new behavior that no scene uses changes neither checksum, and that is what you want to see.
5. [ ] A test: load a small scene that uses it (`LoadSceneFromText`), `Simulation::Step` it a known number of times and check the fields; check it ignores a missing `Transform` or target; check a bad value is a validation error. Add it near the other behavior tests in `SceneTests.cpp`. For a behavior with randomness, check two runs with the same seed agree.
6. [ ] Try it from the command line: write a scene, run `marle-scene validate` on it, then `marle-scene simulate <file> --steps N` and read the field changes (add `--json` or `--report -` if you are parsing). `marle-scene describe <Component>` shows how it reads to an author.
7. [ ] Describe it. Put the explanation in the registry descriptions (they feed the schema, `describe` and the table), and any rule that does not fit in a description (what it does with missing parts, ordering) in the "Behaviors" section of `docs/scene-format.md`. Update the list in `CLAUDE.md` if the behavior is built in.
8. [ ] If it is for players to see, put it in `Assets/Scenes/world.json` (then the determinism goldens change on purpose: regenerate, run the tests again and explain the changed checksum), or in `Assets/Scenes/example.json` (then regenerate `fixtures/canonical.json` the same way).

Run the whole of `SceneTests` at the end (`make config=debug` then `./bin/Debug-linux-x86_64/SceneTests/SceneTests`). It must print `ALL PASSED`.

## Worked example: `Flash`

A behavior that switches a sprite's tint between two colors every `interval` seconds.

**1. The struct** (`Components.h`, next to `Spin` and `Orbit`):

```cpp
// Switches the entity's SpriteRenderer tint between two colors every `Interval` seconds.
struct Flash {
    float Interval = 0.5f;                      // seconds between switches
    glm::vec4 OnTint = { 1.0f, 1.0f, 1.0f, 1.0f };
    glm::vec4 OffTint = { 0.2f, 0.2f, 0.2f, 1.0f };
    bool On = false;                            // which color it shows now; saved with the scene
    float Timer = 0.0f;                         // seconds until the next switch; 0 switches on the first step
};
```

`On` and `Timer` are state, so they are fields: a scene saved part way through, or reloaded while it runs, carries on from where it was.

**2. The registration** (`BuiltinBehaviors.cpp`, before `ClampPosition`):

```cpp
registry.Register<Flash>("Flash", "Behavior: switches the tint of the entity's SpriteRenderer between two colors every interval. It does nothing without a SpriteRenderer.")
    .Field(&Flash::Interval, "interval", "Seconds between switches.").Range(0.01, 3600)
    .Field(&Flash::OnTint, "onTint", "The tint [r, g, b, a] shown while on.").Range(0, 1)
    .Field(&Flash::OffTint, "offTint", "The tint [r, g, b, a] shown while off.").Range(0, 1)
    .Field(&Flash::On, "on", "Whether it shows onTint now. It switches by itself and is saved with the scene.")
    .Field(&Flash::Timer, "timer", "Seconds until the next switch, counting down; 0 switches on the first step.").Range(0, 3600)
    .Behavior([](Flash& flash, BehaviorContext& ctx) {
        SpriteRenderer* sprite = ctx.Self.TryGet<SpriteRenderer>();
        if (!sprite) return;
        flash.Timer -= (float)ctx.Dt;
        if (flash.Timer <= 0.0f) {
            flash.Timer += flash.Interval;
            flash.On = !flash.On;
        }
        sprite->Tint = flash.On ? flash.OnTint : flash.OffTint;
    });
```

Field order here is the order in files, the schema and the table. `Range` limits every number in the field; use `.Required()`, `.OneOf({...})`, `.EntityReference()` or `.AssetPath()` as the field needs (see "Scenes" in `CLAUDE.md`), and `.Validate(fn)` for a rule that spans fields (as `ClampPosition` does for min and max).

**3. Try it:**

```json
{
  "version": 1,
  "name": "flash",
  "entities": [
    { "name": "lamp", "components": {
      "Transform": { "position": [0, 0], "rotation": 0, "scale": [32, 32] },
      "SpriteRenderer": { "texture": "", "cell": [0, 0], "cellSize": [0, 0], "tint": [1, 1, 1, 1] },
      "Flash": { "interval": 0.5, "onTint": [1, 1, 0, 1], "offTint": [0.2, 0.2, 0.2, 1], "on": false, "timer": 0 }
    } }
  ]
}
```

`marle-scene simulate flash.json --steps 70` prints:

```
  lamp.SpriteRenderer.tint: [1.0,1.0,1.0,1.0] -> [1.0,1.0,0.0,1.0]
  lamp.Flash.on: false -> true
  lamp.Flash.timer: 0.0 -> 0.33333284
checksum bfa667e6e594e6e9
```

(Three switches in 70 steps, so it ends on, with a third of a second left to the next.)

**4. What the tests then say.** Adding it fails seven checks until the generated files are regenerated: the schema (four checks), the component table, and the two order tests, which you edit by hand to add `"Flash"` before `"ClampPosition"`. After that `ALL PASSED`, and the golden checksums have not moved, because no scene uses `Flash`.

## Your own game

A game with components of its own registers them on its own `ComponentRegistry` and builds its own tool with `RunSceneTool(args, registry, std::cout, std::cerr)` (see `docs/scene-format.md`, "Command line"). The struct, registration, rules and tests are the same; the generated files are the game's own (`marle-scene schema --output` and `marle-scene reference --update` against its registry). Components registered after `RegisterBuiltinComponents` run after `ClampPosition` on the same entity, so a game behavior that moves an entity is not clamped by it.
