#pragma once

#include "Core.h"
#include "Window.h"
#include "Events/Event.h"
#include "InputFrame.h"
#include "LayerStack.h"

#include <memory>

namespace Marle 
{
    class EXPORT Application
    {
        public:
            Application(const WindowProps& props = WindowProps());
            virtual ~Application();
            
            void Run();
            void Close() { m_Running = false; } // the loop exits after the current frame
            virtual void OnEvent(Event& e);

            // Layers update and render after the application's own OnUpdate/OnRender (bottom to top)
            // and see events before OnEvent (top to bottom, until handled). The application owns them
            // and detaches them, with the GL context still alive, when it is destroyed.
            Layer* PushLayer(std::unique_ptr<Layer> layer) { return m_LayerStack.PushLayer(std::move(layer)); }
            Layer* PushOverlay(std::unique_ptr<Layer> layer) { return m_LayerStack.PushOverlay(std::move(layer)); }
            std::unique_ptr<Layer> PopLayer(Layer* layer) { return m_LayerStack.Pop(layer); }

            // The input of the fixed step being run, valid during OnUpdate and the layers' OnUpdate. Read it
            // instead of polling Input when behavior must be repeatable (ADR-0002).
            const InputFrame& GetInputFrame() const { return m_InputFrame; }

            const LayerStack& GetLayerStack() const { return m_LayerStack; }   // e.g. to construct an ImGuiLayer

            unsigned int GetWidth()  const { return m_Window->GetWidth(); }
            unsigned int GetHeight() const { return m_Window->GetHeight(); }

        protected:
            virtual void OnUpdate(double fixed_dt);
            virtual void OnRender(double interpolation_alpha);
            virtual void OnResize(unsigned int width, unsigned int height) {}

            // The input for the next fixed step. By default it is the live keyboard and mouse
            // (Input::TakeFrame()); override it to feed recorded input instead.
            virtual InputFrame SampleInput();

        private:
            void InitGraphics();
            void ShutdownGraphics();

            // Window callback: handles engine-level events, then forwards to OnEvent().
            void HandleEvent(Event& e);
            void HandleResize(unsigned int width, unsigned int height);

            std::unique_ptr<Window> m_Window;
            LayerStack m_LayerStack;
            InputFrame m_InputFrame;
            bool m_Running = true;

            double m_FixedDeltaTime = 1.0 / 60.0;
            double m_Accumulator = 0.0;
    };

    // to be defined in Client Application
    Application* CreateApplication();

}
