#include "mrlpch.h"
#include "ComponentRegistry.h"
#include "Components.h"
#include "Marle/Core/KeyCodes.h"
#include "Marle/Core/KeyNames.h"
#include <cmath>

namespace Marle {

    namespace {

        // Every key name, preceded by "" for no key
        std::vector<std::string> KeyNamesOrNone()
        {
            std::vector<std::string> names = { "" };
            names.insert(names.end(), KeyNames().begin(), KeyNames().end());
            return names;
        }

        // The key a name stands for; an empty or unknown name is no key (never down)
        int KeyOf(const std::string& name) { return name.empty() ? Key::Unknown : KeyFromName(name); }

        // The same arithmetic as the movement code in the Sandbox that these behaviors replaced, so the
        // two can be compared exactly
        void MoveWithKeys(KeyboardMove& move, BehaviorContext& ctx)
        {
            const InputFrame& input = ctx.Input;
            const bool running = input.IsDown(KeyOf(move.RunKey));

            if (move.TintWhenRunning)
                if (SpriteRenderer* sprite = ctx.Self.TryGet<SpriteRenderer>())
                    sprite->Tint = running ? move.RunTint : move.IdleTint;

            Transform* transform = ctx.Self.TryGet<Transform>();
            if (!transform) return;

            const bool wasd = move.Keys != "arrows", arrows = move.Keys != "wasd";
            glm::vec2 direction(0.0f);
            if ((wasd && input.IsDown(Key::W)) || (arrows && input.IsDown(Key::Up)))    direction.y += 1.0f;
            if ((wasd && input.IsDown(Key::S)) || (arrows && input.IsDown(Key::Down)))  direction.y -= 1.0f;
            if ((wasd && input.IsDown(Key::D)) || (arrows && input.IsDown(Key::Right))) direction.x += 1.0f;
            if ((wasd && input.IsDown(Key::A)) || (arrows && input.IsDown(Key::Left)))  direction.x -= 1.0f;
            if (direction == glm::vec2(0.0f)) return;

            // Relative to an entity (usually the camera), up is up on its screen
            float rotation = 0.0f;
            if (!move.RelativeTo.empty())
                if (const Transform* reference = ctx.Scene.FindEntity(move.RelativeTo).TryGet<Transform>())
                    rotation = reference->Rotation;
            const float c = cosf(rotation), s = sinf(rotation);

            glm::vec2 dir = glm::normalize(direction);
            dir = { dir.x * c - dir.y * s, dir.x * s + dir.y * c };

            const float speed = move.Speed * (running ? move.RunMultiplier : 1.0f);
            transform->Position += dir * speed * (float)ctx.Dt;
        }

    }

