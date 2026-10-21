#pragma once

#include "../Core.h"
#include "../Platform/OpenGL/OpenGLTexture.h"
#include <glm/glm.hpp>

namespace Marle {

    // A rectangular region of a texture (e.g. one sprite in a sprite sheet).
    // Does not own the texture: it must outlive every SubTexture2D made from it.
    class EXPORT SubTexture2D
    {
        public:
            // min/max are normalized UVs; (0,0) is the bottom-left of the image as displayed.
            SubTexture2D(OpenGLTexture2D* texture, const glm::vec2& min, const glm::vec2& max);

            // Sprite-sheet helper. `coords` is the (column, row) of the cell, with (0,0) the
            // bottom-left cell; `cellSize` is one cell in pixels; `spriteSize` is how many
            // cells the sprite spans, e.g. {2, 1} for a sprite two cells wide.
            static SubTexture2D CreateFromCoords(OpenGLTexture2D* texture, const glm::vec2& coords,
                                                 const glm::vec2& cellSize, const glm::vec2& spriteSize = { 1.0f, 1.0f });

            OpenGLTexture2D* GetTexture() const { return m_Texture; }

            // UVs for the quad corners: bottom-left, bottom-right, top-right, top-left
            const glm::vec2* GetTexCoords() const { return m_TexCoords; }

        private:
            OpenGLTexture2D* m_Texture;
            glm::vec2 m_TexCoords[4];
    };

}
