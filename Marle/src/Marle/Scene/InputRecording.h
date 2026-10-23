#pragma once

#include "Marle/Core.h"
#include "Marle/InputFrame.h"
#include "ComponentRegistry.h"
#include "Scene.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Marle {

    // The input of a run, one InputFrame per fixed step, for playing it back (ADR-0002). Replaying a
    // recording on the scene it was made for (same text, so same seed) gives the same run, which the
    // checksum at its end can prove. A recording starts from the scene as it is loaded from its file.
    struct InputRecording {
        std::string SceneName;
        uint64_t SceneHash = 0;         // of the scene file's text (SceneLoadResult::TextHash)
        int Seed = 1;                   // the scene's seed when it was recorded (Scene::GetSeed)
        int StepsPerSecond = 60;        // the fixed step is 1 / StepsPerSecond seconds
        bool HasChecksum = false;
        uint64_t Checksum = 0;          // of the scene after the last step (Simulation::Checksum)
        std::vector<InputFrame> Frames; // one per step

        unsigned GetStepCount() const { return (unsigned)Frames.size(); }
    };

    // The most steps a recording may have (about four and a half hours at 60 a second)
    constexpr unsigned MaxRecordingSteps = 1000000;

    struct RecordingWriteResult {
        std::string Text;
        std::vector<std::string> Problems;   // "where: what", empty if the recording could be written
        bool Ok() const { return Problems.empty(); }
    };

    // The recording as text: JSON, one line per step where the input changed, so it diffs and can be read.
    //
    //   { "version": 1, "scene": "...", "sceneHash": "16 hex digits", "seed": 1, "stepsPerSecond": 60,
    //     "steps": 600, "checksum": "16 hex digits",
    //     "changes": [ { "step": 0, "down": ["W"], "pressed": ["W"] }, { "step": 12, "up": ["W"] }, ... ] }
    //
    // A step is listed only if something changed. `down` and `up` are the keys that went down or up since
    // the step before (key names as in scene files, see Core/KeyNames.h); a key stays down until it is
    // listed in `up`. `pressed` is the keys newly pressed in that step (a tap that was released before
    // the step is `pressed` but never `down`), and `scroll` the wheel movement. Steps not listed repeat
    // the keys that are down and have nothing pressed and no scroll. This loses nothing an InputFrame holds.
    EXPORT RecordingWriteResult WriteRecording(const InputRecording& recording);

    struct RecordingReadResult {
        InputRecording Recording;
        std::vector<std::string> Problems;   // "path: message" with a JSON Pointer path; empty if it is valid
        bool Ok() const { return Problems.empty(); }
    };

    // Reads and checks recording text (comments allowed). Every problem is reported: unknown keys, wrong
    // types, key names that are not keys (with a suggestion), steps out of order or past the end, a key that
    // goes down while it is down or up while it is not. A recording with problems must not be replayed.
    EXPORT RecordingReadResult ReadRecording(const std::string& text);

    // Why a recording cannot be replayed on this scene, or an empty string if it can. A recording made on a
    // different version of the scene (a different hash of its text) is refused, since the same input would
    // not give the same run.
    EXPORT std::string RecordingMismatch(const InputRecording& recording, const std::string& sceneName, uint64_t sceneHash, int seed, int stepsPerSecond);

    // Collects the frames of a run as they are used, one per fixed step.
    class EXPORT InputRecorder
    {
        public:
            // Starts a recording of the scene as it has just been loaded from its file
            void Start(std::string sceneName, uint64_t sceneHash, int seed, int stepsPerSecond);
            bool IsRecording() const { return m_Recording; }

            // One call per fixed step, with the frame that step used
            void Record(const InputFrame& frame);
            unsigned GetStepCount() const { return m_Result.GetStepCount(); }
            uint64_t GetSceneHash() const { return m_Result.SceneHash; }

            // Stops and returns the recording with the checksum of the scene after the last step
            InputRecording Finish(uint64_t checksum);

            // Stops and drops what was recorded
            void Cancel();

        private:
            bool m_Recording = false;
            InputRecording m_Result;
    };

    // Gives a recording's frames back, one per fixed step.
    class EXPORT InputReplay
    {
        public:
            explicit InputReplay(InputRecording recording) : m_Recording(std::move(recording)) {}

            bool IsFinished() const { return m_Step >= m_Recording.GetStepCount(); }
            unsigned GetStep() const { return m_Step; }     // frames given so far
            const InputRecording& GetRecording() const { return m_Recording; }

            // The next step's frame, or an empty one once it is finished
            InputFrame Next() { return IsFinished() ? InputFrame() : m_Recording.Frames[m_Step++]; }

        private:
            InputRecording m_Recording;
            unsigned m_Step = 0;
    };

    // Plays every frame of a recording into a scene, one Simulation::Step each, and returns the scene's
    // checksum afterwards. Headless. It does not reload the scene for a restart request (the caller can).
    EXPORT uint64_t ReplayRecording(const ComponentRegistry& registry, Scene& scene, const InputRecording& recording);

}
