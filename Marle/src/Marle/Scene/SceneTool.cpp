#include "mrlpch.h"
#include "SceneTool.h"
#include "SceneLoader.h"
#include "SceneSchema.h"
#include "SceneWriter.h"
#include "Simulation.h"
#include "InputRecording.h"
#include "Components.h"
#include <map>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace Marle {

    namespace {

        const char* Usage =
            "usage: marle-scene <command> [options]\n"
            "\n"
            "commands:\n"
            "  validate <file|dir>...             check scene files and report every problem\n"
            "  format [--check] [--drop-comments] [--schema <path>] <file|dir>...\n"
            "                                     rewrite scene files in canonical form; a file with comments is\n"
            "                                     left alone unless --drop-comments says to lose the comments\n"
            "  schema [--output <file> | --check <file>]\n"
            "                                     print the JSON Schema for scene files, write it, or check a file\n"
            "  simulate <file> [--steps N] [--recording <file>] [--seed N] [--expect <checksum>]\n"
            "                                     run a scene headless for N fixed steps (default: the recording's length,\n"
            "                                     else 600), print what changed, which triggers touched what (step\n"
            "                                     numbers count from 0) and the final checksum; --expect makes it a\n"
            "                                     check (exit 1 on a different checksum)\n"
            "  help                               show this text\n"
            "\n"
            "A directory means every .json file in it, except *.schema.json.\n"
            "Exit status: 0 all fine, 1 problems found (or files differ with --check), 2 bad command line.\n";

        std::string ReadWholeFile(const std::string& path, bool& ok)
        {
            std::ifstream in(path, std::ios::binary);
            ok = (bool)in;
            std::stringstream text;
            text << in.rdbuf();
            return text.str();
        }

        // The files named on the command line, with directories replaced by the scene files in them
        std::vector<std::string> ExpandPaths(const std::vector<std::string>& paths)
        {
            std::vector<std::string> files;
            for (const std::string& path : paths) {
                std::error_code ec;
                if (!std::filesystem::is_directory(path, ec)) { files.push_back(path); continue; }

                std::vector<std::string> inDirectory;
                for (auto it = std::filesystem::recursive_directory_iterator(path, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                    std::string name = it->path().generic_string();
                    bool isSchema = name.size() >= 12 && name.compare(name.size() - 12, 12, ".schema.json") == 0;
                    if (it->is_regular_file() && it->path().extension() == ".json" && !isSchema) inDirectory.push_back(name);
                }
                std::sort(inDirectory.begin(), inDirectory.end());
                files.insert(files.end(), inDirectory.begin(), inDirectory.end());
            }
            return files;
        }

        std::string Plural(size_t n, const std::string& noun)
        {
            if (n == 1) return "1 " + noun;
            std::string plural = noun.back() == 'y' ? noun.substr(0, noun.size() - 1) + "ies" : noun + "s";
            return std::to_string(n) + " " + plural;
        }

        size_t CountComponents(const Scene& scene, const ComponentRegistry& registry)
        {
            size_t count = registry.GetComponentNames(scene.GetSceneEntity()).size();   // the scene's own
            for (const Entity& entity : scene.GetEntities()) count += registry.GetComponentNames(entity).size();
            return count;
        }

        int Validate(const std::vector<std::string>& paths, const ComponentRegistry& registry, std::ostream& out, std::ostream& err)
        {
            if (paths.empty()) { err << "marle-scene validate: no files given\n\n" << Usage; return 2; }

            std::vector<std::string> files = ExpandPaths(paths);
            if (files.empty()) { out << "no scene files found\n"; return 0; }

            size_t failing = 0;
            for (const std::string& file : files) {
                SceneLoadResult result = LoadScene(file, registry);
                if (result.Ok()) {
                    out << "OK    " << file << ": " << Plural(result.Scene->GetEntityCount(), "entity") << ", " << Plural(CountComponents(*result.Scene, registry), "component") << "\n";
                    continue;
                }

                failing++;
                for (const SceneProblem& problem : result.Problems) out << problem.ToString() << "\n";
                out << "FAIL  " << file << ": " << Plural(result.Problems.size(), "problem");
                if (result.Scene) out << " (loaded " << Plural(result.Scene->GetEntityCount(), "entity") << ", " << Plural(CountComponents(*result.Scene, registry), "component") << ")";
                else out << " (nothing could be loaded)";
                out << "\n";
            }

            if (files.size() > 1) out << Plural(files.size(), "file") << ", " << failing << " with problems\n";
            return failing ? 1 : 0;
        }

        int Format(std::vector<std::string> args, const ComponentRegistry& registry, std::ostream& out, std::ostream& err)
        {
            bool check = false, dropComments = false;
            std::string schema;
            std::vector<std::string> paths;
            for (size_t i = 0; i < args.size(); i++) {
                if (args[i] == "--check") check = true;
                else if (args[i] == "--drop-comments") dropComments = true;
                else if (args[i] == "--schema" && i + 1 < args.size()) schema = args[++i];
                else if (args[i].rfind("--", 0) == 0) { err << "marle-scene format: unknown or incomplete option '" << args[i] << "'\n\n" << Usage; return 2; }
                else paths.push_back(args[i]);
            }
            if (paths.empty()) { err << "marle-scene format: no files given\n\n" << Usage; return 2; }

            std::vector<std::string> files = ExpandPaths(paths);
            if (files.empty()) { out << "no scene files found\n"; return 0; }

            size_t failed = 0, changed = 0;
            for (const std::string& file : files) {
                bool readable = false;
                std::string text = ReadWholeFile(file, readable);
                SceneLoadResult result = LoadScene(file, registry);
                if (!result.Ok()) {
                    // Writing the scene would drop whatever failed to load, so leave the file alone
                    for (const SceneProblem& problem : result.Problems) out << problem.ToString() << "\n";
                    out << "SKIP  " << file << ": " << Plural(result.Problems.size(), "problem") << "; fix them first, formatting would lose what did not load\n";
                    failed++;
                    continue;
                }

                // Comments are lost by writing; leave a file that has them unless told to drop them
                // (the loader accepts comments, a strict parse does not)
                if (!dropComments && Json::parse(text, nullptr, false, false).is_discarded()) {
                    out << "KEEP  " << file << ": has comments, which formatting would drop; left alone (--drop-comments formats it anyway)\n";
                    continue;
                }

                // Keep a $schema key the file has (loading does not keep it)
                SceneWriteOptions options;
                options.Schema = schema;
                Json raw = Json::parse(text, nullptr, false, true);
                if (schema.empty() && raw.is_object() && raw.contains("$schema") && raw["$schema"].is_string()) options.Schema = raw["$schema"].get<std::string>();

                SceneSaveResult saved = SaveSceneToText(*result.Scene, registry, options);
                if (!saved.Ok()) {
                    for (const std::string& problem : saved.Problems) out << file << ": " << problem << "\n";
                    out << "SKIP  " << file << ": cannot be written as a scene file\n";
                    failed++;
                    continue;
                }

                if (saved.Text == text) { out << "OK    " << file << ": already canonical\n"; continue; }

                changed++;
                if (check) { out << "DIFF  " << file << ": would be reformatted\n"; continue; }

                std::string error;
                if (!WriteTextFile(file, saved.Text, &error)) { out << "FAIL  " << file << ": " << error << "\n"; failed++; continue; }
                out << "DONE  " << file << ": reformatted\n";
            }

            if (files.size() > 1) out << Plural(files.size(), "file") << ", " << changed << (check ? " would change" : " changed") << ", " << failed << " skipped\n";
            return failed || (check && changed) ? 1 : 0;
        }

        int Schema(const std::vector<std::string>& args, const ComponentRegistry& registry, std::ostream& out, std::ostream& err)
        {
            const std::string text = SceneSchemaToText(registry);
            if (args.empty()) { out << text; return 0; }
            if (args.size() != 2 || (args[0] != "--output" && args[0] != "--check")) {
                err << "marle-scene schema: expected nothing, '--output <file>' or '--check <file>'\n\n" << Usage;
                return 2;
            }

            const std::string& file = args[1];
            if (args[0] == "--output") {
                std::string error;
                if (!WriteTextFile(file, text, &error)) { out << "FAIL  " << file << ": " << error << "\n"; return 1; }
                out << "DONE  " << file << ": schema written\n";
                return 0;
            }

            bool readable = false;
            std::string current = ReadWholeFile(file, readable);
            if (!readable) { out << "FAIL  " << file << ": cannot open the file\n"; return 1; }
            if (current != text) {
                out << "DIFF  " << file << ": out of date; regenerate it with: marle-scene schema --output " << file << "\n";
                return 1;
            }
            out << "OK    " << file << ": up to date\n";
            return 0;
        }

    }

    namespace {

        // The state of a scene as the registry writes it: entity name to component name to fields, in order
        using Snapshot = std::vector<std::pair<std::string, std::vector<std::pair<std::string, Json>>>>;

        Snapshot TakeSnapshot(const Scene& scene, const ComponentRegistry& registry)
        {
            Snapshot snapshot;
            auto add = [&](const Entity& entity) {
                std::vector<std::pair<std::string, Json>> components;
                for (const std::string& name : registry.GetComponentNames(entity)) components.emplace_back(name, registry.Write(entity, name));
                snapshot.emplace_back(entity.GetName(), std::move(components));
            };
            add(scene.GetSceneEntity());
            for (const Entity& entity : scene.GetEntities()) add(entity);
            return snapshot;
        }

        // One line per field that differs, plus added and removed entities and components
        std::vector<std::string> Describe(const Snapshot& before, const Snapshot& after)
        {
            std::vector<std::string> lines;
            auto find = [](const auto& list, const std::string& name) -> const typename std::decay_t<decltype(list)>::value_type* {
                for (const auto& item : list) if (item.first == name) return &item;
                return nullptr;
            };

            for (const auto& entity : before) {
                const auto* now = find(after, entity.first);
                if (!now) { lines.push_back("removed entity " + entity.first); continue; }
                for (const auto& component : entity.second) {
                    const auto* then = find(now->second, component.first);
                    if (!then) { lines.push_back(entity.first + "." + component.first + ": removed"); continue; }
                    if (component.second == then->second) continue;
                    for (auto it = component.second.begin(); it != component.second.end(); ++it)
                        if (!then->second.contains(it.key()) || then->second[it.key()] != it.value())
                            lines.push_back(entity.first + "." + component.first + "." + it.key() + ": " + it.value().dump() + " -> " + (then->second.contains(it.key()) ? then->second[it.key()].dump() : "(gone)"));
                }
                for (const auto& component : now->second)
                    if (!find(entity.second, component.first)) lines.push_back(entity.first + "." + component.first + ": added");
            }
            for (const auto& entity : after)
                if (!find(before, entity.first)) lines.push_back("added entity " + entity.first);
            return lines;
        }

        bool ParseCount(const std::string& text, long long& value)
        {
            if (text.empty() || text.size() > 12) return false;
            size_t used = 0;
            try { value = std::stoll(text, &used); } catch (...) { return false; }
            return used == text.size();
        }

        int Simulate(const std::vector<std::string>& args, const ComponentRegistry& registry, std::ostream& out, std::ostream& err)
        {
            std::string file, recordingFile, expect;
            long long steps = -1, seed = 0;
            bool hasSeed = false;
            for (size_t i = 0; i < args.size(); i++) {
                const bool hasValue = i + 1 < args.size();
                if (args[i] == "--steps" && hasValue) {
                    if (!ParseCount(args[++i], steps) || steps < 0 || steps > MaxRecordingSteps) { err << "marle-scene simulate: --steps needs a whole number from 0 to " << MaxRecordingSteps << "\n"; return 2; }
                }
                else if (args[i] == "--seed" && hasValue) {
                    if (!ParseCount(args[++i], seed) || seed < INT32_MIN || seed > INT32_MAX) { err << "marle-scene simulate: --seed needs a whole number that fits in 32 bits\n"; return 2; }
                    hasSeed = true;
                }
                else if (args[i] == "--recording" && hasValue) recordingFile = args[++i];
                else if (args[i] == "--expect" && hasValue) expect = args[++i];
                else if (args[i].rfind("--", 0) == 0) { err << "marle-scene simulate: unknown or incomplete option '" << args[i] << "'\n\n" << Usage; return 2; }
                else if (file.empty()) file = args[i];
                else { err << "marle-scene simulate: one scene file at a time\n\n" << Usage; return 2; }
            }
            if (file.empty()) { err << "marle-scene simulate: no scene file given\n\n" << Usage; return 2; }
            if (!expect.empty() && (expect.size() != 16 || expect.find_first_not_of("0123456789abcdef") != std::string::npos)) {
                err << "marle-scene simulate: --expect needs a checksum as 16 lower-case hex digits\n";
                return 2;
            }

            // The scene has to be clean first: a run of a scene with problems would not be the scene in the file
            SceneLoadResult loaded = LoadScene(file, registry);
            if (!loaded.Ok()) {
                for (const SceneProblem& problem : loaded.Problems) out << problem.ToString() << "\n";
                out << "FAIL  " << file << ": " << Plural(loaded.Problems.size(), "problem") << "; fix them before simulating\n";
                return 1;
            }
            Scene& scene = *loaded.Scene;

            // A seed given here replaces the scene's (and starts its random sequence from it)
            if (hasSeed) {
                Entity self = scene.GetSceneEntity();
                if (Random* random = self.TryGet<Random>()) random->Seed = (int)seed;
                else self.Add<Random>().Seed = (int)seed;
                scene.ResetRandom();
            }

            InputRecording recording;
            const bool hasRecording = !recordingFile.empty();
            if (hasRecording) {
                bool readable = false;
                std::string text = ReadWholeFile(recordingFile, readable);
                if (!readable) { out << "FAIL  " << recordingFile << ": cannot open the file\n"; return 1; }
                RecordingReadResult read = ReadRecording(text);
                if (!read.Ok()) {
                    for (const std::string& problem : read.Problems) out << recordingFile << ": " << problem << "\n";
                    out << "FAIL  " << recordingFile << ": " << Plural(read.Problems.size(), "problem") << "\n";
                    return 1;
                }
                recording = std::move(read.Recording);

                // With --seed the point is to try the same input on another seed, so the recording's own is not compared
                std::string mismatch = RecordingMismatch(recording, scene.GetName(), loaded.TextHash, hasSeed ? recording.Seed : scene.GetSeed(), recording.StepsPerSecond);
                if (!mismatch.empty()) { out << "FAIL  " << recordingFile << ": " << mismatch << "\n"; return 1; }
            }

            const long long recorded = hasRecording ? (long long)recording.GetStepCount() : 0;
            if (steps < 0) steps = hasRecording ? recorded : 600;
            const double dt = 1.0 / (hasRecording ? (double)recording.StepsPerSecond : 60.0);

            // Which triggers touched what, in the order the pairs first touched; steps count from 0, like a recording's
            struct Touch { Overlap Pair; long long First = 0, Last = 0, Steps = 0, Entered = 0; };
            std::vector<Touch> touches;

            const Snapshot before = TakeSnapshot(scene, registry);
            for (long long step = 0; step < steps; step++) {
                static const InputFrame none;
                Simulation::Step(registry, scene, step < recorded ? recording.Frames[(size_t)step] : none, dt);
                scene.ClearRestartRequest();

                const SceneOverlaps& overlaps = scene.GetOverlaps();
                for (const Overlap& pair : overlaps.Touching) {
                    auto it = std::find_if(touches.begin(), touches.end(), [&](const Touch& t) { return t.Pair == pair; });
                    if (it == touches.end()) { touches.push_back({ pair, step, step, 0, 0 }); it = touches.end() - 1; }
                    it->Last = step;
                    it->Steps++;
                }
                for (const Overlap& pair : overlaps.Entered)
                    std::find_if(touches.begin(), touches.end(), [&](const Touch& t) { return t.Pair == pair; })->Entered++;
            }
            const Snapshot after = TakeSnapshot(scene, registry);
            const uint64_t checksum = Simulation::Checksum(registry, scene);

            out << "SIM   " << file << ": " << Plural((size_t)steps, "step") << ", seed " << scene.GetSeed();
            if (hasRecording) out << ", input from " << recordingFile << " (" << Plural((size_t)recorded, "step") << (steps > recorded ? "; no input after that" : steps < recorded ? "; stopped early" : "") << ")";
            else out << ", no input";
            out << "\n";

            const std::vector<std::string> changes = Describe(before, after);
            constexpr size_t MaxLines = 40;
            if (changes.empty()) out << "nothing changed\n";
            for (size_t i = 0; i < changes.size() && i < MaxLines; i++) out << "  " << changes[i] << "\n";
            if (changes.size() > MaxLines) out << "  ... and " << (changes.size() - MaxLines) << " more\n";
            if (!touches.empty()) {
                out << "overlaps (trigger > what it touched):\n";
                for (size_t i = 0; i < touches.size() && i < MaxLines; i++) {
                    const Touch& t = touches[i];
                    out << "  " << t.Pair.Trigger << " > " << t.Pair.Other << ": steps " << t.First << " to " << t.Last << ", touching for " << Plural((size_t)t.Steps, "step") << ", entered " << (t.Entered == 1 ? std::string("once") : std::to_string(t.Entered) + " times") << "\n";
                }
                if (touches.size() > MaxLines) out << "  ... and " << (touches.size() - MaxLines) << " more\n";
            }
            out << "checksum " << Simulation::ChecksumText(checksum) << "\n";

            int code = 0;
            if (hasRecording && recording.HasChecksum && steps == recorded && !hasSeed) {
                if (recording.Checksum == checksum) out << "OK    matches the checksum in the recording\n";
                else { out << "DIFF  the recording says " << Simulation::ChecksumText(recording.Checksum) << ": this run is not the one it was made from\n"; code = 1; }
            }
            if (!expect.empty()) {
                if (expect == Simulation::ChecksumText(checksum)) out << "OK    matches --expect\n";
                else { out << "DIFF  expected " << expect << "\n"; code = 1; }
            }
            return code;
        }

    }

    int RunSceneTool(const std::vector<std::string>& args, const ComponentRegistry& registry, std::ostream& out, std::ostream& err)
    {
        if (args.empty()) { err << Usage; return 2; }

        const std::string& command = args[0];
        const std::vector<std::string> rest(args.begin() + 1, args.end());
        if (command == "help" || command == "--help" || command == "-h") { out << Usage; return 0; }
        if (command == "validate") return Validate(rest, registry, out, err);
        if (command == "format") return Format(rest, registry, out, err);
        if (command == "schema") return Schema(rest, registry, out, err);
        if (command == "simulate") return Simulate(rest, registry, out, err);

        err << "marle-scene: unknown command '" << command << "'\n\n" << Usage;
        return 2;
    }

}
