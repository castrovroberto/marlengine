#pragma once

#include "Marle/Core.h"
#include "ComponentRegistry.h"
#include "Scene.h"
#include <cstdint>
#include <string>

namespace Marle {

    // 64-bit FNV-1a, the hash behind the scene checksum (stable, simple, and the same everywhere)
    EXPORT uint64_t Fnv1a64(const void* data, size_t size);

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
            // solid tiles if it moved (ADR-0003, ResolveSolidMove). An entity created during the step first runs
            // on the next one; one destroyed during the step is skipped.
            static void Step(const ComponentRegistry& registry, Scene& scene, const InputFrame& input, double dt);

            // A hash of the whole state of the scene: every component the registry knows, written through
            // the registry, for the scene itself and for each entity in order (with its name), plus the
            // position in the scene's random sequence. Two runs that give the same checksum ended in the
            // same state. Runtime-only data such as the previous Transform is not part of it.
            static uint64_t Checksum(const ComponentRegistry& registry, const Scene& scene);

            // A checksum as 16 lower-case hex digits
            static std::string ChecksumText(uint64_t checksum);
    };

}
