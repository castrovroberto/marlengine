#pragma once

#include "Marle/Core.h"
#include "ComponentRegistry.h"
#include "Scene.h"
#include "SceneLoader.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Marle {

    // What the reloader knows about its file, for showing to the person editing it
    struct SceneReloadStatus {
        std::string File;
        bool Missing = false;                   // the file was not there at the last check (kept scene unchanged)
        bool LastOk = false;                    // the last load had no problems at all
        unsigned Loads = 0;                     // scenes handed out: the first load and each applied reload
        unsigned Failures = 0;                  // reloads refused because the file had problems
        bool KeptPrevious = false;              // the last reload was refused, so the scene from before is still the one running
        std::vector<SceneProblem> Problems;     // from the last load, empty if it was fine
    };

    struct SceneReload {
        bool Attempted = false;                 // the file had changed (or a reload was forced) and was loaded
        std::unique_ptr<Marle::Scene> Scene;    // the new scene: only when it loaded with no problems at all
    };

    // Watches a scene file and reloads it when it changes, without a window or GL context.
    // Reloading is strict: a scene is only handed out when the file loads with no problems, so a
    // half-finished edit never replaces the scene that is running. The caller keeps its current scene
    // when Poll() gives none, and the problems stay in GetStatus() until the next load.
    class EXPORT SceneReloader
    {
        public:
            SceneReloader(std::string file, const ComponentRegistry& registry);

            // The first load. Unlike a reload this returns whatever loaded, problems and all (with
            // nothing running yet, a partial scene is better than none); they are in GetStatus() too.
            SceneLoadResult Load();

            // Checks the file's modification time and size and loads it again if they changed since the
            // last load (or if `force`). A missing file is not a change: the current scene stays and
            // the status says so. Cheap enough to call every few frames.
            SceneReload Poll(bool force = false);

            const SceneReloadStatus& GetStatus() const { return m_Status; }

        private:
            struct Stamp {
                bool Exists = false;
                std::filesystem::file_time_type Modified;
                std::uintmax_t Size = 0;
                bool operator==(const Stamp& other) const { return Exists == other.Exists && Modified == other.Modified && Size == other.Size; }
            };
            Stamp ReadStamp() const;

            const ComponentRegistry& m_Registry;
            Stamp m_Stamp;
            SceneReloadStatus m_Status;
    };

}
