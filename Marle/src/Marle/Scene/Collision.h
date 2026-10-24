#pragma once

#include "Marle/Core.h"
#include "Components.h"
#include "Entity.h"
#include "Scene.h"
#include <glm/glm.hpp>
#include <vector>

namespace Marle {

    // A collider placed in the world (ADR-0003): an axis-aligned box or a circle. Simple arithmetic on floats,
    // done in a fixed order, with no clock and no randomness, so it is deterministic. Needs no GL context.
    struct CollisionShape {
        bool Circle = false;
        glm::vec2 Center = { 0.0f, 0.0f };
        glm::vec2 Half = { 0.0f, 0.0f };   // a box's half width and half height
        float Radius = 0.0f;               // a circle's radius
    };

    // The shape a Collider has with its entity at `position`. Rotation and scale are not used.
    EXPORT CollisionShape ShapeOfCollider(const Collider& collider, glm::vec2 position);

    // The shape of one solid tile
    EXPORT CollisionShape ShapeOfTile(const TileBox& tile);

    // Whether two shapes overlap. Touching does not count, so a shape stopped flush against another does
    // not overlap it.
    EXPORT bool ShapesOverlap(const CollisionShape& a, const CollisionShape& b);

    // Works out which trigger colliders overlap which other colliders (solid or trigger, never itself)
    // from where the entities are now, and stores it with Scene::UpdateOverlaps. Entities and triggers are
    // visited in creation order. An entity needs a Collider and a Transform to take part. Called by
    // Simulation::Step once every entity has run.
    EXPORT void FindOverlaps(Scene& scene);

    // Stops an entity that moved this step at the solid things in its way. An entity is stopped when it
    // has a solid Collider and a Transform whose position differs from its PreviousTransform; it is moved
    // from there along x, then along y, and stops flush against any other solid collider or solid tile it
    // would enter, so it slides along a wall instead of sticking to it. A solid it already overlapped where
    // the step began does not stop it, so it can walk out. Each axis is a sweep from the old coordinate to
    // the new one, so a fast mover cannot jump over a thin wall along an axis. It never moves another
    // entity. Called by Simulation::Step right after the entity's behaviors.
    EXPORT void ResolveSolidMove(Scene& scene, Entity entity);

}
