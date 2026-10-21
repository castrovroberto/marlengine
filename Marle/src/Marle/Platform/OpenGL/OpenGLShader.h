#pragma once

#include <string>
#include <unordered_map>
#include <glad/gl.h>
#include <glm/glm.hpp>

namespace Marle {

    class OpenGLShader {
    public:
        OpenGLShader(const std::string& vertexSrcPath, const std::string& fragmentSrcPath);
        ~OpenGLShader();

        void Bind() const;
        void Unbind() const;

        bool IsValid() const { return m_RendererID != 0; }

        void SetUniform1i(const std::string& name, int value);
        void SetUniform1iv(const std::string& name, int count, const int* values);
        void SetUniformMat4f(const std::string& name, const glm::mat4& matrix);

    private:
        std::string ReadFile(const std::string& filepath);
        GLuint CompileShader(GLenum type, const std::string& source);
        GLuint CreateProgram(const std::string& vertexShader, const std::string& fragmentShader);
        GLint GetUniformLocation(const std::string& name);

        GLuint m_RendererID = 0;
        mutable std::unordered_map<std::string, GLint> m_UniformLocationCache;
    };

}