    void RegisterBuiltinBehaviors(ComponentRegistry& registry)
    {
        registry.Register<KeyboardMove>("KeyboardMove", "Behavior: moves the entity's Transform with the keyboard (W A S D and/or the arrow keys), faster while the run key is held. Diagonal movement is as fast as straight. Put ClampPosition after it on the same entity to keep it inside an area.")
            .Field(&KeyboardMove::Speed, "speed", "World units per second.").Range(0, 100000)
            .Field(&KeyboardMove::RunMultiplier, "runMultiplier", "How many times faster while the run key is held.").Range(0, 100)
            .Field(&KeyboardMove::RunKey, "runKey", "The key that runs; empty for no run key.").OneOf(KeyNamesOrNone())
            .Field(&KeyboardMove::Keys, "keys", "Which keys move it: wasd, arrows or both.").OneOf({ "wasd", "arrows", "both" })
            .Field(&KeyboardMove::RelativeTo, "relativeTo", "An entity (usually the camera) whose rotation turns the movement, so W goes up its screen; empty for the world's own axes. If that entity comes later in the file, its rotation is from the previous step.").EntityReference()
            .Field(&KeyboardMove::TintWhenRunning, "tintWhenRunning", "Whether to set this entity's SpriteRenderer tint to runTint while running and idleTint otherwise.")
            .Field(&KeyboardMove::IdleTint, "idleTint", "The tint [r, g, b, a] when not running, used only when tintWhenRunning is true: set it to the sprite's normal tint.").Range(0, 1)
            .Field(&KeyboardMove::RunTint, "runTint", "The tint [r, g, b, a] while running, used only when tintWhenRunning is true.").Range(0, 1)
            .Behavior(MoveWithKeys);

        registry.Register<KeyboardRotate>("KeyboardRotate", "Behavior: turns the entity's Transform with two keys.")
            .Field(&KeyboardRotate::Speed, "speed", "Radians per second.").Range(0, 100)
            .Field(&KeyboardRotate::CounterClockwiseKey, "counterClockwiseKey", "The key that turns it counter-clockwise.").OneOf(KeyNames())
            .Field(&KeyboardRotate::ClockwiseKey, "clockwiseKey", "The key that turns it clockwise; with both keys held it does not turn.").OneOf(KeyNames())
            .Behavior([](KeyboardRotate& rotate, BehaviorContext& ctx) {
                Transform* transform = ctx.Self.TryGet<Transform>();
                if (!transform) return;
                // The net turn is worked out first, so both keys together are exactly no turn
                float turn = 0.0f;
                if (ctx.Input.IsDown(KeyOf(rotate.CounterClockwiseKey))) turn += rotate.Speed * (float)ctx.Dt;
                if (ctx.Input.IsDown(KeyOf(rotate.ClockwiseKey))) turn -= rotate.Speed * (float)ctx.Dt;
                transform->Rotation += turn;
            });

        registry.Register<WheelZoom>("WheelZoom", "Behavior: zooms the entity's Camera with the mouse wheel, within a range. The zoom is only changed (and held to the range) when the wheel moves.")
            .Field(&WheelZoom::Factor, "factor", "The zoom is multiplied by this for each wheel notch up, and divided by it for each notch down.").Range(0.01, 10)
            .Field(&WheelZoom::MinZoom, "minZoom", "The smallest zoom.").Range(0.01, 100)
            .Field(&WheelZoom::MaxZoom, "maxZoom", "The largest zoom; not smaller than minZoom.").Range(0.01, 100)
            .Validate([](const WheelZoom& zoom, std::vector<ValidationError>& errors) {
                if (zoom.MaxZoom < zoom.MinZoom)
                    errors.push_back({ "/maxZoom", "maxZoom (" + Json(zoom.MaxZoom).dump() + ") must not be smaller than minZoom (" + Json(zoom.MinZoom).dump() + ")" });
            })
            .Behavior([](WheelZoom& zoom, BehaviorContext& ctx) {
                if (ctx.Input.Scroll == 0.0f) return;
                if (Camera* camera = ctx.Self.TryGet<Camera>())
                    camera->Zoom = glm::clamp(camera->Zoom * powf(zoom.Factor, ctx.Input.Scroll), zoom.MinZoom, zoom.MaxZoom);
            });

        registry.Register<ClampPosition>("ClampPosition", "Behavior: keeps the entity's position inside the rectangle from min to max. Put it after whatever moves the entity (such as KeyboardMove) on the same entity.")
            .Field(&ClampPosition::Min, "min", "The lower left corner [x, y], in world units.")
            .Field(&ClampPosition::Max, "max", "The upper right corner [x, y], in world units; not smaller than min on either axis.")
            .Validate([](const ClampPosition& clamp, std::vector<ValidationError>& errors) {
                for (int axis = 0; axis < 2; axis++)
                    if (clamp.Max[axis] < clamp.Min[axis])
                        errors.push_back({ "/max/" + std::to_string(axis), std::string("max ") + (axis ? "y" : "x") + " (" + Json(clamp.Max[axis]).dump() + ") must not be smaller than min " + (axis ? "y" : "x") + " (" + Json(clamp.Min[axis]).dump() + ")" });
            })
            .Behavior([](ClampPosition& clamp, BehaviorContext& ctx) {
                if (Transform* transform = ctx.Self.TryGet<Transform>())
                    transform->Position = glm::clamp(transform->Position, clamp.Min, clamp.Max);
            });
    }

}
