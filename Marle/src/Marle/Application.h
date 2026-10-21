#pragma once

#include "Core.h"
#include "Window.h"
#include "Events/Event.h"

#include <memory>

namespace Marle 
{
    class EXPORT Application
    {
        public:
            Application(const WindowProps& props = WindowProps());
            virtual ~Application();
            
            void Run();
            virtual void OnEvent(Event& e);

            unsigned int GetWidth()  const { return m_Window->GetWidth(); }
            unsigned int GetHeight() const { return m_Window->GetHeight(); }

        protected:
            virtual void OnUpdate(double fixed_dt);
            virtual void OnRender(double interpolation_alpha);
            virtual void OnResize(unsigned int width, unsigned int height) {}

        private:
            void InitGraphics();
            void ShutdownGraphics();

            // Window callback: handles engine-level events, then forwards to OnEvent().
            void HandleEvent(Event& e);
            void HandleResize(unsigned int width, unsigned int height);

            std::unique_ptr<Window> m_Window;
            bool m_Running = true;

            double m_FixedDeltaTime = 1.0 / 60.0;
            double m_Accumulator = 0.0;
    };

    // to be defined in Client Application
    Application* CreateApplication();

}
