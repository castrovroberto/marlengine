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

        protected:
            virtual void OnUpdate(double fixed_dt); // To be overridden by SandboxApp for game logic
            virtual void OnRender(double interpolation_alpha); // For rendering, alpha for interpolation

        private:
            void InitWindow();
            void ShutdownWindow();
            void InitGraphics();
            void ShutdownGraphics();

            WindowProps m_WindowProps;
            bool m_Running = true;

            // Time management for the game loop
            double m_LastFrameTime;
            double m_FixedDeltaTime = 1.0 / 60.0; // Target 60 updates per second
            double m_Accumulator = 0.0;

        #ifdef MRL_PLATFORM_MACOS
            void* m_Window;  // NSWindow* but avoiding Objective-C headers
            void* m_GLContext; // NSOpenGLContext* but avoiding Objective-C headers
        #endif
    };

    // to be defined in Client Application
    Application* CreateApplication();

}