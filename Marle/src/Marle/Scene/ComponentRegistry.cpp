#include "mrlpch.h"
#include "Marle/Log.h"
#include "ComponentRegistry.h"
#include "Components.h"
#include <algorithm>
#include <cctype>

namespace Marle {

    const char* FieldTypeName(FieldType type)
    {
        switch (type) {
            case FieldType::Bool:   return "bool";
            case FieldType::Int:    return "int";
            case FieldType::Float:  return "float";
            case FieldType::String: return "string";
            case FieldType::Vec2:   return "vec2";
            case FieldType::IVec2:  return "ivec2";
            case FieldType::Vec4:   return "vec4";
            case FieldType::StringArray: return "string[]";
        }
        return "unknown";
    }

    namespace detail {

        static std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }

        // Edit distance between two short strings
        static size_t EditDistance(const std::string& a, const std::string& b)
        {
            std::vector<size_t> previous(b.size() + 1), current(b.size() + 1);
            for (size_t j = 0; j <= b.size(); j++) previous[j] = j;
            for (size_t i = 1; i <= a.size(); i++) {
                current[0] = i;
                for (size_t j = 1; j <= b.size(); j++)
                    current[j] = std::min({ previous[j] + 1, current[j - 1] + 1, previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1) });
                std::swap(previous, current);
            }
            return previous[b.size()];
        }

        std::string SuggestionFor(const std::string& word, const std::vector<std::string>& candidates)
        {
            const std::string* best = nullptr;
            size_t bestDistance = 3;   // at most two edits, and fewer for short words
            for (const std::string& candidate : candidates) {
                size_t distance = EditDistance(Lower(word), Lower(candidate));
                if (distance < bestDistance && distance < std::max(word.size(), candidate.size())) {
                    best = &candidate;
                    bestDistance = distance;
                }
            }
            return best ? "did you mean '" + *best + "'?" : "";
        }

        std::string Join(const std::vector<std::string>& words)
        {
            std::string joined;
            for (size_t i = 0; i < words.size(); i++)
                joined += (i ? ", " : "") + words[i];
            return joined;
        }

        void ReportRegistryError(const std::string& message)
        {
            MRL_CORE_ERROR("ComponentRegistry: {}", message);
        }

    }

    static bool IsValidName(const std::string& name)
    {
        if (name.empty() || !std::isalpha((unsigned char)name[0])) return false;
        return std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c); });
    }

    bool ComponentRegistry::Add(const std::shared_ptr<detail::ComponentImpl>& impl)
    {
        if (!IsValidName(impl->Info.Name)) {
            detail::ReportRegistryError("'" + impl->Info.Name + "' is not a valid component name (a letter, then letters and digits)");
            return false;
        }
        for (const auto& existing : m_Components) {
            if (existing->Info.Name == impl->Info.Name) {
                detail::ReportRegistryError("a component named '" + impl->Info.Name + "' is already registered");
                return false;
            }
            if (*existing->Type == *impl->Type) {
                detail::ReportRegistryError("this C++ type is already registered as '" + existing->Info.Name + "', cannot register it as '" + impl->Info.Name + "'");
                return false;
            }
        }
        m_Components.push_back(impl);
        return true;
    }

    const ComponentInfo* ComponentRegistry::Find(const std::string& name) const
    {
        for (const auto& component : m_Components)
            if (component->Info.Name == name) return &component->Info;
        return nullptr;
    }

    std::vector<std::string> ComponentRegistry::GetNames() const
    {
        std::vector<std::string> names;
        for (const auto& component : m_Components) names.push_back(component->Info.Name);
        return names;
    }

    std::vector<const ComponentInfo*> ComponentRegistry::GetComponents() const
    {
        std::vector<const ComponentInfo*> components;
        for (const auto& component : m_Components) components.push_back(&component->Info);
        return components;
    }

    std::vector<ValidationError> ComponentRegistry::Read(Entity entity, const std::string& component, const Json& data) const
    {
        std::vector<ValidationError> errors;
        for (const auto& registered : m_Components) {
            if (registered->Info.Name == component) {
                registered->Read(entity, data, errors);
                return errors;
            }
        }

        std::vector<std::string> names = GetNames();
        std::string hint = detail::SuggestionFor(component, names);
        errors.push_back({ "", "unknown component '" + component + "'" + (hint.empty() ? "" : " (" + hint + ")") + "; known components: " + detail::Join(names) });
        return errors;
    }

    Json ComponentRegistry::Write(const Entity& entity, const std::string& component, bool skipDefaults) const
    {
        for (const auto& registered : m_Components)
            if (registered->Info.Name == component) return registered->Write(entity, skipDefaults);
        return nullptr;
    }

    std::vector<std::string> ComponentRegistry::GetComponentNames(const Entity& entity) const
    {
        std::vector<std::string> names;
        for (const auto& component : m_Components)
            if (component->Has(entity)) names.push_back(component->Info.Name);
        return names;
    }

    void RegisterBuiltinComponents(ComponentRegistry& registry)
    {
        registry.Register<Transform>("Transform", "Where an entity is, how it is turned and how big it is.")
            .Field(&Transform::Position, "position", "World position [x, y], in world units.")
            .Field(&Transform::Rotation, "rotation", "Turn about the position, in radians, counter-clockwise.")
            .Field(&Transform::Scale, "scale", "Size [width, height], in world units.");

        registry.Register<SpriteRenderer>("SpriteRenderer", "Draws the entity as a quad at its Transform: a flat color, a texture, or one cell of a sprite sheet.")
            .Field(&SpriteRenderer::Texture, "texture", "Image path relative to the working directory; empty draws a flat color.").AssetPath()
            .Field(&SpriteRenderer::Cell, "cell", "Sprite sheet cell [column, row], row 0 at the bottom.").Min(0)
            .Field(&SpriteRenderer::CellSize, "cellSize", "One sprite sheet cell [width, height] in pixels; [0, 0] uses the whole texture.").Min(0)
            .Field(&SpriteRenderer::Tint, "tint", "Color [r, g, b, a], each 0 to 1, multiplied with the texture.").Range(0, 1);

        registry.Register<Camera>("Camera", "Views the scene from the entity's Transform.")
            .Field(&Camera::Zoom, "zoom", "How much to magnify the view; larger shows less of the world.").Range(0.01, 100)
            .Field(&Camera::Primary, "primary", "Whether the scene is drawn through this camera.");

        registry.Register<Tilemap>("Tilemap", "A grid of tiles cut from a sprite sheet. The entity's Transform position is the map's bottom-left corner and its scale multiplies the tile size; rotation is not used.")
            .Field(&Tilemap::Texture, "texture", "The sprite sheet image path, relative to the working directory.").AssetPath()
            .Field(&Tilemap::CellSize, "cellSize", "One sprite sheet cell [width, height] in pixels.").Min(1)
            .Field(&Tilemap::TileSize, "tileSize", "One tile [width, height] in world units.").Min(0.001)
            .Field(&Tilemap::Rows, "rows", "The tiles, one string per row, listed top to bottom as the map looks. Each character is a sprite sheet cell counted from the sheet's bottom-left cell, left to right then up, in base 36 (0-9 then a-z), or '.' for no tile. Every row must be the same length.")
            .Validate([](const Tilemap& map, std::vector<ValidationError>& errors) {
                for (size_t i = 0; i < map.Rows.size(); i++) {
                    const std::string& row = map.Rows[i];
                    for (size_t x = 0; x < row.size(); x++)
                        if (TilemapCell(row[x]) == -2) {
                            errors.push_back({ "/rows/" + std::to_string(i), std::string("'") + row[x] + "' at position " + std::to_string(x) + " is not a tile character (use 0-9, a-z, or '.' for no tile)" });
                            break;
                        }
                    if (row.size() != map.Rows[0].size())
                        errors.push_back({ "/rows/" + std::to_string(i), "this row has " + std::to_string(row.size()) + " tiles but the first row has " + std::to_string(map.Rows[0].size()) + "; every row must be the same length" });
                }
            });

        registry.Register<Random>("Random", "On the scene itself: where the scene's random numbers start, so the same seed gives the same sequence on every run. Nothing is ever seeded from the clock.")
            .Field(&Random::Seed, "seed", "Any integer. The same seed always gives the same sequence of random numbers.");
    }

}
