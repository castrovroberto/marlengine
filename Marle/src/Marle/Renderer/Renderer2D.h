#pragma once

#include "../Platform/OpenGL/OpenGLShader.h"
#include "../Platform/OpenGL/OpenGLTexture.h"
#include <glm/glm.hpp>
#include <memory>

namespace Marle {
    
    class Renderer2D {
    public:
        static void Init();
        static void Shutdown();

        static void BeginScene();
        static void EndScene();

        static void DrawQuad(const glm::vec2& position, const glm::vec2& size, OpenGLTexture2D* texture);

    private:
        struct QuadVertex {
            glm::vec3 Position;
            glm::vec2 TexCoord;
        };

        struct RendererData {
            GLuint QuadVAO = 0;
            GLuint QuadVBO = 0;
            GLuint QuadEBO = 0;
            std::unique_ptr<OpenGLShader> TextureShader;
        };

        static std::unique_ptr<RendererData> s_Data;
    };

} 