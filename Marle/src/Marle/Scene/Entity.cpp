#include "mrlpch.h"
#include "Marle/Log.h"
#include "Entity.h"

namespace Marle {

    namespace detail {
        void ReportEntityMisuse(const char* operation, const std::string& entity, std::string_view component, const char* problem)
        {
            MRL_CORE_ERROR("Entity::{}<{}> on {}: {}", operation, std::string(component), entity.empty() ? "an invalid entity" : "'" + entity + "'", problem);
        }
    }

    std::shared_ptr<detail::SceneState> Entity::Lock() const
    {
        auto state = m_State.lock();
        if (state && state->Registry.valid(m_Handle))
            return state;
        return nullptr;
    }

    bool Entity::IsValid() const
    {
        return Lock() != nullptr;
    }

    std::string Entity::GetName() const
    {
        auto state = Lock();
        return state ? state->Registry.get<detail::EntityName>(m_Handle).Value : std::string();
    }

    bool Entity::operator==(const Entity& other) const
    {
        return m_Handle == other.m_Handle && !m_State.owner_before(other.m_State) && !other.m_State.owner_before(m_State);
    }

}
