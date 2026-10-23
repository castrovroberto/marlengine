#pragma once

#include "Core.h"
#include <optional>
#include <string>
#include <vector>

namespace Marle {

    // The arguments the program was started with, after its own name. EntryPoint.h's main() stores them
    // before CreateApplication() runs, so a client can read its options in its constructor.
    EXPORT void SetCommandLine(int argc, char** argv);
    EXPORT const std::vector<std::string>& GetCommandLine();

    // True if the argument `name` (such as "--replay") is present
    EXPORT bool HasCommandLineFlag(const std::string& name);

    // The value of `name`, given as "--replay file" or "--replay=file"; nothing if it is absent or has no value
    EXPORT std::optional<std::string> GetCommandLineValue(const std::string& name);

}
