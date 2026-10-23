# Marlengine

A personal 2D game engine: a shared library that clients link, with a layered application loop and a batched quad renderer.

## Language

**Scene**:
A persistent collection of entities that a layer updates and draws.
_Avoid_: Level, world, render pass

**Frame batch**:
One bracketed span of 2D drawing, submitted under a single camera or pixel-space projection and flushed when it ends.
_Avoid_: Scene (for a render pass), render pass
