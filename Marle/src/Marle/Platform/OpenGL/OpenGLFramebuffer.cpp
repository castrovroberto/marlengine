#include "mrlpch.h"
#include "Marle/Log.h"
#include "OpenGLFramebuffer.h"

namespace Marle {

    OpenGLFramebuffer::OpenGLFramebuffer(uint32_t width, uint32_t height)
        : m_Width(width), m_Height(height)
    {
        if (width == 0 || height == 0) {
            MRL_CORE_ERROR("Cannot create a {}x{} framebuffer", width, height);
            return;
        }

        glGenTextures(1, &m_ColorAttachment);
        glBindTexture(GL_TEXTURE_2D, m_ColorAttachment);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        AllocateColorAttachment();

        GLint prevFramebuffer = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFramebuffer);
        glGenFramebuffers(1, &m_RendererID);
        glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorAttachment, 0);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        m_Complete = status == GL_FRAMEBUFFER_COMPLETE;
        if (!m_Complete)
            MRL_CORE_ERROR("Framebuffer {}x{} is incomplete (status 0x{:x})", width, height, status);

        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFramebuffer);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    OpenGLFramebuffer::~OpenGLFramebuffer()
    {
        glDeleteFramebuffers(1, &m_RendererID);
        glDeleteTextures(1, &m_ColorAttachment);
    }

    void OpenGLFramebuffer::AllocateColorAttachment()
    {
        // The color texture must be bound to GL_TEXTURE_2D
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)m_Width, (GLsizei)m_Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    void OpenGLFramebuffer::Bind()
    {
        if (!m_Complete) {
            MRL_CORE_ERROR("Cannot bind an incomplete framebuffer");
            return;
        }
        m_Bound = true;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_PrevFramebuffer);
        glGetIntegerv(GL_VIEWPORT, m_PrevViewport);
        glBindFramebuffer(GL_FRAMEBUFFER, m_RendererID);
        glViewport(0, 0, (GLsizei)m_Width, (GLsizei)m_Height);
    }

    void OpenGLFramebuffer::Unbind()
    {
        if (!m_Bound) return;
        m_Bound = false;
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)m_PrevFramebuffer);
        glViewport(m_PrevViewport[0], m_PrevViewport[1], m_PrevViewport[2], m_PrevViewport[3]);
    }

    void OpenGLFramebuffer::Resize(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0) {
            MRL_CORE_ERROR("Cannot resize a framebuffer to {}x{}", width, height);
            return;
        }
        if (m_ColorAttachment == 0) {
            MRL_CORE_ERROR("Cannot resize a framebuffer that failed to create");
            return;
        }

        m_Width = width;
        m_Height = height;

        GLint prevTexture = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture);
        glBindTexture(GL_TEXTURE_2D, m_ColorAttachment);
        AllocateColorAttachment();
        glBindTexture(GL_TEXTURE_2D, (GLuint)prevTexture);
    }

}
