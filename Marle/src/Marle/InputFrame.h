#pragma once

#include "Core.h"
#include <bitset>

namespace Marle {

    // Key codes are below this (Marle::Key is far smaller)
    constexpr int MaxKeys = 512;

    // What the player's input was during one fixed step, as behaviors see it (ADR-0002). They read
    // this and never poll Input, so a recording can stand in for the keyboard and a headless test can
    // build one by hand. Application makes one per fixed step (Application::GetInputFrame()).
    struct EXPORT InputFrame
    {
        std::bitset<MaxKeys> Down;       // keys held during this step
        std::bitset<MaxKeys> Pressed;    // keys that went down since the previous frame; a tap that was
                                         // released again before the step shows here but not in Down
        float Scroll = 0.0f;             // vertical mouse wheel movement since the previous frame

        // Marle::Key codes; anything out of range is a key that is not down
        bool IsDown(int key) const { return key >= 0 && key < MaxKeys && Down[key]; }
        bool WasPressed(int key) const { return key >= 0 && key < MaxKeys && Pressed[key]; }

        // For building a frame by hand (tests) or from a recording; they ignore keys out of range
        InputFrame& Press(int key) { if (key >= 0 && key < MaxKeys) { Down.set(key); Pressed.set(key); } return *this; }   // goes down now
        InputFrame& Hold(int key) { if (key >= 0 && key < MaxKeys) Down.set(key); return *this; }                          // was already down
        InputFrame& Release(int key) { if (key >= 0 && key < MaxKeys) Down.reset(key); return *this; }

        bool operator==(const InputFrame& other) const { return Down == other.Down && Pressed == other.Pressed && Scroll == other.Scroll; }
        bool operator!=(const InputFrame& other) const { return !(*this == other); }
    };

}
