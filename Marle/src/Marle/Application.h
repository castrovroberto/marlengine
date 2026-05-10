#pragma once

#include "Core.h"
#include "Window.h"
#include "Events/Event.h"

namespace Marle 
{
    class EXPORT Application
    {
        public:
            Application(const WindowProps& props = WindowProps());
            virtual ~Application();
            
            void Run();
            virtual void OnEvent(Event& e);

            unsigned int GetWidth()  const { return m_WindowProps.Width; }
            unsigned int GetHeight() const { return m_WindowProps.Height; }

        protected:
            virtual void OnUpdate(double fixed_dt);
            virtual void OnRender(double interpolation_alpha);
            virtual void OnResize(unsigned int width, unsigned int height) {}

        public:
            void HandleResize(unsigned int width, unsigned int height);

        private:
            void InitWindow();
            void ShutdownWindow();
            void InitGraphics();
            void ShutdownGraphics();

            WindowProps m_WindowProps;
            bool m_Running = true;

            double m_LastFrameTime;
            double m_FixedDeltaTime = 1.0 / 60.0;
            double m_Accumulator = 0.0;

        #ifdef MRL_PLATFORM_MACOS
            void* m_Window;
            void* m_GLContext;
            void* m_WindowDelegate;
        #endif
    };

    // to be defined in Client Application
    Application* CreateApplication();

}