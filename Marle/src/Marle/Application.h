#pragma once

#include "Core.h"
#include "Window.h"

namespace Marle 
{
    class EXPORT Application
    {
        public:
            Application(const WindowProps& props = WindowProps());
            virtual ~Application();
            
            void Run();

        private:
            void InitWindow();
            void ShutdownWindow();
            void InitGraphics();
            void ShutdownGraphics();

            WindowProps m_WindowProps;
            bool m_Running = true;

        #ifdef MRL_PLATFORM_MACOS
            void* m_Window;  // NSWindow* but avoiding Objective-C headers
            void* m_GLContext; // NSOpenGLContext* but avoiding Objective-C headers
        #endif
    };

    // to be defined in Client Application
    Application* CreateApplication();

}