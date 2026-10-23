#pragma once

#include "Marle/Core.h"
#include "Entity.h"
#include <memory>
#include <string>
#include <vector>

namespace Marle {

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

            // The entity with this name, or an invalid Entity.
            Entity FindEntity(const std::string& name) const;

            // False (and nothing happens) if the handle is invalid or belongs to another scene.
            // Afterwards the handle, and any copies of it, are invalid and the name can be reused.
            bool DestroyEntity(Entity entity);

            size_t GetEntityCount() const;

            // Every entity, in creation order.
            std::vector<Entity> GetEntities() const;

            // Calls fn(Entity, Components&...) for each entity that has all the listed components,
            // in no particular order; use GetEntities() when order matters.
            template<typename... Components, typename Fn>
            void ForEach(Fn&& fn);

        private:
            std::string m_Name;
            std::shared_ptr<detail::SceneState> m_State;
    };

    template<typename... Components, typename Fn>
    void Scene::ForEach(Fn&& fn)
    {
        auto view = m_State->Registry.template view<Components...>();
        for (auto handle : view)
            fn(Entity(m_State, handle), view.template get<Components>(handle)...);
    }

}
