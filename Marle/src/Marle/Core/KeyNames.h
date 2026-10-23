#pragma once

#include "Marle/Core.h"
#include <string>
#include <vector>

namespace Marle {

    // The names scene files use for keys: "W", "Up", "LeftShift", "Space", "F5", "NumPadEnter" and so on.
    // Letters are upper case, digits are "0" to "9", and the rest are spelled out as in Marle::Key
    // (the right bracket is "RightBracket"). Names are exact: case matters.

    // The name of a Marle::Key code, or nullptr if it has none
    EXPORT const char* KeyName(int keyCode);

    // The Marle::Key code with this name, or Key::Unknown (-1) if there is none
    EXPORT int KeyFromName(const std::string& name);

    // Every key name, in a stable order (letters, digits, then the others); for limiting a scene file
    // field to key names with ComponentBuilder::OneOf
    EXPORT const std::vector<std::string>& KeyNames();

}
