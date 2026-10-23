# Roadmap and the Hazel series

Marlengine started from The Cherno's Hazel engine series and has since gone its own way: scenes, behavior and input are plain-text data that an AI can write and git can diff (ADR-0001, ADR-0002). This page records what we take from the rest of the Hazel roadmap, what we skip and why, and the order of the milestones. It is a plan, not a spec: each milestone starts with its own decision.

## Where we are

Done and different from Hazel:

- **Viewport panel**: the world renders into an `OpenGLFramebuffer` shown in an ImGui window (M3). Missing compared with Hazelnut: a dockspace, picking, gizmos, hierarchy and property panels.
- **Scenes**: entt behind our own `Scene`/`Entity`, a component registry, a fail-soft loader, strict hot reload, a canonical writer, a generated JSON Schema and the `marle-scene` tool (M4, M5). Hazel's serialization has none of the validation.
- **Behavior and determinism**: a fixed set of data behaviors, a seeded generator per scene, one `InputFrame` per step, checksums, recordings, `marle-scene simulate` (M6). Hazel's scripting is compiled code with no determinism rule.
- **Play and stop**: `Simulation::Step` holds no state, so a baseline scene plus recorded input gives pause, step and reset without Hazel's clone-the-scene-on-Play.

## What we take

| From Hazel | Why it fits | Where |
|---|---|---|
| `DrawLine` and `DrawCircle` in `Renderer2D` (circle as a shader that tests distance from the quad centre, anti-aliased with `smoothstep`) | The collider outline overlay needs both | M7, before the Sandbox overlay |
| Collider vocabulary: body type (static, kinematic, dynamic) and a sensor flag | Our `Collider` (box or circle, solid or trigger) maps onto it, and keeps Box2D possible later | M7 ADR |
| Inspector generated from the registry | The registry already knows every field, type and range, so no per-component UI code | M9 |
| Mouse picking (an integer attachment, or a CPU hit-test against collider shapes) | Click an entity to see its name and components | M9 |
| Pause, step and reset | Cheap because the step is stateless and the run is deterministic | M9 |
| Audio (miniaudio, `AudioSource`/`AudioListener` as data components) | Single header like our other vendored code; output only, so it never touches determinism | after M9 |

## What we skip or defer

- **Box2D.** The M7 ADR records it as the main alternative to our own simple shapes. It adds dynamic bodies, friction and stacking, which a top-down tile world does not need. Its body state would live outside the registry, so checksums, hot-reload carry-over and fail-soft loading all get harder, and its determinism across platforms is not guaranteed. Revisit if a game needs real physics.
- **Native C++ and C# scripting.** Compiled code breaks "an AI writes text and the loader checks it". An embedded Lua stays deferred as in ADR-0002 until a game needs logic the fixed set cannot express.
- **UUID asset handles.** ADR-0001 chose names, not ids, so scenes stay readable and diffable. Asset paths work today. Revisit if renames become painful.
- **Gizmos and a full editor.** Saving from an editor goes through the canonical writer, which drops comments from hand-written scene files, and the text file is the source of truth. Editing and save-back needs its own design. Inspect-only tools (M9) avoid the problem.
- **MSDF fonts and a dynamic texture slot limit.** Polish: the bitmap font and 16 slots are enough for now.

## Order

1. **M7, collision and triggers**: colliders, solid tiles, movement that stops at walls, overlap events, a debug overlay (needs the line and circle primitives), determinism tests. Starts with the ADR.
2. **M8, interactions and dialog**: a trigger and action vocabulary and dialog as plain text, built on M7's overlaps. Its own ADR first, as ADR-0002 says.
3. **M9, inspect and step tools**: picking, a generated hierarchy and inspector, pause, step and reset. Read-only. It could move ahead of M8 if seeing and poking at M7's results matters more than dialog.
4. **Audio**, as a small milestone after that.
