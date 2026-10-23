#pragma once

#include <cstdint>
#include <glad/gl.h>

namespace Marle {

    // An offscreen render target with one RGBA8 color attachment (no depth: Renderer2D draws back to
    // front without a depth test). Between Bind() and Unbind() everything drawn, including
    // Renderer2D batches, lands in the color texture; use BeginBatch(width, height) for pixel space.
    class OpenGLFramebuffer {
    public:
        // On failure (zero size, incomplete framebuffer) the error is logged and IsComplete() is false.
        OpenGLFramebuffer(uint32_t width, uint32_t height);
        ~OpenGLFramebuffer();

        // Owns GL objects: copying would delete them twice.
        OpenGLFramebuffer(const OpenGLFramebuffer&) = delete;
        OpenGLFramebuffer& operator=(const OpenGLFramebuffer&) = delete;

        // Binds this framebuffer and sets the viewport to its size, remembering the previous
        // framebuffer and viewport for Unbind(). Does nothing (and logs) if !IsComplete().
        void Bind();
        // Restores the framebuffer and viewport that were current at Bind(); a no-op if Bind() did nothing.
        void Unbind();

        // Reallocates the color attachment; its contents become undefined. The texture id is kept.
        // A zero size is ignored (logged).
        void Resize(uint32_t width, uint32_t height);

        bool IsComplete() const { return m_Complete; }
        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }

        // The color texture, e.g. for sampling or showing in an ImGui image. Rows run bottom to top.
        GLuint GetColorAttachment() const { return m_ColorAttachment; }

    private:
        void AllocateColorAttachment();

        GLuint m_RendererID = 0;
        GLuint m_ColorAttachment = 0;
        uint32_t m_Width = 0, m_Height = 0;
        bool m_Complete = false;
        bool m_Bound = false;

        GLint m_PrevFramebuffer = 0;
        GLint m_PrevViewport[4] = { 0, 0, 0, 0 };
    };

}
