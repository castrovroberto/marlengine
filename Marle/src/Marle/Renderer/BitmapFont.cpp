#include "mrlpch.h"
#include "Marle/Log.h"
#include "BitmapFont.h"

namespace Marle {

    static constexpr char FirstGlyph = 32;

    BitmapFont::BitmapFont(const std::string& path, uint32_t cellWidth, uint32_t cellHeight)
        : m_CellWidth(cellWidth), m_CellHeight(cellHeight)
    {
        if (cellWidth == 0 || cellHeight == 0) {
            MRL_CORE_ERROR("Font {} needs a non-zero cell size, got {}x{}", path, cellWidth, cellHeight);
            m_CellWidth = m_CellHeight = 1;
        }

        m_Texture = std::make_unique<OpenGLTexture2D>(path);
        if (!m_Texture->IsLoaded()) return;

        m_Columns = (uint32_t)m_Texture->GetWidth() / m_CellWidth;
        m_Rows = (uint32_t)m_Texture->GetHeight() / m_CellHeight;
        if (m_Columns == 0 || m_Rows == 0)
            MRL_CORE_ERROR("Font {} ({}x{}) is smaller than one {}x{} cell", path, m_Texture->GetWidth(), m_Texture->GetHeight(), m_CellWidth, m_CellHeight);
    }

    SubTexture2D BitmapFont::GetGlyph(char c) const
    {
        // Without a usable sheet, cover the whole texture: Renderer2D swaps in the error checker anyway.
        if (!IsLoaded() || m_Columns == 0 || m_Rows == 0)
            return SubTexture2D(m_Texture.get(), { 0.0f, 0.0f }, { 1.0f, 1.0f });

        uint32_t index = (unsigned char)c - FirstGlyph;
        if ((unsigned char)c < (unsigned char)FirstGlyph || index >= m_Columns * m_Rows)
            index = '?' - FirstGlyph;

        // The sheet's first row is the top of the image; cell rows count from the bottom.
        uint32_t column = index % m_Columns;
        uint32_t row = m_Rows - 1 - index / m_Columns;

        const glm::vec2 texel(0.5f / (float)m_Texture->GetWidth(), 0.5f / (float)m_Texture->GetHeight());
        SubTexture2D cell = SubTexture2D::CreateFromCoords(m_Texture.get(), { (float)column, (float)row }, { (float)m_CellWidth, (float)m_CellHeight });
        const glm::vec2* uv = cell.GetTexCoords();   // bottom-left, bottom-right, top-right, top-left
        return SubTexture2D(m_Texture.get(), uv[0] + texel, uv[2] - texel);
    }

    glm::vec2 BitmapFont::Measure(const std::string& text, float glyphHeight) const
    {
        size_t widest = 0, line = 0, lines = 1;
        for (char c : text) {
            if (c == '\n') { lines++; line = 0; }
            else if (c != '\r') { line++; widest = std::max(widest, line); }
        }
        return { (float)widest * GetAdvance(glyphHeight), (float)lines * glyphHeight };
    }

}
