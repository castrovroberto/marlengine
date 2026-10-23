#include "mrlpch.h"
#include "Input.h"
#include "Core/KeyCodes.h"
#include "Core/MouseCodes.h"
#include "Events/ApplicationEvent.h"
#include "Events/KeyEvent.h"
#include "Events/MouseEvent.h"

#include <bitset>

namespace Marle {

    static constexpr int MaxMouseButtons = 8;
    static_assert(Key::Menu < MaxKeys, "key table too small for Marle::Key");

    static std::bitset<MaxKeys> s_Keys;
    static std::bitset<MaxKeys> s_PressedSinceFrame;   // initial presses since TakeFrame() last ran
    static float s_ScrollSinceFrame = 0.0f;
    static std::bitset<MaxMouseButtons> s_MouseButtons;
    static glm::vec2 s_MousePosition(0.0f);

    bool Input::IsKeyPressed(int keyCode)
    {
        return keyCode >= 0 && keyCode < MaxKeys && s_Keys[keyCode];
    }

    bool Input::IsMouseButtonPressed(int button)
    {
        return button >= 0 && button < MaxMouseButtons && s_MouseButtons[button];
    }

    glm::vec2 Input::GetMousePosition()
    {
        return s_MousePosition;
    }

    InputFrame Input::TakeFrame()
    {
        InputFrame frame;
        frame.Down = s_Keys;
        frame.Pressed = s_PressedSinceFrame;
        frame.Scroll = s_ScrollSinceFrame;
        s_PressedSinceFrame.reset();
        s_ScrollSinceFrame = 0.0f;
        return frame;
    }

    void Input::OnEvent(const Event& e)
    {
        switch (e.GetEventType()) {
            case EventType::KeyPressed: {
                if (e.IsHandled()) break;
                auto& pressed = static_cast<const KeyPressedEvent&>(e);
                int key = pressed.GetKeyCode();
                if (key >= 0 && key < MaxKeys) {
                    s_Keys.set(key);
                    if (pressed.GetRepeatCount() == 0) s_PressedSinceFrame.set(key);   // a repeat is the same press
                }
                break;
            }
            case EventType::MouseScrolled:
                if (e.IsHandled()) break;
                s_ScrollSinceFrame += static_cast<const MouseScrolledEvent&>(e).GetYOffset();
                break;
            case EventType::KeyReleased: {
                int key = static_cast<const KeyReleasedEvent&>(e).GetKeyCode();
                if (key >= 0 && key < MaxKeys) s_Keys.reset(key);
                break;
            }
            case EventType::MouseButtonPressed: {
                if (e.IsHandled()) break;
                int button = static_cast<const MouseButtonPressedEvent&>(e).GetMouseButton();
                if (button >= 0 && button < MaxMouseButtons) s_MouseButtons.set(button);
                break;
            }
            case EventType::MouseButtonReleased: {
                int button = static_cast<const MouseButtonReleasedEvent&>(e).GetMouseButton();
                if (button >= 0 && button < MaxMouseButtons) s_MouseButtons.reset(button);
                break;
            }
            case EventType::MouseMoved: {
                auto& moved = static_cast<const MouseMovedEvent&>(e);
                s_MousePosition = { moved.GetX(), moved.GetY() };
                break;
            }
            case EventType::WindowLostFocus:
                // We won't see the release events for anything held right now.
                s_Keys.reset();
                s_MouseButtons.reset();
                s_PressedSinceFrame.reset();
                s_ScrollSinceFrame = 0.0f;
                break;
            default:
                break;
        }
    }

}
