// Polled-input state tests. Headless: feeds events straight into Input::OnEvent.
// Exits non-zero if any check fails.
#include "Marle/Input.h"
#include "Marle/InputFrame.h"
#include "Marle/Core/KeyNames.h"
#include "Marle/Core/KeyCodes.h"
#include "Marle/Core/MouseCodes.h"
#include "Marle/Events/ApplicationEvent.h"
#include "Marle/Events/KeyEvent.h"
#include "Marle/Events/MouseEvent.h"
#include <cstdio>
#include <set>
#include <string>

using namespace Marle;

static int fails = 0;
#define CHECK(c, name) do { if (!(c)) { fails++; printf("FAIL: %s\n", name); } else { printf("ok:   %s\n", name); } } while (0)

template <typename E> static void Send(E e) { Input::OnEvent(e); }

int main()
{
    CHECK(!Input::IsKeyPressed(Key::A), "nothing pressed initially");
    CHECK(Input::GetMousePosition() == glm::vec2(0.0f), "mouse starts at (0,0)");

    Send(KeyPressedEvent(Key::A, 0));
    CHECK(Input::IsKeyPressed(Key::A), "key down after press");
    CHECK(!Input::IsKeyPressed(Key::B), "other keys unaffected");

    Send(KeyPressedEvent(Key::A, 1));
    CHECK(Input::IsKeyPressed(Key::A), "key stays down on repeat");

    Send(KeyReleasedEvent(Key::A));
    CHECK(!Input::IsKeyPressed(Key::A), "key up after release");

    Send(KeyPressedEvent(Key::Menu, 0));
    CHECK(Input::IsKeyPressed(Key::Menu), "highest key code is tracked");
    Send(KeyReleasedEvent(Key::Menu));

    CHECK(!Input::IsKeyPressed(Key::Unknown) && !Input::IsKeyPressed(100000), "out-of-range key codes are false");
    Send(KeyPressedEvent(Key::Unknown, 0));
    Send(KeyPressedEvent(100000, 0));
    CHECK(!Input::IsKeyPressed(Key::Unknown), "out-of-range press events are ignored");

    Send(MouseButtonPressedEvent(Mouse::Left));
    Send(MouseButtonPressedEvent(Mouse::Right));
    CHECK(Input::IsMouseButtonPressed(Mouse::Left) && Input::IsMouseButtonPressed(Mouse::Right), "mouse buttons down");
    CHECK(!Input::IsMouseButtonPressed(Mouse::Middle), "middle button unaffected");
    Send(MouseButtonReleasedEvent(Mouse::Left));
    CHECK(!Input::IsMouseButtonPressed(Mouse::Left) && Input::IsMouseButtonPressed(Mouse::Right), "mouse button release is per-button");

    Send(MouseMovedEvent(12.5f, 34.0f));
    CHECK(Input::GetMouseX() == 12.5f && Input::GetMouseY() == 34.0f, "mouse position follows move events");

    Send(KeyPressedEvent(Key::LeftShift, 0));
    Send(WindowFocusEvent());
    CHECK(Input::IsKeyPressed(Key::LeftShift), "gaining focus keeps state");
    Send(WindowLostFocusEvent());
    CHECK(!Input::IsKeyPressed(Key::LeftShift) && !Input::IsMouseButtonPressed(Mouse::Right), "losing focus releases keys and buttons");
    CHECK(Input::GetMouseX() == 12.5f, "losing focus keeps mouse position");

    // ---- InputFrame, built by hand
    {
        InputFrame frame;
        CHECK(!frame.IsDown(Key::W) && !frame.WasPressed(Key::W) && frame.Scroll == 0.0f && frame.Down.none() && frame.Pressed.none(), "a new input frame is empty");

        frame.Press(Key::W).Hold(Key::LeftShift);
        CHECK(frame.IsDown(Key::W) && frame.WasPressed(Key::W), "Press makes a key down and newly pressed");
        CHECK(frame.IsDown(Key::LeftShift) && !frame.WasPressed(Key::LeftShift), "Hold makes a key down but not newly pressed");
        frame.Release(Key::W);
        CHECK(!frame.IsDown(Key::W) && frame.WasPressed(Key::W), "a tap shows as pressed without being down");

        InputFrame before = frame;
        frame.Press(-1).Press(MaxKeys).Press(100000).Hold(-7).Hold(MaxKeys + 1).Release(-3).Release(MaxKeys);
        CHECK(frame == before && !frame.IsDown(-1) && !frame.IsDown(MaxKeys) && !frame.WasPressed(100000) && !frame.IsDown(Key::Unknown), "keys out of range are ignored and are never down");
        frame.Press(Key::NumPadEqual).Press(Key::Menu).Press(Key::Escape);
        CHECK(frame.IsDown(Key::NumPadEqual) && frame.IsDown(Key::Menu) && frame.IsDown(Key::Escape), "high and low key codes work");

        InputFrame a, b;
        a.Press(Key::A); b.Press(Key::A);
        CHECK(a == b && !(a != b), "frames with the same keys are equal");
        b.Scroll = 1.0f;
        CHECK(a != b, "a different scroll makes them differ");
        b.Scroll = 0.0f; b.Hold(Key::B);
        CHECK(a != b, "and so does a different key");
    }

    // ---- Input::TakeFrame: what a fixed step sees
    {
        Input::TakeFrame();   // drop whatever the earlier checks left
        Send(KeyPressedEvent(Key::W, 0));
        InputFrame first = Input::TakeFrame();
        CHECK(first.IsDown(Key::W) && first.WasPressed(Key::W) && first.Scroll == 0.0f, "a key pressed since the last frame is down and newly pressed");
        InputFrame held = Input::TakeFrame();
        CHECK(held.IsDown(Key::W) && !held.WasPressed(Key::W), "the next frame still has it down, but it is no longer new");
        Send(KeyPressedEvent(Key::W, 1));
        Send(KeyPressedEvent(Key::W, 2));
        InputFrame repeat = Input::TakeFrame();
        CHECK(repeat.IsDown(Key::W) && !repeat.WasPressed(Key::W), "key repeats are not new presses");
        Send(KeyReleasedEvent(Key::W));
        InputFrame released = Input::TakeFrame();
        CHECK(!released.IsDown(Key::W) && !released.WasPressed(Key::W) && released.Down.none() && released.Pressed.none(), "a released key is gone from the next frame");

        // taps and several keys
        Send(KeyPressedEvent(Key::S, 0));
        Send(KeyReleasedEvent(Key::S));
        InputFrame tap = Input::TakeFrame();
        CHECK(!tap.IsDown(Key::S) && tap.WasPressed(Key::S), "a key pressed and released between two frames is pressed but not down");
        Send(KeyPressedEvent(Key::D, 0));
        Send(KeyReleasedEvent(Key::D));
        Send(KeyPressedEvent(Key::D, 0));
        InputFrame twice = Input::TakeFrame();
        CHECK(twice.IsDown(Key::D) && twice.WasPressed(Key::D) && twice.Pressed.count() == 1, "pressing again between frames is still one new press");
        Send(KeyReleasedEvent(Key::D));
        Input::TakeFrame();
        Send(KeyPressedEvent(Key::Q, 0));
        Send(KeyPressedEvent(Key::E, 0));
        Send(KeyPressedEvent(Key::LeftShift, 0));
        InputFrame several = Input::TakeFrame();
        CHECK(several.Down.count() == 3 && several.Pressed.count() == 3 && several.IsDown(Key::Q) && several.IsDown(Key::E) && several.IsDown(Key::LeftShift), "several keys at once");
        Send(KeyReleasedEvent(Key::Q)); Send(KeyReleasedEvent(Key::E)); Send(KeyReleasedEvent(Key::LeftShift));
        Input::TakeFrame();

        // events split cleanly between frames
        Send(KeyPressedEvent(Key::A, 0));
        InputFrame one = Input::TakeFrame();
        Send(KeyPressedEvent(Key::B, 0));
        InputFrame two = Input::TakeFrame();
        CHECK(one.WasPressed(Key::A) && !one.WasPressed(Key::B) && two.WasPressed(Key::B) && !two.WasPressed(Key::A) && two.IsDown(Key::A), "a press goes into exactly one frame");
        Send(KeyReleasedEvent(Key::A)); Send(KeyReleasedEvent(Key::B));
        Input::TakeFrame();

        // the wheel
        Send(MouseScrolledEvent(0.0f, 2.0f));
        Send(MouseScrolledEvent(5.0f, -0.5f));
        InputFrame wheel = Input::TakeFrame();
        CHECK(wheel.Scroll == 1.5f, "wheel movement adds up over a frame, and only the vertical counts");
        CHECK(Input::TakeFrame().Scroll == 0.0f, "and is gone from the next one");
        Send(MouseScrolledEvent(0.0f, -3.0f));
        CHECK(Input::TakeFrame().Scroll == -3.0f, "scrolling the other way is negative");

        // what it ignores
        Send(KeyPressedEvent(100000, 0));
        Send(KeyPressedEvent(Key::Unknown, 0));
        Send(MouseMovedEvent(1.0f, 2.0f));
        Send(MouseButtonPressedEvent(Mouse::Left));
        InputFrame ignored = Input::TakeFrame();
        CHECK(ignored.Down.none() && ignored.Pressed.none() && ignored.Scroll == 0.0f, "keys out of range, the mouse position and mouse buttons are not in the frame");
        Send(MouseButtonReleasedEvent(Mouse::Left));

        // losing focus drops what was waiting
        Send(KeyPressedEvent(Key::D, 0));
        Send(MouseScrolledEvent(0.0f, 3.0f));
        Send(WindowLostFocusEvent());
        InputFrame lost = Input::TakeFrame();
        CHECK(lost.Down.none() && lost.Pressed.none() && lost.Scroll == 0.0f, "losing focus drops held keys, waiting presses and wheel movement");

        // a frame is a snapshot
        Send(KeyPressedEvent(Key::Z, 0));
        InputFrame snapshot = Input::TakeFrame();
        Send(KeyReleasedEvent(Key::Z));
        CHECK(snapshot.IsDown(Key::Z) && !Input::TakeFrame().IsDown(Key::Z), "a frame already taken does not change when keys are released");

        // frames mirror polling
        Send(KeyPressedEvent(Key::H, 0));
        Send(KeyPressedEvent(Key::F5, 0));
        InputFrame mirror = Input::TakeFrame();
        bool same = true;
        for (int key = -2; key < MaxKeys + 2; key++) same = same && mirror.IsDown(key) == Input::IsKeyPressed(key);
        CHECK(same && mirror.IsDown(Key::H) && mirror.IsDown(Key::F5), "a frame's Down is what polling says, for every key code");
        Send(KeyReleasedEvent(Key::H)); Send(KeyReleasedEvent(Key::F5));
        Input::TakeFrame();
    }

    // ---- key names
    {
        // every Marle::Key constant (keep in step with Core/KeyCodes.h) has a name that names it back
        std::vector<int> every = { Key::Space };
        for (int k = Key::Num0; k <= Key::Num9; k++) every.push_back(k);
        for (int k = Key::A; k <= Key::Z; k++) every.push_back(k);
        for (int k : { Key::Escape, Key::Enter, Key::Tab, Key::Backspace, Key::Insert, Key::Delete, Key::Right, Key::Left, Key::Down, Key::Up }) every.push_back(k);
        for (int k = Key::F1; k <= Key::F12; k++) every.push_back(k);
        for (int k : { Key::LeftShift, Key::RightShift, Key::LeftControl, Key::RightControl, Key::LeftAlt, Key::RightAlt, Key::LeftSuper, Key::RightSuper }) every.push_back(k);
        for (int k = Key::NumPad0; k <= Key::NumPad9; k++) every.push_back(k);
        for (int k : { Key::NumPadDecimal, Key::NumPadDivide, Key::NumPadMultiply, Key::NumPadSubtract, Key::NumPadAdd, Key::NumPadEnter, Key::NumPadEqual }) every.push_back(k);
        for (int k : { Key::Apostrophe, Key::Comma, Key::Minus, Key::Period, Key::Slash, Key::Semicolon, Key::Equal, Key::LeftBracket, Key::Backslash, Key::RBracket, Key::GraveAccent }) every.push_back(k);
        for (int k : { Key::CapsLock, Key::ScrollLock, Key::NumLock, Key::PrintScreen, Key::Pause, Key::Menu }) every.push_back(k);

        bool named = true, roundTrips = true, inRange = true;
        std::set<int> codes;
        for (int k : every) {
            const char* name = KeyName(k);
            named = named && name != nullptr;
            roundTrips = roundTrips && name && KeyFromName(name) == k;
            inRange = inRange && k >= 0 && k < MaxKeys;
            codes.insert(k);
        }
        CHECK(named && roundTrips && inRange, "every key has a name, and the name gives the key back");
        CHECK(codes.size() == every.size() && KeyNames().size() == every.size(), "the key list has no repeats and the name list is exactly as long");

        // the names, and no others, are in KeyNames()
        std::set<std::string> names(KeyNames().begin(), KeyNames().end());
        bool listed = names.size() == KeyNames().size();
        for (int k : every) listed = listed && names.count(KeyName(k)) == 1;
        CHECK(listed, "KeyNames lists each name once");
        bool back = true;
        for (const std::string& name : KeyNames()) { int code = KeyFromName(name); back = back && code != Key::Unknown && std::string(KeyName(code)) == name; }
        CHECK(back, "and each of them names a key that has it");

        // every code from 0 up either has a name that round trips or has none
        bool consistent = true;
        int namedCodes = 0;
        for (int code = -5; code < MaxKeys + 5; code++) {
            const char* name = KeyName(code);
            if (name) { namedCodes++; consistent = consistent && KeyFromName(name) == code; }
        }
        CHECK(consistent && namedCodes == (int)every.size(), "no code outside the list has a name");

        CHECK(std::string(KeyName(Key::W)) == "W" && std::string(KeyName(Key::Num7)) == "7" && std::string(KeyName(Key::Space)) == "Space" && std::string(KeyName(Key::LeftShift)) == "LeftShift" && std::string(KeyName(Key::F12)) == "F12" && std::string(KeyName(Key::NumPad3)) == "NumPad3" && std::string(KeyName(Key::NumPadEnter)) == "NumPadEnter" && std::string(KeyName(Key::RBracket)) == "RightBracket" && std::string(KeyName(Key::Up)) == "Up" && std::string(KeyName(Key::Escape)) == "Escape", "names are spelled the way scene files spell them");
        CHECK(KeyFromName("W") == Key::W && KeyFromName("7") == Key::Num7 && KeyFromName("RightBracket") == Key::RBracket && KeyFromName("F1") == Key::F1, "and read back");
        CHECK(KeyFromName("w") == Key::Unknown && KeyFromName("space") == Key::Unknown && KeyFromName("") == Key::Unknown && KeyFromName("Nope") == Key::Unknown && KeyFromName("RBracket") == Key::Unknown && KeyFromName("W ") == Key::Unknown, "names are exact: a different case, a space, an empty or an unknown name give no key");
        CHECK(KeyName(Key::Unknown) == nullptr && KeyName(100000) == nullptr && KeyName(-5) == nullptr && KeyName(0) == nullptr, "codes that are not keys have no name");
        CHECK(KeyNames().front() == "A" && KeyNames()[25] == "Z" && KeyNames()[26] == "0" && KeyNames()[35] == "9" && KeyNames()[36] == "Space", "the list starts with the letters, then the digits");
    }

    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
