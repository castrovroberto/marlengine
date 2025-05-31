#include "mrlpch.h"
#include "Renderer2D.h"
#include "../Application.h"
#include <glm/gtc/matrix_transform.hpp>

namespace Marle {

    std::unique_ptr<Renderer2D::RendererData> Renderer2D::s_Data = nullptr;

    void Renderer2D::Init()
    {
        printf("Initializing Renderer2D...\n");
        
        s_Data = std::make_unique<RendererData>();

        // Create and load shader
        s_Data->TextureShader = std::make_unique<OpenGLShader>(
            "Assets/Shaders/Texture.vert", 
            "Assets/Shaders/Texture.frag"
        );

        // Define quad vertices (unit quad: -0.5 to 0.5)
        float vertices[] = {
            // Positions        // TexCoords
            -0.5f, -0.5f, 0.0f,  0.0f, 0.0f, // Bottom-left
             0.5f, -0.5f, 0.0f,  1.0f, 0.0f, // Bottom-right
             0.5f,  0.5f, 0.0f,  1.0f, 1.0f, // Top-right
            -0.5f,  0.5f, 0.0f,  0.0f, 1.0f  // Top-left
        };

        unsigned int indices[] = {
            0, 1, 2, // First triangle
            2, 3, 0  // Second triangle
        };

        // Generate and bind VAO
        glGenVertexArrays(1, &s_Data->QuadVAO);
        glBindVertexArray(s_Data->QuadVAO);

        // Generate and bind VBO
        glGenBuffers(1, &s_Data->QuadVBO);
        glBindBuffer(GL_ARRAY_BUFFER, s_Data->QuadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        // Generate and bind EBO
        glGenBuffers(1, &s_Data->QuadEBO);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_Data->QuadEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

        // Position attribute (location = 0)
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        // TexCoord attribute (location = 1)
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        // Unbind VAO
        glBindVertexArray(0);

        printf("Renderer2D initialized successfully\n");
    }

    void Renderer2D::Shutdown()
    {
        if (s_Data) {
            glDeleteVertexArrays(1, &s_Data->QuadVAO);
            glDeleteBuffers(1, &s_Data->QuadVBO);
            glDeleteBuffers(1, &s_Data->QuadEBO);
            s_Data.reset();
        }
        printf("Renderer2D shutdown complete\n");
    }

    void Renderer2D::BeginScene()
    {
        if (!s_Data || !s_Data->TextureShader) {
            printf("Error: Renderer2D not initialized!\n");
            return;
        }

        // Setup orthographic projection (using hard-coded window size for now)
        // TODO: Get this from Application/Window
        float windowWidth = 1024.0f;
        float windowHeight = 768.0f;

        s_Data->TextureShader->Bind();
        
        // Create orthographic projection matrix (0,0 at bottom-left)
        glm::mat4 proj = glm::ortho(0.0f, windowWidth, 0.0f, windowHeight, -1.0f, 1.0f);
        
        // View matrix (identity for now, no camera movement)
        glm::mat4 view = glm::mat4(1.0f);
        glm::mat4 viewProjectionMatrix = proj * view;
        
        s_Data->TextureShader->SetUniformMat4f("u_ViewProjection", viewProjectionMatrix);
    }

    void Renderer2D::EndScene()
    {
        if (s_Data && s_Data->TextureShader) {
            s_Data->TextureShader->Unbind();
        }
    }

    void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, OpenGLTexture2D* texture)
    {
        if (!s_Data || !s_Data->TextureShader || !texture) {
            printf("Error: Cannot draw quad - missing components\n");
            return;
        }

        // Bind texture
        texture->Bind(0);
        
        // Set texture uniform
        s_Data->TextureShader->SetUniform1i("u_Texture", 0);

        // Create transform matrix
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(position, 0.0f)) *
                              glm::scale(glm::mat4(1.0f), glm::vec3(size, 1.0f));
        
        s_Data->TextureShader->SetUniformMat4f("u_Transform", transform);

        // Draw quad
        glBindVertexArray(s_Data->QuadVAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }

} 