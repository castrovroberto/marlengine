#pragma once

#include "Marle/Core.h"
#include "SceneState.h"
#include <memory>
#include <string>
#include <string_view>

namespace Marle {

    class Scene;

    // A named thing in a Scene, made of components. A small copyable handle: it does not own the
    // entity. A handle goes invalid when its entity is destroyed or its scene is gone, and every
    // operation on an invalid handle is safe: queries answer "no", and the rest log an error.
    // Components are plain data and must be default constructible.
    class EXPORT Entity
    {
        public:
            Entity() = default;     // an invalid handle

            bool IsValid() const;
            explicit operator bool() const { return IsValid(); }

            // The name this entity was created with (unique within its scene); empty if invalid.
            std::string GetName() const;

            template<typename T> bool Has() const;

            // nullptr if the handle is invalid or the entity has no T. The pointer is only valid
            // until a component is added to or removed from this entity's scene.
            template<typename T> T* TryGet();
            template<typename T> const T* TryGet() const;

            // For when the entity is known to have a T. If it doesn't (or the handle is invalid)
            // the error is logged and a scratch T is returned that is not stored on anything.
            template<typename T> T& Get();

            // Constructs a T on the entity and returns it. If the entity already has a T, the error
            // is logged and the existing one is returned unchanged; on an invalid handle a scratch
            // T is returned that is not stored on anything.
            template<typename T, typename... Args> T& Add(Args&&... args);

            // True if a T was removed.
            template<typename T> bool Remove();

            bool operator==(const Entity& other) const;
            bool operator!=(const Entity& other) const { return !(*this == other); }

        private:
            friend class Scene;
            Entity(const std::shared_ptr<detail::SceneState>& state, entt::entity handle)
                : m_State(state), m_Handle(handle) {}

            // The shared state if this handle still points at a live entity, else null
            std::shared_ptr<detail::SceneState> Lock() const;

            std::weak_ptr<detail::SceneState> m_State;
            entt::entity m_Handle = entt::null;
    };

    template<typename T> bool Entity::Has() const
    {
        auto state = Lock();
        return state && state->Registry.all_of<T>(m_Handle);
    }

    template<typename T> T* Entity::TryGet()
    {
        auto state = Lock();
        return state ? state->Registry.try_get<T>(m_Handle) : nullptr;
    }

    template<typename T> const T* Entity::TryGet() const
    {
        auto state = Lock();
        return state ? std::as_const(state->Registry).template try_get<T>(m_Handle) : nullptr;
    }

    template<typename T> T& Entity::Get()
    {
        if (T* component = TryGet<T>())
            return *component;

        detail::ReportEntityMisuse("Get", GetName(), entt::type_name<T>::value(), IsValid() ? "the entity has no such component" : "the entity handle is invalid");
        static T scratch;
        scratch = T();
        return scratch;
    }

    template<typename T, typename... Args> T& Entity::Add(Args&&... args)
    {
        auto state = Lock();
        if (!state) {
            detail::ReportEntityMisuse("Add", GetName(), entt::type_name<T>::value(), "the entity handle is invalid");
            static T scratch;
            scratch = T();
            return scratch;
        }
        if (T* existing = state->Registry.try_get<T>(m_Handle)) {
            detail::ReportEntityMisuse("Add", GetName(), entt::type_name<T>::value(), "the entity already has this component");
            return *existing;
        }
        return state->Registry.emplace<T>(m_Handle, std::forward<Args>(args)...);
    }

    template<typename T> bool Entity::Remove()
    {
        auto state = Lock();
        return state && state->Registry.remove<T>(m_Handle) > 0;
    }

}
