#pragma once

#include "Marle/Core.h"
#include "ComponentRegistry.h"
#include "Scene.h"
#include <memory>
#include <string>
#include <vector>

namespace Marle {

    // The scene file version this engine reads (see docs/scene-format.md).
    constexpr int SceneFormatVersion = 1;

    // One thing wrong with a scene file. `Path` is a JSON Pointer to where, empty for the whole file.
    struct SceneProblem {
        std::string File;
        std::string Path;
        std::string Message;

        std::string ToString() const;   // "file /entities/3/components/Transform/position/1: message"
    };

    struct SceneLoadResult {
        // The scene with everything that loaded. Null only when the file as a whole cannot be used:
        // it cannot be read or parsed, its top level or version is wrong, or a key is repeated outside
        // an entity.
        std::unique_ptr<Marle::Scene> Scene;

        // Everything that was wrong: repeated keys first (they are found while parsing), then the rest in
        // file order, then references to entities that do not exist (they need every entity loaded).
        // Empty means the file is fully valid.
        std::vector<SceneProblem> Problems;

        // FNV-1a hash of the text that was loaded (Simulation.h), to tell one version of a scene file from
        // another, as an input recording does
        uint64_t TextHash = 0;

        bool Ok() const { return Scene && Problems.empty(); }
    };

    // The naming rule for entities in scene files: 1 to 64 characters, no control characters, no
    // leading or trailing whitespace and no '/'.
    EXPORT bool IsValidEntityName(const std::string& name);

    // Why an asset path is not allowed in a scene file, or empty if it is: paths are relative to the
    // working directory and written with '/', so a scene file cannot point at arbitrary files.
    EXPORT std::string AssetPathProblem(const std::string& path);

    // Loads a scene file. One mistake never costs the whole scene: each entity and component is
    // checked on its own, the offending one is skipped (a missing asset file is the exception: the
    // component loads and the file is reported), and every problem is returned. Needs no GL context.
    EXPORT SceneLoadResult LoadScene(const std::string& file, const ComponentRegistry& registry);

    // The same for text already in memory; `source` names it in the problems.
    EXPORT SceneLoadResult LoadSceneFromText(const std::string& text, const std::string& source, const ComponentRegistry& registry);

}
