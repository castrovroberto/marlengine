#pragma once

#include "Core.h"
#include "Events/Event.h"
#include "InputFrame.h"
#include <glm/glm.hpp>

namespace Marle {

    // Polled input state, kept up to date from window events by the Application.
    // State changes land when the window pumps events (end of each frame), so every
    // OnUpdate/OnRender in a frame sees the same state. Held keys and buttons are
    // released when the window loses focus.
    class EXPORT Input
    {
        public:
            static bool IsKeyPressed(int keyCode);              // Marle::Key::*
            static bool IsMouseButtonPressed(int button);       // Marle::Mouse::*

            // Window coordinates, origin bottom-left; (0,0) until the mouse first moves.
            static glm::vec2 GetMousePosition();
            static float GetMouseX() { return GetMousePosition().x; }
            static float GetMouseY() { return GetMousePosition().y; }

            // The input frame for one fixed step: the keys held now, plus the key presses and wheel movement
            // that arrived since the last call, which it then forgets. Application calls it once per fixed
            // step, so with several steps in one rendered frame only the first sees the new presses and
            // scroll. Key repeats are not presses. Losing focus drops presses that were not yet delivered.
            static InputFrame TakeFrame();

            // Feeds an event into the state. Called by Application; exposed for tests.
            static void OnEvent(const Event& e);
    };

}
