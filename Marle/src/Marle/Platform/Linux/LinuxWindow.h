#pragma once

#include "Marle/Window.h"

struct GLFWwindow;

namespace Marle {

    class LinuxWindow : public Window
    {
        public:
            LinuxWindow(const WindowProps& props);
            ~LinuxWindow() override;

            void OnUpdate() override;
            bool IsValid() const { return m_Window != nullptr; }

            unsigned int GetWidth() const override { return m_Data.Width; }
            unsigned int GetHeight() const override { return m_Data.Height; }

            void SetEventCallback(const EventCallbackFn& callback) override { m_Data.EventCallback = callback; }

        private:
            struct WindowData {
                unsigned int Width = 0, Height = 0;
                EventCallbackFn EventCallback;
            };

            GLFWwindow* m_Window = nullptr;
            WindowData m_Data;
    };

}
