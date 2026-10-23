#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

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

    // Where an entity's Transform was before the last simulation step, so rendering can draw between the
    // previous and the current step. Runtime only: it is not registered, so it is never written to a
    // scene file, validated, in the schema or in a checksum.
    struct PreviousTransform {
        glm::vec2 Position = { 0.0f, 0.0f };
        float Rotation = 0.0f;
        glm::vec2 Scale = { 1.0f, 1.0f };
    };

    // A grid of tiles cut from a sprite sheet. The entity's Transform.Position is the bottom-left corner
    // of the map and Transform.Scale multiplies the tile size; rotation is not used. Rows are listed top
    // to bottom, as the map looks. Each character is one tile: a cell of the sprite sheet counted from
    // its bottom-left cell, left to right then up, in base 36 (0-9 then a-z), or '.' for no tile.
    struct Tilemap {
        std::string Texture;                          // the sprite sheet image path
        glm::vec2 CellSize = { 32.0f, 32.0f };        // one sheet cell in pixels
        glm::vec2 TileSize = { 64.0f, 64.0f };        // one tile in world units
        std::vector<std::string> Rows;
    };

    // The sheet cell a tile character stands for: 0 and up, -1 for '.' (no tile), -2 if it is not a tile character
    inline int TilemapCell(char c)
    {
        if (c == '.') return -1;
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'z') return 10 + (c - 'a');
        return -2;
    }

    // On the scene itself: the number the scene's random numbers start from. The same seed gives the
    // same sequence on every run; nothing is ever seeded from the clock.
    struct Random {
        int Seed = 1;
    };

    // Views the scene from the entity's Transform.
    struct Camera {
        float Zoom = 1.0f;                     // larger shows less of the world
        bool Primary = false;                  // the camera the scene is drawn with
    };

}
