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

    // Behaviors that read the keyboard and mouse wheel through the step's InputFrame (see BuiltinBehaviors.cpp).

    // Moves the entity's Transform with the keyboard: W A S D and/or the arrow keys, at a speed in world
    // units per second, faster while the run key is held. Diagonals are not faster than straight lines.
    struct KeyboardMove {
        float Speed = 100.0f;                       // world units per second
        float RunMultiplier = 2.0f;                 // speed factor while the run key is held
        std::string RunKey = "LeftShift";           // a key name, or empty for no run key
        std::string Keys = "both";                  // "wasd", "arrows" or "both"
        std::string RelativeTo;                     // an entity: movement is turned by its rotation (W is up its screen); empty = world axes
        bool TintWhenRunning = false;               // set this entity's SpriteRenderer tint to RunTint while running, IdleTint otherwise
        glm::vec4 IdleTint = { 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec4 RunTint = { 1.0f, 0.65f, 0.3f, 1.0f };
    };

    // Turns the entity's Transform with two keys.
    struct KeyboardRotate {
        float Speed = 1.0f;                         // radians per second
        std::string CounterClockwiseKey = "Q";
        std::string ClockwiseKey = "E";
    };

    // Zooms the entity's Camera with the mouse wheel.
    struct WheelZoom {
        float Factor = 1.1f;                        // zoom is multiplied by this for each wheel notch up
        float MinZoom = 0.25f;
        float MaxZoom = 4.0f;
    };

    // Behaviors that move an entity by themselves.

    // Puts the entity at another entity's position plus an offset, every step. It does nothing while the
    // target does not exist, has no Transform, or is the entity itself.
    struct Follow {
        std::string Target;                         // an entity
        glm::vec2 Offset = { 0.0f, 0.0f };          // world units
    };

    // Turns the entity's Transform at a steady rate, starting from the rotation it has.
    struct Spin {
        float Speed = 1.0f;                         // radians per second, counter-clockwise when positive
    };

    // Moves the entity around another entity in a circle, and can turn it as it goes. Angle is where on
    // the circle it is now (scene data, so a saved scene holds it); set it to start somewhere else.
    struct Orbit {
        std::string Target;                         // an entity; it does nothing while that does not exist or is itself
        float Radius = 100.0f;                      // world units
        float Speed = 1.0f;                         // radians per second around the target
        float Angle = 0.0f;                         // radians, 0 is to the right of the target
        float RotationSpeed = 0.0f;                 // radians per second the entity itself turns
    };

    // Walks in a random direction, picking a new one now and then from the scene's random numbers.
    // Heading and Timer are its current state (scene data).
    struct Wander {
        float Speed = 50.0f;                        // world units per second
        float ChangeEvery = 1.0f;                   // seconds between new directions
        float Heading = 0.0f;                       // radians, the direction it is walking
        float Timer = 0.0f;                         // seconds until the next new direction; 0 picks one on the first step
    };

    // Keeps the entity's position inside a rectangle.
    struct ClampPosition {
        glm::vec2 Min = { 0.0f, 0.0f };
        glm::vec2 Max = { 100.0f, 100.0f };
    };

    // Views the scene from the entity's Transform.
    struct Camera {
        float Zoom = 1.0f;                     // larger shows less of the world
        bool Primary = false;                  // the camera the scene is drawn with
    };

}
