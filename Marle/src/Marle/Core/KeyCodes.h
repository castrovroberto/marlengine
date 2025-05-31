#pragma once

namespace Marle {
    namespace Key {
        enum : int {
            Unknown = -1,
            
            // Printable keys
            Space = 32,
            
            // Numbers
            Num0 = 48, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
            
            // Letters
            A = 65, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
            
            // Special keys
            Escape = 256,
            Enter,
            Tab,
            Backspace,
            Insert,
            Delete,
            
            // Arrow keys
            Right,
            Left,
            Down,
            Up,
            
            // Function keys
            F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
            
            // Modifier keys
            LeftShift,
            RightShift,
            LeftControl,
            RightControl,
            LeftAlt,
            RightAlt,
            LeftSuper,    // Command key on macOS
            RightSuper,
            
            // Numpad keys
            NumPad0,
            NumPad1,
            NumPad2,
            NumPad3,
            NumPad4,
            NumPad5,
            NumPad6,
            NumPad7,
            NumPad8,
            NumPad9,
            NumPadDecimal,
            NumPadDivide,
            NumPadMultiply,
            NumPadSubtract,
            NumPadAdd,
            NumPadEnter,
            NumPadEqual,
            
            // Other commonly used keys
            Apostrophe = 39,  // '
            Comma = 44,       // ,
            Minus = 45,       // -
            Period = 46,      // .
            Slash = 47,       // /
            Semicolon = 59,   // ;
            Equal = 61,       // =
            LeftBracket = 91, // [
            Backslash = 92,   // \
            RBracket = 93, // ]
            GraveAccent = 96, // `
            
            CapsLock,
            ScrollLock,
            NumLock,
            PrintScreen,
            Pause,
            
            Menu
        };
    }
} 