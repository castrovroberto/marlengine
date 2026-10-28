#include "mrlpch.h"
#include "Simulation.h"
#include "Components.h"
#include "Collision.h"

namespace Marle {

    uint64_t Fnv1a64(const void* data, size_t size)
    {
        uint64_t hash = 14695981039346656037ull;
        const unsigned char* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; i++) {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    const char* SimulationEventKindName(SimulationEvent::Kind kind)
    {
        switch (kind) {
            case SimulationEvent::Kind::TriggerEntered: return "trigger_entered";
            case SimulationEvent::Kind::TriggerLeft:    return "trigger_left";
            case SimulationEvent::Kind::SolidStopped:   return "solid_stopped";
        }
        return "unknown";
    }

    void Simulation::Step(const ComponentRegistry& registry, Scene& scene, const InputFrame& input, double dt, std::vector<SimulationEvent>* events)
    {
        // The entities as they are now; ones made during the step wait for the next
        std::vector<Entity> entities = scene.GetEntities();

        for (Entity entity : entities) {
            const Transform* transform = entity.TryGet<Transform>();
            if (!transform) continue;
            PreviousTransform previous;
            previous.Position = transform->Position;
            previous.Rotation = transform->Rotation;
            previous.Scale = transform->Scale;
            if (PreviousTransform* existing = entity.TryGet<PreviousTransform>()) *existing = previous;
            else entity.Add<PreviousTransform>() = previous;
        }

        BehaviorContext context{ scene, scene.GetSceneEntity(), input, scene.GetRandom(), dt };
        registry.RunBehaviors(scene.GetSceneEntity(), context);
        for (Entity entity : entities) {
            if (!entity.IsValid()) continue;   // destroyed by an earlier behavior
            context.Self = entity;
            registry.RunBehaviors(entity, context);
            const SolidMove stopped = ResolveSolidMove(scene, entity);   // right after the entity's own behaviors, so what follows sees where it ended up
            if (events && (stopped.StoppedX || stopped.StoppedY))
                events->push_back({ SimulationEvent::Kind::SolidStopped, entity.GetName(), "", std::string(stopped.StoppedX ? "x" : "") + (stopped.StoppedY ? "y" : "") });
        }

        FindOverlaps(scene);   // once every entity is where it ends up; behaviors see this on the next step

        if (events) {
            for (const Overlap& pair : scene.GetOverlaps().Entered) events->push_back({ SimulationEvent::Kind::TriggerEntered, pair.Trigger, pair.Other, "" });
            for (const Overlap& pair : scene.GetOverlaps().Left) events->push_back({ SimulationEvent::Kind::TriggerLeft, pair.Trigger, pair.Other, "" });
        }
    }

    uint64_t Simulation::Checksum(const ComponentRegistry& registry, const Scene& scene)
    {
        uint64_t hash = 14695981039346656037ull;
        // Each piece is followed by a byte that cannot be part of the text, so "ab" "c" and "a" "bc" differ
        auto mix = [&](const std::string& text) {
            for (unsigned char byte : text) { hash ^= byte; hash *= 1099511628211ull; }
            hash ^= 0xFFu; hash *= 1099511628211ull;
        };
        auto mixComponents = [&](const Entity& entity) {
            for (const std::string& component : registry.GetComponentNames(entity)) {
                mix(component);
                mix(registry.Write(entity, component).dump());
            }
        };

        const RandomGenerator::State random = scene.GetRandom().GetState();
        mix("random");
        mix(std::to_string(random.Value) + "," + std::to_string(random.Increment));

        mix("scene");
        mixComponents(scene.GetSceneEntity());
        for (const Entity& entity : scene.GetEntities()) {
            mix("entity");
            mix(entity.GetName());
            mixComponents(entity);
        }
        return hash;
    }

    std::string Simulation::ChecksumText(uint64_t checksum)
    {
        static const char* digits = "0123456789abcdef";
        std::string text(16, '0');
        for (int i = 15; i >= 0; i--, checksum >>= 4) text[i] = digits[checksum & 0xF];
        return text;
    }

}
