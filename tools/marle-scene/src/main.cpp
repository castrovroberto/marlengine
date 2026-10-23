// marle-scene: check and tidy scene files. See Marle/Scene/SceneTool.h for the commands.
// A game with components of its own builds its own tool the same way, with its own registry.
#include "Marle/Scene/SceneTool.h"
#include <iostream>

int main(int argc, char** argv)
{
    Marle::ComponentRegistry registry;
    Marle::RegisterBuiltinComponents(registry);
    return Marle::RunSceneTool(std::vector<std::string>(argv + 1, argv + argc), registry, std::cout, std::cerr);
}
