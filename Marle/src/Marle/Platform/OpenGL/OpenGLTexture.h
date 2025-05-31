#pragma once

#include <string>
#include <glad/gl.h>

namespace Marle {
    
    class OpenGLTexture2D {
    public:
        OpenGLTexture2D(const std::string& path);
        ~OpenGLTexture2D();

        void Bind(uint32_t slot = 0) const;
        void Unbind() const;

        int GetWidth() const { return m_Width; }
        int GetHeight() const { return m_Height; }
        GLuint GetRendererID() const { return m_RendererID; }

    private:
        GLuint m_RendererID = 0;
        std::string m_FilePath;
        unsigned char* m_LocalBuffer = nullptr;
        int m_Width = 0, m_Height = 0, m_BPP = 0; // Bits Per Pixel (or channels)
    };

} 