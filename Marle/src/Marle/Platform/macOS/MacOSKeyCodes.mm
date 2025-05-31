#include "MacOSKeyCodes.h"
#import <Cocoa/Cocoa.h>

namespace Marle {
    int TranslateMacOSKeyCodeToMarle(unsigned short nativeKeyCode) {
        switch (nativeKeyCode) {
            // Letters (using raw key codes)
            case 0:                         return Marle::Key::A;
            case 1:                         return Marle::Key::S;
            case 2:                         return Marle::Key::D;
            case 3:                         return Marle::Key::F;
            case 4:                         return Marle::Key::H;
            case 5:                         return Marle::Key::G;
            case 6:                         return Marle::Key::Z;
            case 7:                         return Marle::Key::X;
            case 8:                         return Marle::Key::C;
            case 9:                         return Marle::Key::V;
            case 11:                        return Marle::Key::B;
            case 12:                        return Marle::Key::Q;
            case 13:                        return Marle::Key::W;
            case 14:                        return Marle::Key::E;
            case 15:                        return Marle::Key::R;
            case 16:                        return Marle::Key::Y;
            case 17:                        return Marle::Key::T;
            case 18:                        return Marle::Key::Num1;
            case 19:                        return Marle::Key::Num2;
            case 20:                        return Marle::Key::Num3;
            case 21:                        return Marle::Key::Num4;
            case 22:                        return Marle::Key::Num6;
            case 23:                        return Marle::Key::Num5;
            case 24:                        return Marle::Key::Equal;
            case 25:                        return Marle::Key::Num9;
            case 26:                        return Marle::Key::Num7;
            case 27:                        return Marle::Key::Minus;
            case 28:                        return Marle::Key::Num8;
            case 29:                        return Marle::Key::Num0;
            case 30:                        return 93; // Right bracket key code directly
            case 31:                        return Marle::Key::O;
            case 32:                        return Marle::Key::U;
            case 33:                        return Marle::Key::LeftBracket;
            case 34:                        return Marle::Key::I;
            case 35:                        return Marle::Key::P;
            case 37:                        return Marle::Key::L;
            case 38:                        return Marle::Key::J;
            case 39:                        return Marle::Key::Apostrophe;
            case 40:                        return Marle::Key::K;
            case 41:                        return Marle::Key::Semicolon;
            case 42:                        return Marle::Key::Backslash;
            case 43:                        return Marle::Key::Comma;
            case 44:                        return Marle::Key::Slash;
            case 45:                        return Marle::Key::N;
            case 46:                        return Marle::Key::M;
            case 47:                        return Marle::Key::Period;
            case 50:                        return Marle::Key::GraveAccent;

            // Keypad
            case 65:                        return Marle::Key::NumPadDecimal;
            case 67:                        return Marle::Key::NumPadMultiply;
            case 69:                        return Marle::Key::NumPadAdd;
            case 71:                        return Marle::Key::NumLock;
            case 75:                        return Marle::Key::NumPadDivide;
            case 76:                        return Marle::Key::NumPadEnter;
            case 78:                        return Marle::Key::NumPadSubtract;
            case 81:                        return Marle::Key::NumPadEqual;
            case 82:                        return Marle::Key::NumPad0;
            case 83:                        return Marle::Key::NumPad1;
            case 84:                        return Marle::Key::NumPad2;
            case 85:                        return Marle::Key::NumPad3;
            case 86:                        return Marle::Key::NumPad4;
            case 87:                        return Marle::Key::NumPad5;
            case 88:                        return Marle::Key::NumPad6;
            case 89:                        return Marle::Key::NumPad7;
            case 91:                        return Marle::Key::NumPad8;
            case 92:                        return Marle::Key::NumPad9;

            // Special keys
            case 36:                        return Marle::Key::Enter;
            case 48:                        return Marle::Key::Tab;
            case 49:                        return Marle::Key::Space;
            case 51:                        return Marle::Key::Backspace;
            case 53:                        return Marle::Key::Escape;
            case 55:                        return Marle::Key::LeftSuper;
            case 56:                        return Marle::Key::LeftShift;
            case 57:                        return Marle::Key::CapsLock;
            case 58:                        return Marle::Key::LeftAlt;
            case 59:                        return Marle::Key::LeftControl;
            case 60:                        return Marle::Key::RightShift;
            case 61:                        return Marle::Key::RightAlt;
            case 62:                        return Marle::Key::RightControl;
            case 54:                        return Marle::Key::RightSuper;

            // Function keys
            case 122:                       return Marle::Key::F1;
            case 120:                       return Marle::Key::F2;
            case 99:                        return Marle::Key::F3;
            case 118:                       return Marle::Key::F4;
            case 96:                        return Marle::Key::F5;
            case 97:                        return Marle::Key::F6;
            case 98:                        return Marle::Key::F7;
            case 100:                       return Marle::Key::F8;
            case 101:                       return Marle::Key::F9;
            case 109:                       return Marle::Key::F10;
            case 103:                       return Marle::Key::F11;
            case 111:                       return Marle::Key::F12;

            // Arrow keys
            case 123:                       return Marle::Key::Left;
            case 124:                       return Marle::Key::Right;
            case 125:                       return Marle::Key::Down;
            case 126:                       return Marle::Key::Up;

            // Other keys
            case 117:                       return Marle::Key::Delete;
            case 114:                       return Marle::Key::Insert;

            default:                        return Marle::Key::Unknown;
        }
    }
} 