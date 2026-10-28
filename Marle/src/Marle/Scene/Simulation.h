#pragma once

#include "Marle/Core.h"
#include "ComponentRegistry.h"
#include "Scene.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Marle {

    // 64-bit FNV-1a, the hash behind the scene checksum (stable, simple, and the same everywhere)
    EXPORT uint64_t Fnv1a64(const void* data, size_t size);

    // Something the engine saw happen during one step, for tools and tests that want to know what a run did
    // and not only where it ended (`marle-scene simulate --report`). Collecting events only reads what the
    // step already worked out: it changes no component and no checksum.
    struct SimulationEvent {
        enum class Kind {
            TriggerEntered,   // `Entity` is the trigger, `Other` what started touching it
            TriggerLeft,      // `Entity` is the trigger, `Other` what stopped touching it
            SolidStopped,     // `Entity` moved and was cut short by a solid collider or tile; `Axis` is "x", "y" or "xy"
        } What;
        std::string Entity;
        std::string Other;
        std::string Axis;
    };

    // "trigger_entered", "trigger_left" or "solid_stopped"
    EXPORT const char* SimulationEventKindName(SimulationEvent::Kind kind);

    // Runs a scene's behaviors one fixed step at a time (ADR-0002). It holds no state of its own: everything
    // that changes lives in the scene, so a scene can be saved, loaded and carried on, and two scenes in the
    // same state step the same way. It needs no window or GL context, and it never looks at a clock.
    class EXPORT Simulation
    {
        public:
            // One fixed step of `dt` seconds. First it records where each entity's Transform is, as its
            // PreviousTransform (for SceneRenderer to draw between steps). Then it runs behaviors: the
            // scene's own components first, then each entity in the order of the scene file, and within
            // an entity its behaviors in registration order, followed by stopping it at solid colliders and
            // solid tiles if it moved (ADR-0003, ResolveSolidMove). When every entity has run, the scene's
            // overlaps are worked out (FindOverlaps; Scene::GetOverlaps). An entity created during the step first runs
            // on the next one; one destroyed during the step is skipped.
            //
            // With `events`, what happened is appended to it: first each SolidStopped, in the order the entities
            // ran, then each TriggerEntered and TriggerLeft in the order of Scene::GetOverlaps(). A trigger that
            // keeps touching something is not an event on each step; it is the stretch between Entered and Left.
            static void Step(const ComponentRegistry& registry, Scene& scene, const InputFrame& input, double dt, std::vector<SimulationEvent>* events = nullptr);

            // A hash of the whole state of the scene: every component the registry knows, written through
            // the registry, for the scene itself and for each entity in order (with its name), plus the
            // position in the scene's random sequence. Two runs that give the same checksum ended in the
            // same state. Runtime-only data such as the previous Transform is not part of it.
            static uint64_t Checksum(const ComponentRegistry& registry, const Scene& scene);

            // A checksum as 16 lower-case hex digits
            static std::string ChecksumText(uint64_t checksum);
    };

}
