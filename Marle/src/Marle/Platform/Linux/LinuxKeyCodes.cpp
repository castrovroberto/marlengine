#include "mrlpch.h"
#include "LinuxKeyCodes.h"
#include "Marle/Core/KeyCodes.h"

#include <GLFW/glfw3.h>

namespace Marle {

    int TranslateGLFWKeyToMarle(int k)
    {
        // Letters, digits and most printable keys share their ASCII values with GLFW.
        if (k >= GLFW_KEY_A && k <= GLFW_KEY_Z) return k;
        if (k >= GLFW_KEY_0 && k <= GLFW_KEY_9) return k;

        switch (k) {
            case GLFW_KEY_SPACE:         return Key::Space;
            case GLFW_KEY_APOSTROPHE:    return Key::Apostrophe;
            case GLFW_KEY_COMMA:         return Key::Comma;
            case GLFW_KEY_MINUS:         return Key::Minus;
            case GLFW_KEY_PERIOD:        return Key::Period;
            case GLFW_KEY_SLASH:         return Key::Slash;
            case GLFW_KEY_SEMICOLON:     return Key::Semicolon;
            case GLFW_KEY_EQUAL:         return Key::Equal;
            case GLFW_KEY_LEFT_BRACKET:  return Key::LeftBracket;
            case GLFW_KEY_BACKSLASH:     return Key::Backslash;
            case GLFW_KEY_RIGHT_BRACKET: return Key::RBracket;
            case GLFW_KEY_GRAVE_ACCENT:  return Key::GraveAccent;

            case GLFW_KEY_ESCAPE:        return Key::Escape;
            case GLFW_KEY_ENTER:         return Key::Enter;
            case GLFW_KEY_TAB:           return Key::Tab;
            case GLFW_KEY_BACKSPACE:     return Key::Backspace;
            case GLFW_KEY_INSERT:        return Key::Insert;
            case GLFW_KEY_DELETE:        return Key::Delete;

            case GLFW_KEY_RIGHT:         return Key::Right;
            case GLFW_KEY_LEFT:          return Key::Left;
            case GLFW_KEY_DOWN:          return Key::Down;
            case GLFW_KEY_UP:            return Key::Up;

            case GLFW_KEY_F1:  return Key::F1;
            case GLFW_KEY_F2:  return Key::F2;
            case GLFW_KEY_F3:  return Key::F3;
            case GLFW_KEY_F4:  return Key::F4;
            case GLFW_KEY_F5:  return Key::F5;
            case GLFW_KEY_F6:  return Key::F6;
            case GLFW_KEY_F7:  return Key::F7;
            case GLFW_KEY_F8:  return Key::F8;
            case GLFW_KEY_F9:  return Key::F9;
            case GLFW_KEY_F10: return Key::F10;
            case GLFW_KEY_F11: return Key::F11;
            case GLFW_KEY_F12: return Key::F12;

            case GLFW_KEY_LEFT_SHIFT:    return Key::LeftShift;
            case GLFW_KEY_RIGHT_SHIFT:   return Key::RightShift;
            case GLFW_KEY_LEFT_CONTROL:  return Key::LeftControl;
            case GLFW_KEY_RIGHT_CONTROL: return Key::RightControl;
            case GLFW_KEY_LEFT_ALT:      return Key::LeftAlt;
            case GLFW_KEY_RIGHT_ALT:     return Key::RightAlt;
            case GLFW_KEY_LEFT_SUPER:    return Key::LeftSuper;
            case GLFW_KEY_RIGHT_SUPER:   return Key::RightSuper;

            case GLFW_KEY_KP_0:        return Key::NumPad0;
            case GLFW_KEY_KP_1:        return Key::NumPad1;
            case GLFW_KEY_KP_2:        return Key::NumPad2;
            case GLFW_KEY_KP_3:        return Key::NumPad3;
            case GLFW_KEY_KP_4:        return Key::NumPad4;
            case GLFW_KEY_KP_5:        return Key::NumPad5;
            case GLFW_KEY_KP_6:        return Key::NumPad6;
            case GLFW_KEY_KP_7:        return Key::NumPad7;
            case GLFW_KEY_KP_8:        return Key::NumPad8;
            case GLFW_KEY_KP_9:        return Key::NumPad9;
            case GLFW_KEY_KP_DECIMAL:  return Key::NumPadDecimal;
            case GLFW_KEY_KP_DIVIDE:   return Key::NumPadDivide;
            case GLFW_KEY_KP_MULTIPLY: return Key::NumPadMultiply;
            case GLFW_KEY_KP_SUBTRACT: return Key::NumPadSubtract;
            case GLFW_KEY_KP_ADD:      return Key::NumPadAdd;
            case GLFW_KEY_KP_ENTER:    return Key::NumPadEnter;
            case GLFW_KEY_KP_EQUAL:    return Key::NumPadEqual;

            case GLFW_KEY_CAPS_LOCK:    return Key::CapsLock;
            case GLFW_KEY_SCROLL_LOCK:  return Key::ScrollLock;
            case GLFW_KEY_NUM_LOCK:     return Key::NumLock;
            case GLFW_KEY_PRINT_SCREEN: return Key::PrintScreen;
            case GLFW_KEY_PAUSE:        return Key::Pause;
        }
        return Key::Unknown;
    }

}
