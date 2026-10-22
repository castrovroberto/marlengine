#include "mrlpch.h"
#include "ImGuiLayer.h"
#include "Marle/Log.h"
#include "Marle/Core/KeyCodes.h"
#include "Marle/Core/MouseCodes.h"
#include "Marle/Events/KeyEvent.h"
#include "Marle/Events/MouseEvent.h"
#include "Marle/Events/ApplicationEvent.h"

#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>
#include <glad/gl.h>
#include <chrono>

namespace Marle {

    static double GetCurrentTimeSeconds()
    {
        using clock = std::chrono::steady_clock;
        return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
    }

    // The framebuffer size Application keeps the GL viewport at.
    static ImVec2 GetViewportSize()
    {
        GLint viewport[4] = { 0, 0, 0, 0 };
        glGetIntegerv(GL_VIEWPORT, viewport);
        return ImVec2((float)viewport[2], (float)viewport[3]);
    }

    static ImGuiKey ToImGuiKey(int key)
    {
        if (key >= Key::A && key <= Key::Z)             return (ImGuiKey)(ImGuiKey_A + (key - Key::A));
        if (key >= Key::Num0 && key <= Key::Num9)      return (ImGuiKey)(ImGuiKey_0 + (key - Key::Num0));
        if (key >= Key::F1 && key <= Key::F12)          return (ImGuiKey)(ImGuiKey_F1 + (key - Key::F1));
        if (key >= Key::NumPad0 && key <= Key::NumPad9) return (ImGuiKey)(ImGuiKey_Keypad0 + (key - Key::NumPad0));

        switch (key) {
            case Key::Space:          return ImGuiKey_Space;
            case Key::Escape:         return ImGuiKey_Escape;
            case Key::Enter:          return ImGuiKey_Enter;
            case Key::Tab:            return ImGuiKey_Tab;
            case Key::Backspace:      return ImGuiKey_Backspace;
            case Key::Insert:         return ImGuiKey_Insert;
            case Key::Delete:         return ImGuiKey_Delete;
            case Key::Right:          return ImGuiKey_RightArrow;
            case Key::Left:           return ImGuiKey_LeftArrow;
            case Key::Down:           return ImGuiKey_DownArrow;
            case Key::Up:             return ImGuiKey_UpArrow;
            case Key::LeftShift:      return ImGuiKey_LeftShift;
            case Key::RightShift:     return ImGuiKey_RightShift;
            case Key::LeftControl:    return ImGuiKey_LeftCtrl;
            case Key::RightControl:   return ImGuiKey_RightCtrl;
            case Key::LeftAlt:        return ImGuiKey_LeftAlt;
            case Key::RightAlt:       return ImGuiKey_RightAlt;
            case Key::LeftSuper:      return ImGuiKey_LeftSuper;
            case Key::RightSuper:     return ImGuiKey_RightSuper;
            case Key::NumPadDecimal:  return ImGuiKey_KeypadDecimal;
            case Key::NumPadDivide:   return ImGuiKey_KeypadDivide;
            case Key::NumPadMultiply: return ImGuiKey_KeypadMultiply;
            case Key::NumPadSubtract: return ImGuiKey_KeypadSubtract;
            case Key::NumPadAdd:      return ImGuiKey_KeypadAdd;
            case Key::NumPadEnter:    return ImGuiKey_KeypadEnter;
            case Key::NumPadEqual:    return ImGuiKey_KeypadEqual;
            case Key::Apostrophe:     return ImGuiKey_Apostrophe;
            case Key::Comma:          return ImGuiKey_Comma;
            case Key::Minus:          return ImGuiKey_Minus;
            case Key::Period:         return ImGuiKey_Period;
            case Key::Slash:          return ImGuiKey_Slash;
            case Key::Semicolon:      return ImGuiKey_Semicolon;
            case Key::Equal:          return ImGuiKey_Equal;
            case Key::LeftBracket:    return ImGuiKey_LeftBracket;
            case Key::Backslash:      return ImGuiKey_Backslash;
            case Key::RBracket:       return ImGuiKey_RightBracket;
            case Key::GraveAccent:    return ImGuiKey_GraveAccent;
            case Key::CapsLock:       return ImGuiKey_CapsLock;
            case Key::ScrollLock:     return ImGuiKey_ScrollLock;
            case Key::NumLock:        return ImGuiKey_NumLock;
            case Key::PrintScreen:    return ImGuiKey_PrintScreen;
            case Key::Pause:          return ImGuiKey_Pause;
            case Key::Menu:           return ImGuiKey_Menu;
            default:                  return ImGuiKey_None;
        }
    }

    ImGuiLayer::ImGuiLayer(const LayerStack& layers)
        : Layer("ImGuiLayer"), m_Layers(layers)
    {
    }

    void ImGuiLayer::OnAttach()
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr; // don't write imgui.ini into the working directory
        io.BackendPlatformName = "marle";

        ImGui::StyleColorsDark();
        ImGui_ImplOpenGL3_Init("#version 330 core");

        m_LastTime = GetCurrentTimeSeconds();
    }

    void ImGuiLayer::OnDetach()
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext();
    }

    void ImGuiLayer::OnRender(double)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = GetViewportSize();

        double now = GetCurrentTimeSeconds();
        io.DeltaTime = (float)std::max(now - m_LastTime, 1e-5);
        m_LastTime = now;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        for (auto& layer : m_Layers)
            layer->OnImGuiRender();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void ImGuiLayer::OnEvent(Event& e)
    {
        ImGuiIO& io = ImGui::GetIO();

        switch (e.GetEventType()) {
            case EventType::MouseMoved: {
                auto& m = static_cast<MouseMovedEvent&>(e);
                // Marle's mouse origin is bottom-left, ImGui's is top-left
                io.AddMousePosEvent(m.GetX(), GetViewportSize().y - m.GetY());
                break;
            }
            case EventType::MouseButtonPressed:
            case EventType::MouseButtonReleased: {
                int button = static_cast<MouseButtonEvent&>(e).GetMouseButton();
                io.AddMouseButtonEvent(button, e.GetEventType() == EventType::MouseButtonPressed);
                if (io.WantCaptureMouse)
                    e.SetHandled();
                break;
            }
            case EventType::MouseScrolled: {
                auto& s = static_cast<MouseScrolledEvent&>(e);
                io.AddMouseWheelEvent(s.GetXOffset(), s.GetYOffset());
                if (io.WantCaptureMouse)
                    e.SetHandled();
                break;
            }
            case EventType::KeyPressed:
            case EventType::KeyReleased: {
                ImGuiKey key = ToImGuiKey(static_cast<KeyEvent&>(e).GetKeyCode());
                if (key != ImGuiKey_None)
                    io.AddKeyEvent(key, e.GetEventType() == EventType::KeyPressed);
                if (io.WantCaptureKeyboard)
                    e.SetHandled();
                break;
            }
            case EventType::KeyTyped:
                io.AddInputCharacter((unsigned int)static_cast<KeyTypedEvent&>(e).GetKeyCode());
                if (io.WantCaptureKeyboard)
                    e.SetHandled();
                break;
            case EventType::WindowFocus:
                io.AddFocusEvent(true);
                break;
            case EventType::WindowLostFocus:
                io.AddFocusEvent(false);
                break;
            default:
                break;
        }
    }

}
