#pragma once

#include "Marle/Core.h"
#include "Entity.h"
#include "RandomGenerator.h"
#include <memory>
#include <string>
#include <vector>

namespace Marle {

    // One trigger collider overlapping one other collider (solid or trigger), by entity names (ADR-0003).
    struct Overlap {
        std::string Trigger;   // the entity with the trigger collider
        std::string Other;     // the entity it overlaps
        bool operator==(const Overlap& o) const { return Trigger == o.Trigger && Other == o.Other; }
    };

    // What overlapped at the end of the last simulation step. Pairs are sorted by the creation order of the
    // trigger, then of the other entity. Two triggers that overlap are listed both ways round.
    struct SceneOverlaps {
        std::vector<Overlap> Touching;   // every pair that overlaps now
        std::vector<Overlap> Entered;    // in Touching, and not in the previous step
        std::vector<Overlap> Staying;    // in Touching, and in the previous step too
        std::vector<Overlap> Left;       // not in Touching, and in the previous step (in its order); includes pairs
                                         // whose entity has since been destroyed
    };

    // A named collection of entities. Entity names are unique within a scene. Entities are stored
    // in entt, which clients never see. Destroying the scene invalidates every Entity handle made
    // from it. Don't create or destroy entities from inside ForEach.
    class EXPORT Scene
    {
        public:
            explicit Scene(std::string name = "Untitled");
            ~Scene();

            // A scene owns its entities: copying would have to duplicate them.
            Scene(const Scene&) = delete;
            Scene& operator=(const Scene&) = delete;

            const std::string& GetName() const { return m_Name; }

            // A new entity with this name. An empty name or one already in use is logged and gives
            // an invalid Entity; the scene is unchanged.
            Entity CreateEntity(const std::string& name);

            // The scene's own entity, which carries components that belong to the scene as a whole (ADR-0002),
            // such as a random seed. It is not one of the scene's entities: it has no name to look up
            // (GetName() gives "(scene)"), is not in GetEntities() or GetEntityCount(), is skipped by
            // ForEach and cannot be destroyed.
            Entity GetSceneEntity() const;

            // The scene's random number generator (ADR-0002): the only source of random numbers a behavior
            // may use. It starts from the seed in the scene's own Random component, or seed 1 if there is
            // none, and loading a scene starts it there. It is runtime state: a saved scene restarts its
            // sequence from the seed.
            RandomGenerator& GetRandom() { return m_Random; }
            const RandomGenerator& GetRandom() const { return m_Random; }

            // Restarts the generator from the seed in the scene's own Random component (seed 1 if none)
            void ResetRandom();

            // The seed in the scene's own Random component, or 1 if it has none
            int GetSeed() const;

            // A behavior cannot reload a scene, so one that wants a restart asks (RestartOnKey does), and the
            // host decides what restarting means (the Sandbox loads the scene from its file again). It is
            // runtime state: not in a scene file and not in a checksum.
            void RequestRestart() { m_RestartRequested = true; }
            bool IsRestartRequested() const { return m_RestartRequested; }
            void ClearRestartRequest() { m_RestartRequested = false; }

            // Which triggers overlap what, as of the end of the last Simulation::Step, so a behavior sees what
            // happened in the step before. Empty for a scene that has not stepped. Runtime state, worked out
            // again from positions every step: it is not in a scene file or a checksum (ADR-0003).
            const SceneOverlaps& GetOverlaps() const { return m_Overlaps; }

            // Replaces the overlaps with the pairs now touching, sorted as SceneOverlaps says, and works out
            // Entered, Staying and Left against the ones it had. For Simulation::Step.
            void UpdateOverlaps(std::vector<Overlap> touching);

            // The entity with this name, or an invalid Entity.
            Entity FindEntity(const std::string& name) const;

            // False (and nothing happens) if the handle is invalid or belongs to another scene.
            // Afterwards the handle, and any copies of it, are invalid and the name can be reused.
            bool DestroyEntity(Entity entity);

            size_t GetEntityCount() const;

            // Every entity, in creation order.
            std::vector<Entity> GetEntities() const;

            // Calls fn(Entity, Components&...) for each entity that has all the listed components,
            // in no particular order; use GetEntities() when order matters. The scene's own entity is not visited.
            template<typename... Components, typename Fn>
            void ForEach(Fn&& fn);

        private:
            std::string m_Name;
            std::shared_ptr<detail::SceneState> m_State;
            RandomGenerator m_Random;
            bool m_RestartRequested = false;
            SceneOverlaps m_Overlaps;
    };

    template<typename... Components, typename Fn>
    void Scene::ForEach(Fn&& fn)
    {
        auto view = m_State->Registry.template view<Components...>();
        for (auto handle : view)
            if (handle != m_State->SceneEntity)
                fn(Entity(m_State, handle), view.template get<Components>(handle)...);
    }

}
