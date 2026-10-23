# Scenes are text data, stored in entt behind our own Scene and Entity

Marlengine is meant to be driven by AI: worlds, characters, interactions and later physics, scripts and dialog are added by writing plain text that the engine loads, and that diffs cleanly in git. So a **Scene** is defined by a text **scene file**, loaded through a component registry (a name, a way to read and write fields, and a description of those fields for each **Component**), and the engine stores entities in [entt](https://github.com/skypjack/entt) behind our own `Scene` and `Entity` types so client code never touches `entt::registry`.

## Considered Options

- **Custom ECS.** Rejected: storage, views and safe removal during iteration are a milestone of work, and neither entt nor a custom ECS provides text loading, validation or schema. That layer (the registry) is ours either way and is where the value is. Reconsider only if entt's constraints get in the way; the wrapper keeps that cheap.
- **No ECS (plain object list).** Rejected: components are the unit a text file, a schema and an AI edit, so they need to be first-class.
- **YAML (as in the Cherno series) or TOML for scene files.** Rejected in favor of JSON with comments allowed: one wrong indent in YAML is exactly the kind of small edit that breaks a whole scene, and TOML is awkward for nested entity lists. JSON is strict, has mature schema tooling, and nlohmann/json is a single header.

## Consequences

- Scene files use stable, human-readable entity names, and references between entities use those names. No generated ids or counters, so hand and AI edits stay small and local.
- Loading fails soft: each entity and component is validated on its own, errors name the file and the path inside it, the rest of the scene still loads, and a missing asset shows up as the existing error checker. Unknown fields are errors, not ignored.
- Output is deterministic (stable key order, one entity per block) so git diffs stay readable.
- Loading and validation must work without a GL context, so a `validate` command and the schema generated from the registry can serve as the AI's feedback loop.
- This adds one dependency (entt) now and another (nlohmann/json) with the format work. Scripting, dialog and physics are not decided here; the registry will reference them by name.
