#pragma once

#include <string>
#include <glad/gl.h>
#include <glm/glm.hpp>

namespace Marle {
    
    class OpenGLShader {
    public:
        OpenGLShader(const std::string& vertexSrcPath, const std::string& fragmentSrcPath);
        ~OpenGLShader();

        void Bind() const;
        void Unbind() const;

        // Utility functions to set uniforms
        void SetUniform1i(const std::string& name, int value);
        void SetUniformMat4f(const std::string& name, const glm::mat4& matrix);

    private:
        std::string ReadFile(const std::string& filepath);
        GLuint CompileShader(GLenum type, const std::string& source);
        GLuint CreateProgram(const std::string& vertexShader, const std::string& fragmentShader);
        GLint GetUniformLocation(const std::string& name);

        GLuint m_RendererID = 0;
    };

} 