# Marlengine

A personal 2D game engine: a shared library that clients link, with a layered application loop and a batched quad renderer.

## Language

**Scene**:
A named collection of entities that a layer updates and draws, defined by a scene file.
_Avoid_: Level, world, render pass

**Entity**:
A named thing in a scene, made of components. It has no behavior or data of its own.
_Avoid_: Object, actor, node, game object

**Component**:
One piece of data on an entity, such as its transform or sprite, identified by a name that scene files use.
_Avoid_: Property, trait, behavior

**Scene file**:
The plain-text file that defines a scene's entities and their components, written by hand or by an AI and versioned in git.
_Avoid_: Level file, prefab, save file

**Frame batch**:
One bracketed span of 2D drawing, submitted under a single camera or pixel-space projection and flushed when it ends.
_Avoid_: Scene (for a render pass), render pass
