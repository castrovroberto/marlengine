#pragma once

#include <glm/glm.hpp>
#include <string>

namespace Marle {

    // Built-in components, registered by RegisterBuiltinComponents(). Plain data; the registry
    // describes each one's fields for scene files (name, meaning, default, range).

    // Where an entity is, how it is turned and how big it is.
    struct Transform {
        glm::vec2 Position = { 0.0f, 0.0f };   // world units
        float Rotation = 0.0f;                 // radians, counter-clockwise
        glm::vec2 Scale = { 1.0f, 1.0f };      // size in world units
    };

    // Draws the entity as a quad at its Transform: a flat color, or a texture or one cell of a sprite sheet.
    struct SpriteRenderer {
        std::string Texture;                   // image path; empty draws a flat color
        glm::ivec2 Cell = { 0, 0 };            // sprite sheet cell (column, row), row 0 at the bottom
        glm::vec2 CellSize = { 0.0f, 0.0f };   // one cell in pixels; 0 uses the whole texture
        glm::vec4 Tint = { 1.0f, 1.0f, 1.0f, 1.0f };   // multiplied with the texture, RGBA 0 to 1
    };

    // Views the scene from the entity's Transform.
    struct Camera {
        float Zoom = 1.0f;                     // larger shows less of the world
        bool Primary = false;                  // the camera the scene is drawn with
    };

}
