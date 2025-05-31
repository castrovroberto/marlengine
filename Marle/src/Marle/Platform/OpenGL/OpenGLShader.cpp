#include "mrlpch.h"
#include "OpenGLShader.h"
#include <fstream>
#include <sstream>
#include <glm/gtc/type_ptr.hpp>

namespace Marle {

    OpenGLShader::OpenGLShader(const std::string& vertexSrcPath, const std::string& fragmentSrcPath)
    {
        std::string vertexSource = ReadFile(vertexSrcPath);
        std::string fragmentSource = ReadFile(fragmentSrcPath);
        
        if (vertexSource.empty() || fragmentSource.empty()) {
            printf("Failed to read shader files!\n");
            return;
        }

        m_RendererID = CreateProgram(vertexSource, fragmentSource);
    }

    OpenGLShader::~OpenGLShader()
    {
        glDeleteProgram(m_RendererID);
    }

    void OpenGLShader::Bind() const
    {
        glUseProgram(m_RendererID);
    }

    void OpenGLShader::Unbind() const
    {
        glUseProgram(0);
    }

    void OpenGLShader::SetUniform1i(const std::string& name, int value)
    {
        GLint location = GetUniformLocation(name);
        glUniform1i(location, value);
    }

    void OpenGLShader::SetUniformMat4f(const std::string& name, const glm::mat4& matrix)
    {
        GLint location = GetUniformLocation(name);
        glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
    }

    std::string OpenGLShader::ReadFile(const std::string& filepath)
    {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            printf("Failed to open shader file: %s\n", filepath.c_str());
            return "";
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();

        return buffer.str();
    }

    GLuint OpenGLShader::CompileShader(GLenum type, const std::string& source)
    {
        GLuint shader = glCreateShader(type);
        const char* src = source.c_str();
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);

        // Check for compilation errors
        GLint result;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &result);
        if (result == GL_FALSE) {
            GLint length;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
            char* message = new char[length];
            glGetShaderInfoLog(shader, length, &length, message);
            
            printf("Failed to compile %s shader:\n%s\n", 
                   (type == GL_VERTEX_SHADER ? "vertex" : "fragment"), message);
            
            delete[] message;
            glDeleteShader(shader);
            return 0;
        }

        return shader;
    }

    GLuint OpenGLShader::CreateProgram(const std::string& vertexShader, const std::string& fragmentShader)
    {
        GLuint program = glCreateProgram();
        GLuint vs = CompileShader(GL_VERTEX_SHADER, vertexShader);
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragmentShader);

        if (vs == 0 || fs == 0) {
            glDeleteProgram(program);
            return 0;
        }

        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
        glValidateProgram(program);

        // Check for linking errors
        GLint result;
        glGetProgramiv(program, GL_LINK_STATUS, &result);
        if (result == GL_FALSE) {
            GLint length;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
            char* message = new char[length];
            glGetProgramInfoLog(program, length, &length, message);
            
            printf("Failed to link shader program:\n%s\n", message);
            
            delete[] message;
            glDeleteProgram(program);
            glDeleteShader(vs);
            glDeleteShader(fs);
            return 0;
        }

        glDetachShader(program, vs);
        glDetachShader(program, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);

        printf("Shader program created successfully\n");
        return program;
    }

    GLint OpenGLShader::GetUniformLocation(const std::string& name)
    {
        GLint location = glGetUniformLocation(m_RendererID, name.c_str());
        if (location == -1) {
            printf("Warning: uniform '%s' not found\n", name.c_str());
        }
        return location;
    }

} 