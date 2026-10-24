#pragma once

#include "../Core.h"
#include "../Platform/OpenGL/OpenGLTexture.h"
#include "BitmapFont.h"
#include "OrthographicCamera.h"
#include "SubTexture2D.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <string>

namespace Marle {
    
    // Batched 2D quad renderer. Quads are collected between BeginBatch() and EndBatch()
    // and submitted in as few draw calls as possible (at most 16 distinct textures per call).
    // Positions are quad centers in world space. BeginBatch(w, h) uses pixel space with the origin
    // at the bottom-left; BeginBatch(camera) uses the camera's view and projection.
    // Quads are alpha-blended (texture alpha * tint alpha) in the order they are submitted, so draw
    // back to front. Textures are straight (non-premultiplied) alpha.
    // Rotations are in radians, counter-clockwise positive, about the quad center.
    class EXPORT Renderer2D {
    public:
        struct Statistics {
            uint32_t DrawCalls = 0;
            uint32_t QuadCount = 0;
        };

        static void Init();
        static void Shutdown();

        static void BeginBatch(const OrthographicCamera& camera);
        static void BeginBatch(float viewportWidth, float viewportHeight); // pixel space, no camera
        static void EndBatch();

        static void DrawQuad(const glm::vec2& position, const glm::vec2& size, OpenGLTexture2D* texture);
        static void DrawQuad(const glm::vec2& position, const glm::vec2& size, OpenGLTexture2D* texture, const glm::vec4& tint);
        static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color);

        static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const SubTexture2D& subTexture);
        static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const SubTexture2D& subTexture, const glm::vec4& tint);

        static void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const SubTexture2D& subTexture);
        static void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const SubTexture2D& subTexture, const glm::vec4& tint);
        static void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, OpenGLTexture2D* texture);
        static void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, OpenGLTexture2D* texture, const glm::vec4& tint);
        static void DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const glm::vec4& color);

        // A straight line from `from` to `to`, `thickness` wide (world units, or pixels in pixel space),
        // centered on the line, with square ends. A zero-length line draws nothing. One flat quad.
        static void DrawLine(const glm::vec2& from, const glm::vec2& to, const glm::vec4& color, float thickness = 1.0f);

        // A circle around `center`, anti-aliased by a shader that tests the distance from the center (one quad).
        // With `thickness` 0 it is a filled disk; above 0 it is a ring that wide (world units, or pixels in
        // pixel space) just inside `radius`, and a thickness of `radius` or more is the same as a disk.
        // Drawn in submission order like any quad, and counted as one in GetStats().
        static void DrawCircle(const glm::vec2& center, float radius, const glm::vec4& color, float thickness = 0.0f);

        // Draws text left to right as one quad per glyph (spaces draw nothing). `position` is the
        // bottom-left corner of the first glyph cell and `glyphHeight` the height of a line in world
        // units; '\n' starts a new line below, and a character the font lacks draws as '?'.
        static void DrawString(const BitmapFont& font, const std::string& text, const glm::vec2& position, float glyphHeight, const glm::vec4& color = glm::vec4(1.0f));

        // Accumulated since the last ResetStats()
        static Statistics GetStats();
        static void ResetStats();

    private:
        struct RendererData;
        static void Flush();
        static void NextBatch();
        static void SubmitQuad(const glm::vec2& position, const glm::vec2& size, float rotation, uint32_t textureID, const glm::vec2* texCoords, const glm::vec4& color, float circle = 0.0f);

        static std::unique_ptr<RendererData> s_Data;
    };

} 
