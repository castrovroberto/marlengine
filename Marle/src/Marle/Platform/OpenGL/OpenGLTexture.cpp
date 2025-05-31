#include "mrlpch.h"
#include "OpenGLTexture.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace Marle {

    OpenGLTexture2D::OpenGLTexture2D(const std::string& path)
        : m_FilePath(path)
    {
        // Load image data
        stbi_set_flip_vertically_on_load(1); // Flip Y coordinate to match OpenGL
        m_LocalBuffer = stbi_load(path.c_str(), &m_Width, &m_Height, &m_BPP, 4); // Force 4 channels (RGBA)
        
        if (!m_LocalBuffer) {
            printf("Failed to load texture: %s\n", path.c_str());
            printf("STB Error: %s\n", stbi_failure_reason());
            return;
        }

        printf("Loaded texture: %s (%dx%d, %d channels)\n", path.c_str(), m_Width, m_Height, m_BPP);

        // Generate OpenGL texture
        glGenTextures(1, &m_RendererID);
        glBindTexture(GL_TEXTURE_2D, m_RendererID);

        // Set texture parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // Upload texture data
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Width, m_Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_LocalBuffer);

        // Unbind texture
        glBindTexture(GL_TEXTURE_2D, 0);

        // Free image data
        if (m_LocalBuffer) {
            stbi_image_free(m_LocalBuffer);
            m_LocalBuffer = nullptr;
        }
    }

    OpenGLTexture2D::~OpenGLTexture2D()
    {
        glDeleteTextures(1, &m_RendererID);
    }

    void OpenGLTexture2D::Bind(uint32_t slot) const
    {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, m_RendererID);
    }

    void OpenGLTexture2D::Unbind() const
    {
        glBindTexture(GL_TEXTURE_2D, 0);
    }

} 