#include "mrlpch.h"
#include "CommandLine.h"

namespace Marle {

    static std::vector<std::string>& Arguments()
    {
        static std::vector<std::string> arguments;
        return arguments;
    }

    void SetCommandLine(int argc, char** argv)
    {
        Arguments().assign(argv + (argc > 0 ? 1 : 0), argv + argc);
    }

    const std::vector<std::string>& GetCommandLine()
    {
        return Arguments();
    }

    bool HasCommandLineFlag(const std::string& name)
    {
        for (const std::string& argument : Arguments())
            if (argument == name || argument.rfind(name + "=", 0) == 0) return true;
        return false;
    }

    std::optional<std::string> GetCommandLineValue(const std::string& name)
    {
        const std::vector<std::string>& arguments = Arguments();
        for (size_t i = 0; i < arguments.size(); i++) {
            if (arguments[i] == name)
                return i + 1 < arguments.size() ? std::optional<std::string>(arguments[i + 1]) : std::nullopt;
            if (arguments[i].rfind(name + "=", 0) == 0)
                return arguments[i].substr(name.size() + 1);
        }
        return std::nullopt;
    }

}
