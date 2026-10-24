#include "mrlpch.h"
#include "Collision.h"

namespace Marle {

    namespace {

        // Closer than this is touching, not overlapping, so a shape stopped flush against another is not
        // inside it by a rounding error
        constexpr float kContact = 1e-3f;

        // Two shapes reduce to one: the offset between their centers against a box of half size H with its
        // corners rounded by R (R = 0 is a plain box). Box and box: H is the sum of the halves. Circle and box:
        // the box's half size, rounded by the radius. Two circles: a point rounded by the sum of the radii.
        struct Reduced {
            glm::vec2 H;
            float R;
        };

        Reduced Reduce(const CollisionShape& a, const CollisionShape& b)
        {
            if (!a.Circle && !b.Circle) return { a.Half + b.Half, 0.0f };
            if (a.Circle && !b.Circle) return { b.Half, a.Radius };
            if (!a.Circle && b.Circle) return { a.Half, b.Radius };
            return { glm::vec2(0.0f), a.Radius + b.Radius };
        }

        // How far apart the centers must be along `axis` for the two not to overlap, when they are
        // `otherDistance` apart along the other axis; 0 if the other axis already keeps them apart
        float RequiredDistance(const Reduced& r, int axis, float otherDistance)
        {
            const int other = 1 - axis;
            const float away = fabsf(otherDistance);
            if (r.R > 0.0f) {
                const float outside = std::max(away - r.H[other], 0.0f);
                if (outside >= r.R - kContact) return 0.0f;
                return r.H[axis] + sqrtf(r.R * r.R - outside * outside);
            }
            if (away >= r.H[other] - kContact) return 0.0f;
            return r.H[axis];
        }

        // Every solid thing other than `self`, in a fixed order: solid colliders in creation order, then the
        // solid tiles of each tilemap in creation order
        std::vector<CollisionShape> SolidObstacles(Scene& scene, Entity self)
        {
            std::vector<CollisionShape> obstacles;
            const std::vector<Entity> entities = scene.GetEntities();
            for (const Entity& other : entities) {
                if (other == self) continue;
                const Collider* collider = other.TryGet<Collider>();
                const Transform* transform = other.TryGet<Transform>();
                if (collider && transform && collider->Kind == "solid")
                    obstacles.push_back(ShapeOfCollider(*collider, transform->Position));
            }
            for (const Entity& other : entities) {
                if (other == self) continue;
                const Tilemap* map = other.TryGet<Tilemap>();
                if (!map) continue;
                for (const TileBox& tile : TilemapSolidTiles(*map, other.TryGet<Transform>()))
                    obstacles.push_back(ShapeOfTile(tile));
            }
            return obstacles;
        }

    }

    CollisionShape ShapeOfCollider(const Collider& collider, glm::vec2 position)
    {
        CollisionShape shape;
        shape.Circle = collider.Shape == "circle";
        shape.Center = position + collider.Offset;
        shape.Half = collider.Size * 0.5f;
        shape.Radius = collider.Radius;
        return shape;
    }

    CollisionShape ShapeOfTile(const TileBox& tile)
    {
        CollisionShape shape;
        shape.Center = tile.Center;
        shape.Half = tile.Size * 0.5f;
        return shape;
    }

    bool ShapesOverlap(const CollisionShape& a, const CollisionShape& b)
    {
        const Reduced r = Reduce(a, b);
        const glm::vec2 d = a.Center - b.Center;
        if (r.R > 0.0f) {
            const float x = std::max(fabsf(d.x) - r.H.x, 0.0f), y = std::max(fabsf(d.y) - r.H.y, 0.0f);
            return sqrtf(x * x + y * y) < r.R - kContact;
        }
        return fabsf(d.x) < r.H.x - kContact && fabsf(d.y) < r.H.y - kContact;
    }

    void ResolveSolidMove(Scene& scene, Entity entity)
    {
        const Collider* collider = entity.TryGet<Collider>();
        Transform* transform = entity.TryGet<Transform>();
        const PreviousTransform* previous = entity.TryGet<PreviousTransform>();
        if (!collider || !transform || !previous || collider->Kind != "solid") return;
        if (transform->Position == previous->Position) return;

        const glm::vec2 from = previous->Position, to = transform->Position;
        CollisionShape mover = ShapeOfCollider(*collider, from);

        // A solid it already overlapped where the step began does not stop it
        std::vector<CollisionShape> obstacles;
        for (const CollisionShape& obstacle : SolidObstacles(scene, entity))
            if (!ShapesOverlap(mover, obstacle)) obstacles.push_back(obstacle);

        // Along x with y where it was, then along y with x where it ended up. For each obstacle the centers
        // must stay `required` apart on the axis; stopping at the nearest one on the way.
        glm::vec2 result = from;
        for (int axis = 0; axis < 2; axis++) {
            const int other = 1 - axis;
            const float start = from[axis], goal = to[axis];
            float end = goal;
            for (const CollisionShape& obstacle : obstacles) {
                const Reduced r = Reduce(mover, obstacle);
                const float otherDistance = (result[other] + collider->Offset[other]) - obstacle.Center[other];
                const float required = RequiredDistance(r, axis, otherDistance);
                if (required <= 0.0f) continue;

                const float centerStart = start + collider->Offset[axis];
                if (goal > start) {
                    const float limit = obstacle.Center[axis] - required - collider->Offset[axis];
                    if (centerStart <= obstacle.Center[axis] - required + kContact && goal > limit) end = std::min(end, std::max(limit, start));
                } else if (goal < start) {
                    const float limit = obstacle.Center[axis] + required - collider->Offset[axis];
                    if (centerStart >= obstacle.Center[axis] + required - kContact && goal < limit) end = std::max(end, std::min(limit, start));
                }
            }
            result[axis] = end;
        }
        transform->Position = result;
    }

}
