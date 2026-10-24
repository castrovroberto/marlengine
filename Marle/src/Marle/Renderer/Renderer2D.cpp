#include "mrlpch.h"
#include "../Log.h"
#include "Renderer2D.h"
#include "../Platform/OpenGL/OpenGLShader.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

namespace Marle {

    static constexpr uint32_t MaxQuads = 10000;
    static constexpr uint32_t MaxVertices = MaxQuads * 4;
    static constexpr uint32_t MaxIndices = MaxQuads * 6;
    static constexpr uint32_t MaxTextureSlots = 16; // guaranteed by GL 3.3; slot 0 is the white texture

    // Programmer/asset errors can repeat for every quad of every frame; report each only once.
    static void ReportOnce(const char* message)
    {
        static std::unordered_set<std::string> reported;
        if (reported.insert(message).second)
            MRL_CORE_ERROR("{} (further occurrences not logged)", message);
    }

    struct QuadVertex {
        glm::vec3 Position;
        glm::vec4 Color;
        glm::vec2 TexCoord;
        float TexIndex;
        float Circle;   // 0 for a quad; for a circle, the ring width as a fraction of the radius (see Texture.frag)
    };

    struct Renderer2D::RendererData {
        GLuint QuadVAO = 0;
        GLuint QuadVBO = 0;
        GLuint QuadEBO = 0;
        GLuint WhiteTexture = 0;
        GLuint ErrorTexture = 0;   // magenta/black checker, drawn in place of missing textures
        std::unique_ptr<OpenGLShader> TextureShader;

        std::vector<QuadVertex> Vertices;   // CPU-side staging, sized MaxVertices
        uint32_t QuadCount = 0;             // quads in the current batch

        GLuint TextureSlots[MaxTextureSlots] = {};
        uint32_t TextureSlotCount = 1;

        Renderer2D::Statistics Stats;
    };

    // Full-texture UVs: bottom-left, bottom-right, top-right, top-left
    static const glm::vec2 s_FullTexCoords[4] = { {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f} };

    std::unique_ptr<Renderer2D::RendererData> Renderer2D::s_Data = nullptr;

