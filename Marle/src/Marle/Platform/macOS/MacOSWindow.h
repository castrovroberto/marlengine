#pragma once

#include "Marle/Window.h"

namespace Marle {

    class MacOSWindow : public Window
    {
        public:
            MacOSWindow(const WindowProps& props);
            ~MacOSWindow() override;

            void OnUpdate() override;

            unsigned int GetWidth() const override { return m_Data.Width; }
            unsigned int GetHeight() const override { return m_Data.Height; }

            void SetEventCallback(const EventCallbackFn& callback) override { m_Data.EventCallback = callback; }

            bool IsValid() const { return m_Window != nullptr && m_GLContext != nullptr; }

            struct WindowData {
                unsigned int Width = 0, Height = 0;
                EventCallbackFn EventCallback;
            };

        private:
            bool InitWindow(const WindowProps& props);
            bool InitGraphics();

            WindowData m_Data;
            bool m_CloseSent = false;

            // Cocoa objects, held as void* to keep Objective-C out of this header
            void* m_Window = nullptr;
            void* m_GLContext = nullptr;
            void* m_WindowDelegate = nullptr;
    };

}
