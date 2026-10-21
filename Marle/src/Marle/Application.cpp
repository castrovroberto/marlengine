#include "mrlpch.h"
#include "Log.h"
#include "Application.h"
#include "Events/KeyEvent.h"
#include "Events/ApplicationEvent.h"
#include "Core/KeyCodes.h"
#include "Input.h"
#include "Renderer/Renderer2D.h"

#include <glad/gl.h>
#include <chrono>

namespace Marle {

    static double GetCurrentTimeSeconds()
    {
        using clock = std::chrono::steady_clock;
        return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
    }

    Application::Application(const WindowProps& props)
    {
        MRL_CORE_INFO("Creating Marle Application: {}", props.Title);

        m_Window = Window::Create(props);
        if (!m_Window) {
            MRL_CORE_ERROR("Failed to create window!");
            m_Running = false;
            return;
        }
        m_Window->SetEventCallback([this](Event& e) { HandleEvent(e); });

        InitGraphics();
    }

    Application::~Application()
    {
        MRL_CORE_INFO("Destroying Marle Application");
        m_LayerStack.Clear(); // layers may own GL resources, so go before the context
        if (m_Window) {
            ShutdownGraphics();
            m_Window.reset();
        }
    }

    void Application::InitGraphics()
    {
        MRL_CORE_INFO("  Vendor: {}", (const char*)glGetString(GL_VENDOR));
        MRL_CORE_INFO("  Renderer: {}", (const char*)glGetString(GL_RENDERER));
        MRL_CORE_INFO("  Version: {}", (const char*)glGetString(GL_VERSION));

        // Set initial OpenGL state
        glViewport(0, 0, m_Window->GetWidth(), m_Window->GetHeight());
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f); // Dark moody blue/gray

        Renderer2D::Init();
    }

    void Application::ShutdownGraphics()
    {
        // Must happen while the window's GL context is still alive.
        Renderer2D::Shutdown();
    }

    void Application::HandleEvent(Event& e)
    {
        Input::OnEvent(e);

        switch (e.GetEventType()) {
            case EventType::WindowClose:
                m_Running = false;
                break;
            case EventType::WindowResize: {
                auto& resize = static_cast<WindowResizeEvent&>(e);
                HandleResize(resize.GetWidth(), resize.GetHeight());
                break;
            }
            case EventType::KeyPressed:
                if (static_cast<KeyPressedEvent&>(e).GetKeyCode() == Key::Escape)
                    m_Running = false;
                break;
            default:
                break;
        }

        m_LayerStack.DispatchEvent(e);
        if (!e.IsHandled())
            OnEvent(e);
    }

    void Application::HandleResize(unsigned int width, unsigned int height)
    {
        glViewport(0, 0, (GLsizei)width, (GLsizei)height);
        OnResize(width, height);
    }

    void Application::OnEvent(Event& e)
    {
        MRL_CORE_TRACE("Application::OnEvent - {}", e.ToString());
        
        // Handle application-level events here
        // For now, just log them
    }

    void Application::OnUpdate(double fixed_dt) {
        // Base implementation can be empty or provide some core engine update logic if needed in the future.
    }

    void Application::OnRender(double interpolation_alpha) {
        // This is where rendering commands specific to the application/game state will go.
        // For now, we'll keep the clear color and buffer swap in the main Run() loop,
        // but eventually, those might move into a more structured rendering system called from here.
    }

    void Application::Run()
    {
        MRL_CORE_INFO("Application Run loop started");

        double last_time = GetCurrentTimeSeconds();

        while (m_Running)
        {
            // 1. Calculate Frame Time
            double current_time = GetCurrentTimeSeconds();
            double frame_time = current_time - last_time;
            last_time = current_time;

            // Optional: Clamp frame_time to avoid spiral of death if debugging or extreme stalls
            if (frame_time > 0.25) { // Max frame time step e.g. 0.25s
                frame_time = 0.25;
            }

            m_Accumulator += frame_time;

            // 2. Fixed Updates for Game Logic
            while (m_Accumulator >= m_FixedDeltaTime) {
                // --- Input Polling could go here if needed (e.g., IsKeyPressed) ---
                
                // --- Core Engine Subsystem Updates (e.g., Physics, Animation) ---
                // (None yet)

                // --- Game Specific Update ---
                OnUpdate(m_FixedDeltaTime); // Call the virtual update method
                for (auto& layer : m_LayerStack)
                    layer->OnUpdate(m_FixedDeltaTime);

                m_Accumulator -= m_FixedDeltaTime;
            }

            // 3. Rendering
            double interpolation_alpha = m_Accumulator / m_FixedDeltaTime; // For smooth rendering between states

            glClear(GL_COLOR_BUFFER_BIT);

            // --- Call Game Specific Render ---
            OnRender(interpolation_alpha); // Call the virtual render method
            for (auto& layer : m_LayerStack)
                layer->OnRender(interpolation_alpha);

            // --- UI Rendering / Debug Rendering could go here ---

            // 4. Process events and swap buffers
            m_Window->OnUpdate();
        }
        
        MRL_CORE_INFO("Application Run loop ended");
    }

}
