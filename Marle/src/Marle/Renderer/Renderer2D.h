#pragma once

#include "../Core.h"
#include "../Platform/OpenGL/OpenGLTexture.h"
#include "OrthographicCamera.h"
#include "SubTexture2D.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>

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

        // Accumulated since the last ResetStats()
        static Statistics GetStats();
        static void ResetStats();

    private:
        struct RendererData;
        static void Flush();
        static void NextBatch();
        static void SubmitQuad(const glm::vec2& position, const glm::vec2& size, float rotation, uint32_t textureID, const glm::vec2* texCoords, const glm::vec4& color);

        static std::unique_ptr<RendererData> s_Data;
    };

} 
