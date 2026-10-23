#pragma once

#include "Marle/Core.h"
#include "ComponentRegistry.h"
#include "Scene.h"
#include <string>
#include <vector>

namespace Marle {

    struct SceneWriteOptions {
        // Written as the "$schema" key when not empty (scene files do not keep it when loaded)
        std::string Schema;
    };

    struct SceneSaveResult {
        // The scene file text in canonical form (docs/scene-format.md); only meaningful when Ok()
        std::string Text;

        // Reasons the scene cannot be written so that it reads back: an entity name scene files cannot
        // hold, a number that is not finite, a value outside its field's range, an unsafe asset path,
        // text that is not valid UTF-8.
        std::vector<std::string> Problems;

        bool Ok() const { return Problems.empty(); }
    };

    // Writes text to a file, replacing any existing one, through a temporary file beside it that is
    // renamed into place, so a failure never leaves a half-written file. On failure returns false
    // and, if `error` is given, says why; nothing is logged.
    EXPORT bool WriteTextFile(const std::string& file, const std::string& text, std::string* error = nullptr);

    // The scene as a scene file: entities in creation order, components in registration order, fields
    // in declaration order, every field written, one line per component. Loading the text gives the same
    // scene back, and writing that scene gives the same text. Components the registry does not know
    // are not written; comments in the file the scene came from are not kept. Needs no GL context.
    EXPORT SceneSaveResult SaveSceneToText(const Scene& scene, const ComponentRegistry& registry, const SceneWriteOptions& options = SceneWriteOptions());

    // Writes the scene file, replacing any existing one. If there are problems, or the file cannot be
    // written, the reasons are logged, false is returned and the existing file is left as it was.
    EXPORT bool SaveScene(const Scene& scene, const ComponentRegistry& registry, const std::string& file, const SceneWriteOptions& options = SceneWriteOptions());

}
