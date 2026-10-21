// Polled-input state tests. Headless: feeds events straight into Input::OnEvent.
// Exits non-zero if any check fails.
#include "Marle/Input.h"
#include "Marle/Core/KeyCodes.h"
#include "Marle/Core/MouseCodes.h"
#include "Marle/Events/ApplicationEvent.h"
#include "Marle/Events/KeyEvent.h"
#include "Marle/Events/MouseEvent.h"
#include <cstdio>

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

    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
