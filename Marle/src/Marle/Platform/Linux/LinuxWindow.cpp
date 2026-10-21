#include "mrlpch.h"
#include "Marle/Log.h"
#include "LinuxWindow.h"
#include "LinuxKeyCodes.h"
#include "Marle/Events/ApplicationEvent.h"
#include "Marle/Events/KeyEvent.h"
#include "Marle/Events/MouseEvent.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

namespace Marle {

    static int s_GLFWWindowCount = 0;

    static void GLFWErrorCallback(int error, const char* description)
    {
        MRL_CORE_ERROR("GLFW error ({}): {}", error, description);
    }

    std::unique_ptr<Window> Window::Create(const WindowProps& props)
    {
        auto window = std::make_unique<LinuxWindow>(props);
        if (!window->IsValid()) return nullptr;
        return window;
    }

    LinuxWindow::LinuxWindow(const WindowProps& props)
    {
        m_Data.Width = props.Width;
        m_Data.Height = props.Height;

        MRL_CORE_INFO("Creating Linux window: {} ({}x{})", props.Title, props.Width, props.Height);

        if (s_GLFWWindowCount == 0) {
            glfwSetErrorCallback(GLFWErrorCallback);
            if (!glfwInit()) {
                MRL_CORE_ERROR("Failed to initialize GLFW!");
                return;
            }
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        m_Window = glfwCreateWindow((int)props.Width, (int)props.Height, props.Title, nullptr, nullptr);
        if (!m_Window) {
            MRL_CORE_ERROR("Failed to create GLFW window!");
            if (s_GLFWWindowCount == 0) glfwTerminate();
            return;
        }
        ++s_GLFWWindowCount;

        glfwMakeContextCurrent(m_Window);
        glfwSwapInterval(1);

        int version = gladLoadGL((GLADloadfunc)glfwGetProcAddress);
        if (version == 0) {
            MRL_CORE_ERROR("Failed to initialize GLAD!");
            return;
        }
        MRL_CORE_INFO("GLAD initialized. OpenGL version: {}.{}", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));

        glfwSetWindowUserPointer(m_Window, &m_Data);

        glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window) {
            auto& data = *(WindowData*)glfwGetWindowUserPointer(window);
            WindowCloseEvent event;
            data.EventCallback(event);
        });

        glfwSetWindowFocusCallback(m_Window, [](GLFWwindow* window, int focused) {
            auto& data = *(WindowData*)glfwGetWindowUserPointer(window);
            if (focused) { WindowFocusEvent event; data.EventCallback(event); }
            else         { WindowLostFocusEvent event; data.EventCallback(event); }
        });

        glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height) {
            auto& data = *(WindowData*)glfwGetWindowUserPointer(window);
            data.Width = (unsigned int)width;
            data.Height = (unsigned int)height;
            WindowResizeEvent event(data.Width, data.Height);
            data.EventCallback(event);
        });

        glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods) {
            auto& data = *(WindowData*)glfwGetWindowUserPointer(window);
            int marleKey = TranslateGLFWKeyToMarle(key);
            switch (action) {
                case GLFW_PRESS:   { KeyPressedEvent event(marleKey, 0);  data.EventCallback(event); break; }
                case GLFW_REPEAT:  { KeyPressedEvent event(marleKey, 1);  data.EventCallback(event); break; }
                case GLFW_RELEASE: { KeyReleasedEvent event(marleKey);    data.EventCallback(event); break; }
            }
        });

        glfwSetCharCallback(m_Window, [](GLFWwindow* window, unsigned int codepoint) {
            auto& data = *(WindowData*)glfwGetWindowUserPointer(window);
            KeyTypedEvent event((int)codepoint);
            data.EventCallback(event);
        });

        glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int mods) {
            auto& data = *(WindowData*)glfwGetWindowUserPointer(window);
            switch (action) {
                case GLFW_PRESS:   { MouseButtonPressedEvent event(button);  data.EventCallback(event); break; }
                case GLFW_RELEASE: { MouseButtonReleasedEvent event(button); data.EventCallback(event); break; }
            }
        });

        glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xOffset, double yOffset) {
            auto& data = *(WindowData*)glfwGetWindowUserPointer(window);
            MouseScrolledEvent event((float)xOffset, (float)yOffset);
            data.EventCallback(event);
        });

        glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double x, double y) {
            auto& data = *(WindowData*)glfwGetWindowUserPointer(window);
            // GLFW's origin is top-left; the Window contract is bottom-left.
            MouseMovedEvent event((float)x, (float)data.Height - (float)y);
            data.EventCallback(event);
        });
    }

    LinuxWindow::~LinuxWindow()
    {
        if (m_Window) {
            glfwDestroyWindow(m_Window);
            --s_GLFWWindowCount;
        }
        if (s_GLFWWindowCount == 0) glfwTerminate();
        MRL_CORE_TRACE("Linux window shutdown complete");
    }

    void LinuxWindow::OnUpdate()
    {
        glfwPollEvents();
        glfwSwapBuffers(m_Window);
    }

}
