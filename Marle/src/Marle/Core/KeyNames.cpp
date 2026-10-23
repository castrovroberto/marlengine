#include "mrlpch.h"
#include "KeyNames.h"
#include "KeyCodes.h"
#include "Marle/InputFrame.h"
#include <unordered_map>

namespace Marle {

    namespace {

        struct Names {
            std::vector<std::string> InOrder;
            std::unordered_map<std::string, int> ByName;
            std::unordered_map<int, std::string> ByCode;

            void Add(int code, const std::string& name)
            {
                InOrder.push_back(name);
                ByName.emplace(name, code);
                ByCode.emplace(code, name);
            }

            Names()
            {
                for (int i = 0; i < 26; i++) Add(Key::A + i, std::string(1, (char)('A' + i)));
                for (int i = 0; i < 10; i++) Add(Key::Num0 + i, std::string(1, (char)('0' + i)));

                Add(Key::Space, "Space");
                Add(Key::Up, "Up");
                Add(Key::Down, "Down");
                Add(Key::Left, "Left");
                Add(Key::Right, "Right");
                Add(Key::Enter, "Enter");
                Add(Key::Escape, "Escape");
                Add(Key::Tab, "Tab");
                Add(Key::Backspace, "Backspace");
                Add(Key::Insert, "Insert");
                Add(Key::Delete, "Delete");

                for (int i = 0; i < 12; i++) Add(Key::F1 + i, "F" + std::to_string(i + 1));

                Add(Key::LeftShift, "LeftShift");
                Add(Key::RightShift, "RightShift");
                Add(Key::LeftControl, "LeftControl");
                Add(Key::RightControl, "RightControl");
                Add(Key::LeftAlt, "LeftAlt");
                Add(Key::RightAlt, "RightAlt");
                Add(Key::LeftSuper, "LeftSuper");
                Add(Key::RightSuper, "RightSuper");

                for (int i = 0; i < 10; i++) Add(Key::NumPad0 + i, "NumPad" + std::to_string(i));
                Add(Key::NumPadDecimal, "NumPadDecimal");
                Add(Key::NumPadDivide, "NumPadDivide");
                Add(Key::NumPadMultiply, "NumPadMultiply");
                Add(Key::NumPadSubtract, "NumPadSubtract");
                Add(Key::NumPadAdd, "NumPadAdd");
                Add(Key::NumPadEnter, "NumPadEnter");
                Add(Key::NumPadEqual, "NumPadEqual");

                Add(Key::Apostrophe, "Apostrophe");
                Add(Key::Comma, "Comma");
                Add(Key::Minus, "Minus");
                Add(Key::Period, "Period");
                Add(Key::Slash, "Slash");
                Add(Key::Semicolon, "Semicolon");
                Add(Key::Equal, "Equal");
                Add(Key::LeftBracket, "LeftBracket");
                Add(Key::Backslash, "Backslash");
                Add(Key::RBracket, "RightBracket");
                Add(Key::GraveAccent, "GraveAccent");

                Add(Key::CapsLock, "CapsLock");
                Add(Key::ScrollLock, "ScrollLock");
                Add(Key::NumLock, "NumLock");
                Add(Key::PrintScreen, "PrintScreen");
                Add(Key::Pause, "Pause");
                Add(Key::Menu, "Menu");
            }
        };

        const Names& GetNames()
        {
            static const Names names;
            return names;
        }

    }

    const char* KeyName(int keyCode)
    {
        const auto& byCode = GetNames().ByCode;
        auto it = byCode.find(keyCode);
        return it == byCode.end() ? nullptr : it->second.c_str();
    }

    int KeyFromName(const std::string& name)
    {
        const auto& byName = GetNames().ByName;
        auto it = byName.find(name);
        return it == byName.end() ? Key::Unknown : it->second;
    }

    const std::vector<std::string>& KeyNames()
    {
        return GetNames().InOrder;
    }

}