    void Renderer2D::Init()
    {
        MRL_CORE_INFO("Initializing Renderer2D...");
        
        auto data = std::make_unique<RendererData>();

        data->TextureShader = std::make_unique<OpenGLShader>(
            "Assets/Shaders/Texture.vert", 
            "Assets/Shaders/Texture.frag"
        );
        if (!data->TextureShader->IsValid()) {
            MRL_CORE_ERROR("Renderer2D shader failed to load, renderer disabled");
            return;
        }

        data->Vertices.resize(MaxVertices);

        // Standard alpha blending. There is no depth test, so quads blend in submission order.
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        // Dynamic vertex buffer, refilled every batch
        glGenVertexArrays(1, &data->QuadVAO);
        glBindVertexArray(data->QuadVAO);

        glGenBuffers(1, &data->QuadVBO);
        glBindBuffer(GL_ARRAY_BUFFER, data->QuadVBO);
        glBufferData(GL_ARRAY_BUFFER, MaxVertices * sizeof(QuadVertex), nullptr, GL_DYNAMIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), (const void*)offsetof(QuadVertex, Position));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), (const void*)offsetof(QuadVertex, Color));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), (const void*)offsetof(QuadVertex, TexCoord));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), (const void*)offsetof(QuadVertex, TexIndex));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), (const void*)offsetof(QuadVertex, Circle));

        // Index buffer is static: every quad uses the same 0,1,2,2,3,0 pattern
        std::vector<uint32_t> indices(MaxIndices);
        for (uint32_t i = 0, offset = 0; i < MaxIndices; i += 6, offset += 4) {
            indices[i + 0] = offset + 0;
            indices[i + 1] = offset + 1;
            indices[i + 2] = offset + 2;
            indices[i + 3] = offset + 2;
            indices[i + 4] = offset + 3;
            indices[i + 5] = offset + 0;
        }
        glGenBuffers(1, &data->QuadEBO);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data->QuadEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

        glBindVertexArray(0);

        // 1x1 white texture so flat-colored quads go through the same path
        const uint32_t white = 0xffffffff;
        glGenTextures(1, &data->WhiteTexture);
        glBindTexture(GL_TEXTURE_2D, data->WhiteTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
        glBindTexture(GL_TEXTURE_2D, 0);
        data->TextureSlots[0] = data->WhiteTexture;

        // 2x2 magenta/black checker (bottom row first), the classic "missing texture" look
        const uint32_t magenta = 0xffff00ff, black = 0xff000000;
        const uint32_t checker[4] = { magenta, black, black, magenta };
        glGenTextures(1, &data->ErrorTexture);
        glBindTexture(GL_TEXTURE_2D, data->ErrorTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, checker);
        glBindTexture(GL_TEXTURE_2D, 0);

        int samplers[MaxTextureSlots];
        for (uint32_t i = 0; i < MaxTextureSlots; i++) samplers[i] = (int)i;
        data->TextureShader->Bind();
        data->TextureShader->SetUniform1iv("u_Textures", MaxTextureSlots, samplers);
        data->TextureShader->Unbind();

        s_Data = std::move(data);
        MRL_CORE_INFO("Renderer2D initialized successfully");
    }

    void Renderer2D::Shutdown()
    {
        if (s_Data) {
            glDisable(GL_BLEND);
            glDeleteVertexArrays(1, &s_Data->QuadVAO);
            glDeleteBuffers(1, &s_Data->QuadVBO);
            glDeleteBuffers(1, &s_Data->QuadEBO);
            glDeleteTextures(1, &s_Data->WhiteTexture);
            glDeleteTextures(1, &s_Data->ErrorTexture);
            s_Data.reset();
        }
        MRL_CORE_TRACE("Renderer2D shutdown complete");
    }

    void Renderer2D::BeginBatch(float viewportWidth, float viewportHeight)
    {
        BeginBatch(OrthographicCamera(0.0f, viewportWidth, 0.0f, viewportHeight));
    }

    void Renderer2D::BeginBatch(const OrthographicCamera& camera)
    {
        if (!s_Data) {
            ReportOnce("Renderer2D not initialized!");
            return;
        }

        s_Data->TextureShader->Bind();
        s_Data->TextureShader->SetUniformMat4f("u_ViewProjection", camera.GetViewProjectionMatrix());

        s_Data->QuadCount = 0;
        s_Data->TextureSlotCount = 1;
    }

    void Renderer2D::EndBatch()
    {
        if (!s_Data) return;

        Flush();
        s_Data->TextureShader->Unbind();
    }

    void Renderer2D::Flush()
    {
        if (s_Data->QuadCount == 0) return;

        glBindBuffer(GL_ARRAY_BUFFER, s_Data->QuadVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, s_Data->QuadCount * 4 * sizeof(QuadVertex), s_Data->Vertices.data());

        for (uint32_t i = 0; i < s_Data->TextureSlotCount; i++) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, s_Data->TextureSlots[i]);
        }

        glBindVertexArray(s_Data->QuadVAO);
        glDrawElements(GL_TRIANGLES, s_Data->QuadCount * 6, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);

        s_Data->Stats.DrawCalls++;
    }

    void Renderer2D::NextBatch()
    {
        Flush();
        s_Data->QuadCount = 0;
        s_Data->TextureSlotCount = 1;
    }

    void Renderer2D::SubmitQuad(const glm::vec2& position, const glm::vec2& size, float rotation, uint32_t textureID, const glm::vec2* texCoords, const glm::vec4& color, float circle)
    {
        if (s_Data->QuadCount >= MaxQuads)
            NextBatch();

        // Slot 0 is the white texture; textureID 0 (flat-colored quads) and the white texture map to it.
        uint32_t slot = 0;
        if (textureID != 0 && textureID != s_Data->WhiteTexture) {
            for (uint32_t i = 1; i < s_Data->TextureSlotCount; i++) {
                if (s_Data->TextureSlots[i] == textureID) { slot = i; break; }
            }
            if (slot == 0) {
                if (s_Data->TextureSlotCount >= MaxTextureSlots)
                    NextBatch();
                slot = s_Data->TextureSlotCount++;
                s_Data->TextureSlots[slot] = textureID;
            }
        }

        static const glm::vec2 corners[4] = { {-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f} };

        const float c = cosf(rotation), sn = sinf(rotation);

        QuadVertex* v = &s_Data->Vertices[s_Data->QuadCount * 4];
        for (int i = 0; i < 4; i++) {
            glm::vec2 offset = corners[i] * size;
            glm::vec2 rotated(offset.x * c - offset.y * sn, offset.x * sn + offset.y * c);
            v[i].Position = glm::vec3(position + rotated, 0.0f);
            v[i].Color = color;
            v[i].TexCoord = texCoords[i];
            v[i].TexIndex = (float)slot;
            v[i].Circle = circle;
        }
        s_Data->QuadCount++;
        s_Data->Stats.QuadCount++;
    }

    // GL texture id to draw with; null or failed-to-load textures become the error checker.
    static uint32_t ResolveTexture(OpenGLTexture2D* texture, GLuint errorTexture)
    {
        if (!texture) {
            ReportOnce("DrawQuad called with a null texture, drawing the error texture");
            return errorTexture;
        }
        return texture->IsLoaded() ? texture->GetRendererID() : errorTexture;
    }

    void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, OpenGLTexture2D* texture)
    {
        DrawQuad(position, size, texture, glm::vec4(1.0f));
    }

    void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, OpenGLTexture2D* texture, const glm::vec4& tint)
    {
        DrawRotatedQuad(position, size, 0.0f, texture, tint);
    }

    void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color)
    {
        DrawRotatedQuad(position, size, 0.0f, color);
    }

    void Renderer2D::DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, OpenGLTexture2D* texture)
    {
        DrawRotatedQuad(position, size, rotation, texture, glm::vec4(1.0f));
    }

    void Renderer2D::DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, OpenGLTexture2D* texture, const glm::vec4& tint)
    {
        if (!s_Data) {
            ReportOnce("Cannot draw quad - Renderer2D not initialized");
            return;
        }
        SubmitQuad(position, size, rotation, ResolveTexture(texture, s_Data->ErrorTexture), s_FullTexCoords, tint);
    }

    void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const SubTexture2D& subTexture)
    {
        DrawRotatedQuad(position, size, 0.0f, subTexture, glm::vec4(1.0f));
    }

    void Renderer2D::DrawQuad(const glm::vec2& position, const glm::vec2& size, const SubTexture2D& subTexture, const glm::vec4& tint)
    {
        DrawRotatedQuad(position, size, 0.0f, subTexture, tint);
    }

    void Renderer2D::DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const SubTexture2D& subTexture)
    {
        DrawRotatedQuad(position, size, rotation, subTexture, glm::vec4(1.0f));
    }

    void Renderer2D::DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const SubTexture2D& subTexture, const glm::vec4& tint)
    {
        if (!s_Data) {
            ReportOnce("Cannot draw quad - Renderer2D not initialized");
            return;
        }
        SubmitQuad(position, size, rotation, ResolveTexture(subTexture.GetTexture(), s_Data->ErrorTexture), subTexture.GetTexCoords(), tint);
    }

    void Renderer2D::DrawRotatedQuad(const glm::vec2& position, const glm::vec2& size, float rotation, const glm::vec4& color)
    {
        if (!s_Data) {
            ReportOnce("Cannot draw quad - Renderer2D not initialized");
            return;
        }
        SubmitQuad(position, size, rotation, 0, s_FullTexCoords, color);
    }

    void Renderer2D::DrawLine(const glm::vec2& from, const glm::vec2& to, const glm::vec4& color, float thickness)
    {
        if (!s_Data) {
            ReportOnce("Cannot draw line - Renderer2D not initialized");
            return;
        }
        const glm::vec2 delta = to - from;
        const float length = glm::length(delta);
        if (length <= 0.0f || thickness <= 0.0f) return;
        SubmitQuad((from + to) * 0.5f, { length, thickness }, atan2f(delta.y, delta.x), 0, s_FullTexCoords, color);
    }

    void Renderer2D::DrawCircle(const glm::vec2& center, float radius, const glm::vec4& color, float thickness)
    {
        if (!s_Data) {
            ReportOnce("Cannot draw circle - Renderer2D not initialized");
            return;
        }
        if (radius <= 0.0f) return;
        const float ring = thickness <= 0.0f ? 1.0f : std::min(thickness / radius, 1.0f);
        SubmitQuad(center, { radius * 2.0f, radius * 2.0f }, 0.0f, 0, s_FullTexCoords, color, ring);
    }

    void Renderer2D::DrawString(const BitmapFont& font, const std::string& text, const glm::vec2& position, float glyphHeight, const glm::vec4& color)
    {
        const float advance = font.GetAdvance(glyphHeight);
        const glm::vec2 glyphSize(advance, glyphHeight);

        size_t column = 0, line = 0;
        for (char c : text) {
            if (c == '\n') { line++; column = 0; continue; }
            if (c == '\r') continue;

            if (c != ' ') {
                glm::vec2 center = position + glm::vec2(((float)column + 0.5f) * advance, glyphHeight * (0.5f - (float)line));
                DrawQuad(center, glyphSize, font.GetGlyph(c), color);
            }
            column++;
        }
    }

    Renderer2D::Statistics Renderer2D::GetStats()
    {
        return s_Data ? s_Data->Stats : Statistics();
    }

    void Renderer2D::ResetStats()
    {
        if (s_Data) s_Data->Stats = Statistics();
    }

} 
