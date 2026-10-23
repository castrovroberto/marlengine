#pragma once

#include <entt/entt.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Marle {
namespace detail {

    // Shared by a Scene and the Entity handles made from it. Entities hold it weakly, so a handle
    // notices when its scene is gone. Not part of the client API: clients never see entt.
    struct SceneState {
        entt::registry Registry;
        std::unordered_map<std::string, entt::entity> ByName;
        std::vector<entt::entity> Order;    // creation order, for deterministic iteration and saving
        entt::entity SceneEntity = entt::null;   // carries the scene's own components; not in ByName or Order
    };

    struct EntityName { std::string Value; };   // every entity has one, kept as a component

    // Logs a misuse of an Entity handle; `entity` is its name, or empty for an invalid handle.
    void ReportEntityMisuse(const char* operation, const std::string& entity, std::string_view component, const char* problem);

}
}
