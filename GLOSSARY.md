# Marlengine

A personal 2D game engine: a shared library that clients link, with a layered application loop and a batched quad renderer.

## Language

**Scene**:
A named collection of entities, and components of its own, that a layer updates and draws, defined by a scene file.
_Avoid_: Level, world, render pass

**Entity**:
A named thing in a scene, made of components. It has no behavior or data of its own.
_Avoid_: Object, actor, node, game object

**Component**:
One piece of data on an entity, such as its transform or sprite, identified by a name that scene files use.
_Avoid_: Property, trait

**Behavior**:
A component that makes its entity (or the scene) do something every fixed step, such as move, follow another entity or spin, configured by its fields in the scene file.
_Avoid_: Script, controller, AI

**Collider**:
A component that gives an entity a shape (an axis-aligned box or a circle) that is either solid, so movement stops at it, or a trigger, which blocks nothing and only reports what overlaps it.
_Avoid_: Hitbox, body, physics object

**Trigger**:
A collider that blocks nothing; a step reports which entities entered, stayed in and left it, for later features such as interactions to read.
_Avoid_: Sensor, zone, event

**Input frame**:
What the player's input was during one fixed step, as behaviors see it; a recording is a list of them.
_Avoid_: Input event, polled input

**Scene file**:
The plain-text file that defines a scene's entities and components, and any components of the scene itself, written by hand or by an AI and versioned in git.
_Avoid_: Level file, prefab, save file

**Frame batch**:
One bracketed span of 2D drawing, submitted under a single camera or pixel-space projection and flushed when it ends.
_Avoid_: Scene (for a render pass), render pass
