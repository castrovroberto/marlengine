#pragma once

#include "../Core.h"
#include "SubTexture2D.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace Marle {

    // A fixed-width font read from a sheet of equally sized glyph cells: ASCII 32 (space) upward in
    // reading order starting at the top-left cell (tools/make_font_sheet.py makes one). Glyphs are
    // white with their coverage in alpha, so a tint colors them. Characters outside the sheet are
    // drawn as '?'. A sheet that fails to load is logged and leaves the font not loaded;
    // Renderer2D then draws its glyphs as the error checker, like any failed texture.
    class EXPORT BitmapFont
    {
        public:
            BitmapFont(const std::string& path, uint32_t cellWidth, uint32_t cellHeight);

            bool IsLoaded() const { return m_Texture && m_Texture->IsLoaded(); }

            // The cell for a character. The UVs are inset half a texel so linear filtering
            // doesn't pull in the neighboring cell. The font must outlive the returned SubTexture2D.
            SubTexture2D GetGlyph(char c) const;

            // Width of one glyph for a given height (cells keep their proportions)
            float GetAdvance(float glyphHeight) const { return glyphHeight * (float)m_CellWidth / (float)m_CellHeight; }

            // Size of the text's bounding box: the widest line by one glyph height per line.
            glm::vec2 Measure(const std::string& text, float glyphHeight) const;

        private:
            std::unique_ptr<OpenGLTexture2D> m_Texture;
            uint32_t m_CellWidth, m_CellHeight;
            uint32_t m_Columns = 0, m_Rows = 0;
    };

}
