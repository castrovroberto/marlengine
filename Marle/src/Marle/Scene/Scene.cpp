#include "mrlpch.h"
#include "Marle/Log.h"
#include "Scene.h"
#include "Components.h"
#include <algorithm>

namespace Marle {

    Scene::Scene(std::string name)
        : m_Name(std::move(name)), m_State(std::make_shared<detail::SceneState>())
    {
        m_State->SceneEntity = m_State->Registry.create();
        m_State->Registry.emplace<detail::EntityName>(m_State->SceneEntity, detail::EntityName{ "(scene)" });
    }

    Scene::~Scene() = default;   // dropping the state expires every Entity's weak reference

    Entity Scene::CreateEntity(const std::string& name)
    {
        if (name.empty()) {
            MRL_CORE_ERROR("Scene '{}': cannot create an entity with an empty name", m_Name);
            return Entity();
        }
        if (m_State->ByName.count(name)) {
            MRL_CORE_ERROR("Scene '{}': an entity named '{}' already exists", m_Name, name);
            return Entity();
        }

        entt::entity handle = m_State->Registry.create();
        m_State->Registry.emplace<detail::EntityName>(handle, detail::EntityName{ name });
        m_State->ByName.emplace(name, handle);
        m_State->Order.push_back(handle);
        return Entity(m_State, handle);
    }

    int Scene::GetSeed() const
    {
        const Random* seed = GetSceneEntity().TryGet<Random>();
        return seed ? seed->Seed : 1;
    }

    void Scene::ResetRandom()
    {
        // a negative seed is the number it wraps to as a 32-bit integer, so every int is a different seed
        m_Random.Seed((uint64_t)(uint32_t)GetSeed());
    }

    void Scene::UpdateOverlaps(std::vector<Overlap> touching)
    {
        auto contains = [](const std::vector<Overlap>& list, const Overlap& pair) { return std::find(list.begin(), list.end(), pair) != list.end(); };

        SceneOverlaps next;
        for (const Overlap& pair : touching)
            (contains(m_Overlaps.Touching, pair) ? next.Staying : next.Entered).push_back(pair);
        for (const Overlap& pair : m_Overlaps.Touching)
            if (!contains(touching, pair)) next.Left.push_back(pair);
        next.Touching = std::move(touching);
        m_Overlaps = std::move(next);
    }

    Entity Scene::GetSceneEntity() const
    {
        return Entity(m_State, m_State->SceneEntity);
    }

    Entity Scene::FindEntity(const std::string& name) const
    {
        auto it = m_State->ByName.find(name);
        return it == m_State->ByName.end() ? Entity() : Entity(m_State, it->second);
    }

    bool Scene::DestroyEntity(Entity entity)
    {
        if (entity.Lock() != m_State)
            return false;
        if (entity.m_Handle == m_State->SceneEntity) {
            MRL_CORE_ERROR("Scene '{}': the scene's own entity cannot be destroyed", m_Name);
            return false;
        }

        m_State->ByName.erase(entity.GetName());
        m_State->Order.erase(std::find(m_State->Order.begin(), m_State->Order.end(), entity.m_Handle));
        m_State->Registry.destroy(entity.m_Handle);
        return true;
    }

    size_t Scene::GetEntityCount() const
    {
        return m_State->Order.size();
    }

    std::vector<Entity> Scene::GetEntities() const
    {
        std::vector<Entity> entities;
        entities.reserve(m_State->Order.size());
        for (entt::entity handle : m_State->Order)
            entities.push_back(Entity(m_State, handle));
        return entities;
    }

}
