#pragma once

#include <string>
#include <glad/gl.h>

namespace Marle {
    
    class OpenGLTexture2D {
    public:
        // Loads an image file as RGBA8. On failure the error is logged and the texture is left
        // not loaded (IsLoaded() == false, size 0); Renderer2D draws such textures as a
        // magenta/black checkerboard so the problem is visible.
        OpenGLTexture2D(const std::string& path);
        ~OpenGLTexture2D();

        // Owns a GL texture: copying would delete it twice.
        OpenGLTexture2D(const OpenGLTexture2D&) = delete;
        OpenGLTexture2D& operator=(const OpenGLTexture2D&) = delete;

        bool IsLoaded() const { return m_RendererID != 0; }

        void Bind(uint32_t slot = 0) const;
        void Unbind() const;

        int GetWidth() const { return m_Width; }
        int GetHeight() const { return m_Height; }
        GLuint GetRendererID() const { return m_RendererID; }

    private:
        GLuint m_RendererID = 0;
        std::string m_FilePath;
        int m_Width = 0, m_Height = 0;
    };

} 