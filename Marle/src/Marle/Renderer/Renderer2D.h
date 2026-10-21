#pragma once

#include "../Core.h"
#include "../Platform/OpenGL/OpenGLTexture.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>

namespace Marle {
    
    // Batched 2D quad renderer. Quads are collected between BeginScene() and EndScene()
    // and submitted in as few draw calls as possible (at most 16 distinct textures per call).
    // Positions are quad centers in the pixel space given to BeginScene (origin bottom-left).
    class EXPORT Renderer2D {
    public:
        struct Statistics {
            uint32_t DrawCalls = 0;
            uint32_t QuadCount = 0;
        };

        static void Init();
        static void Shutdown();

        static void BeginScene(float viewportWidth, float viewportHeight);
        static void EndScene();

        static void DrawQuad(const glm::vec2& position, const glm::vec2& size, OpenGLTexture2D* texture);
        static void DrawQuad(const glm::vec2& position, const glm::vec2& size, OpenGLTexture2D* texture, const glm::vec4& tint);
        static void DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color);

        // Accumulated since the last ResetStats()
        static Statistics GetStats();
        static void ResetStats();

    private:
        struct RendererData;
        static void Flush();
        static void NextBatch();
        static void SubmitQuad(const glm::vec2& position, const glm::vec2& size, uint32_t textureID, const glm::vec4& color);

        static std::unique_ptr<RendererData> s_Data;
    };

} 
