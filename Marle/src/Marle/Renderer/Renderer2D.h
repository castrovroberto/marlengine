#pragma once

#include "../Core.h"
#include "../Platform/OpenGL/OpenGLTexture.h"
#include "OrthographicCamera.h"
#include "SubTexture2D.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>

namespace Marle {
    
    // Batched 2D quad renderer. Quads are collected between BeginScene() and EndScene()
    // and submitted in as few draw calls as possible (at most 16 distinct textures per call).
    // Positions are quad centers in world space. BeginScene(w, h) uses pixel space with the origin
    // at the bottom-left; BeginScene(camera) uses the camera's view and projection.
    // Rotations are in radians, counter-clockwise positive, about the quad center.
    class EXPORT Renderer2D {
    public:
        struct Statistics {
            uint32_t DrawCalls = 0;
            uint32_t QuadCount = 0;
        };

        static void Init();
        static void Shutdown();

        static void BeginScene(const OrthographicCamera& camera);
        static void BeginScene(float viewportWidth, float viewportHeight); // pixel space, no camera
        static void EndScene();

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
