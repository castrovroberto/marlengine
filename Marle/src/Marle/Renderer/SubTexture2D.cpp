#include "mrlpch.h"
#include "SubTexture2D.h"

namespace Marle {

    SubTexture2D::SubTexture2D(OpenGLTexture2D* texture, const glm::vec2& min, const glm::vec2& max)
        : m_Texture(texture)
    {
        m_TexCoords[0] = { min.x, min.y };
        m_TexCoords[1] = { max.x, min.y };
        m_TexCoords[2] = { max.x, max.y };
        m_TexCoords[3] = { min.x, max.y };
    }

    SubTexture2D SubTexture2D::CreateFromCoords(OpenGLTexture2D* texture, const glm::vec2& coords,
                                                const glm::vec2& cellSize, const glm::vec2& spriteSize)
    {
        // A texture that failed to load has no size: fall back to the full (empty) texture.
        if (!texture || texture->GetWidth() <= 0 || texture->GetHeight() <= 0)
            return SubTexture2D(texture, { 0.0f, 0.0f }, { 1.0f, 1.0f });

        glm::vec2 sheet((float)texture->GetWidth(), (float)texture->GetHeight());
        glm::vec2 min((coords.x * cellSize.x) / sheet.x, (coords.y * cellSize.y) / sheet.y);
        glm::vec2 max(((coords.x + spriteSize.x) * cellSize.x) / sheet.x, ((coords.y + spriteSize.y) * cellSize.y) / sheet.y);
        return SubTexture2D(texture, min, max);
    }

}
