#include "mrlpch.h"
#include "SceneTool.h"
#include "SceneLoader.h"
#include "SceneSchema.h"
#include "ComponentReference.h"
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
            "  validate [--json] <file|dir>...    check scene files and report every problem\n"
            "  format [--check] [--drop-comments] [--schema <path>] <file|dir>...\n"
            "                                     rewrite scene files in canonical form; a file with comments is\n"
            "                                     left alone unless --drop-comments says to lose the comments\n"
            "  schema [--output <file> | --check <file>]\n"
            "                                     print the JSON Schema for scene files, write it, or check a file\n"
            "  simulate <file> [--steps N] [--recording <file>] [--seed N] [--expect <checksum>] [--json]\n"
            "                                     run a scene headless for N fixed steps (default: the recording's length,\n"
            "                                     else 600), print what changed, which triggers touched what (step\n"
            "                                     numbers count from 0) and the final checksum; --expect makes it a\n"
            "                                     check (exit 1 on a different checksum)\n"
            "  describe [--json] [<component>]    show what a component is and each of its fields (type, default, range,\n"
            "                                     required, flags); without a name, list the components\n"
            "  reference [--check <file> | --update <file>]\n"
            "                                     print the Markdown table of every component and field, or replace\n"
            "                                     the table between the markers in a document, or check that it is\n"
            "                                     up to date (exit 1 if not)\n"
            "  help                               show this text\n"
            "\n"
            "--json (validate, simulate, describe) prints one JSON object instead of text; see docs/scene-format.md. A bad\n"
            "command line is still reported as text on stderr.\n"
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

        // Removes `flag` from args and says whether it was there
        bool TakeFlag(std::vector<std::string>& args, const std::string& flag)
        {
            auto it = std::remove(args.begin(), args.end(), flag);
            bool found = it != args.end();
            args.erase(it, args.end());
            return found;
        }

        // The version of the --json output; bump it when a field is renamed or removed
        constexpr int JsonVersion = 1;

        void PrintJson(std::ostream& out, const Json& json) { out << json.dump(2) << "\n"; }

        int Validate(std::vector<std::string> paths, const ComponentRegistry& registry, std::ostream& out, std::ostream& err)
        {
            const bool asJson = TakeFlag(paths, "--json");
            if (paths.empty()) { err << "marle-scene validate: no files given\n\n" << Usage; return 2; }

            std::vector<std::string> files = ExpandPaths(paths);
            if (files.empty() && !asJson) { out << "no scene files found\n"; return 0; }

            Json fileList = Json::array();
            size_t failing = 0, problemCount = 0;
            for (const std::string& file : files) {
                SceneLoadResult result = LoadScene(file, registry);
                problemCount += result.Problems.size();
                if (!result.Ok()) failing++;

                if (asJson) {
                    Json problems = Json::array();
                    for (const SceneProblem& problem : result.Problems) problems.push_back({ { "pointer", problem.Path }, { "message", problem.Message } });
                    Json entry = { { "file", file }, { "ok", result.Ok() } };
                    if (result.Scene) { entry["entities"] = result.Scene->GetEntityCount(); entry["components"] = CountComponents(*result.Scene, registry); }
                    entry["problems"] = std::move(problems);
                    fileList.push_back(std::move(entry));
                    continue;
                }

                if (result.Ok()) {
                    out << "OK    " << file << ": " << Plural(result.Scene->GetEntityCount(), "entity") << ", " << Plural(CountComponents(*result.Scene, registry), "component") << "\n";
                    continue;
                }

                for (const SceneProblem& problem : result.Problems) out << problem.ToString() << "\n";
                out << "FAIL  " << file << ": " << Plural(result.Problems.size(), "problem");
                if (result.Scene) out << " (loaded " << Plural(result.Scene->GetEntityCount(), "entity") << ", " << Plural(CountComponents(*result.Scene, registry), "component") << ")";
                else out << " (nothing could be loaded)";
                out << "\n";
            }

            if (asJson) {
                PrintJson(out, { { "version", JsonVersion }, { "ok", failing == 0 }, { "files", std::move(fileList) },
                                 { "summary", { { "files", files.size() }, { "failing", failing }, { "problems", problemCount } } } });
                return failing ? 1 : 0;
            }

            if (files.size() > 1) out << Plural(files.size(), "file") << ", " << failing << " with problems\n";
            return failing ? 1 : 0;
        }

        int DescribeCommand(std::vector<std::string> args, const ComponentRegistry& registry, std::ostream& out, std::ostream& err)
        {
            const bool asJson = TakeFlag(args, "--json");
            for (const std::string& arg : args)
                if (arg.rfind("--", 0) == 0) { err << "marle-scene describe: unknown option '" << arg << "'\n\n" << Usage; return 2; }
            if (args.size() > 1) { err << "marle-scene describe: one component at a time\n\n" << Usage; return 2; }

            const std::vector<const ComponentInfo*> all = registry.GetComponents();
            if (args.empty()) {
                if (asJson) {
                    Json list = Json::array();
                    for (const ComponentInfo* info : all) list.push_back({ { "name", info->Name }, { "description", info->Description }, { "behavior", info->IsBehavior } });
                    PrintJson(out, { { "version", JsonVersion }, { "ok", true }, { "components", std::move(list) } });
                    return 0;
                }
                for (const ComponentInfo* info : all) out << info->Name << (info->IsBehavior ? " (behavior)" : "") << ": " << info->Description << "\n";
                return 0;
            }

            for (const ComponentInfo* info : all) {
                if (info->Name != args[0]) continue;
                if (asJson) { Json json = DescribeComponent(*info); Json result = { { "version", JsonVersion }, { "ok", true } }; result.update(json); PrintJson(out, result); }
                else out << DescribeComponentText(*info);
                return 0;
            }

            std::vector<std::string> names;
            for (const ComponentInfo* info : all) names.push_back(info->Name);
            const std::string suggestion = detail::SuggestionFor(args[0], names);
            const std::string message = "unknown component '" + args[0] + "'" + (suggestion.empty() ? "" : " (" + suggestion + ")") + "; known components: " + detail::Join(names);
            if (asJson) PrintJson(out, { { "version", JsonVersion }, { "ok", false }, { "error", message } });
            else err << "marle-scene describe: " << message << "\n";
            return 1;
        }

        int Reference(const std::vector<std::string>& args, const ComponentRegistry& registry, std::ostream& out, std::ostream& err)
        {
            const std::string table = ComponentTableMarkdown(registry);
            if (args.empty()) { out << table; return 0; }
            if (args.size() != 2 || (args[0] != "--check" && args[0] != "--update")) { err << "marle-scene reference: expected --check <file> or --update <file>\n\n" << Usage; return 2; }

            bool readable = false;
            const std::string text = ReadWholeFile(args[1], readable);
            if (!readable) { out << "FAIL  " << args[1] << ": cannot open the file\n"; return 1; }
            std::string updated = text;
            if (!ReplaceComponentTable(updated, table)) { out << "FAIL  " << args[1] << ": it needs the lines " << ComponentTableBegin << " and " << ComponentTableEnd << ", once each, in that order\n"; return 1; }

            if (args[0] == "--check") {
                if (updated == text) { out << "OK    " << args[1] << ": the component table is up to date\n"; return 0; }
                out << "DIFF  " << args[1] << ": the component table is out of date; run marle-scene reference --update " << args[1] << "\n";
                return 1;
            }
            if (updated == text) { out << "OK    " << args[1] << ": already up to date\n"; return 0; }
            std::string problem;
            if (!WriteTextFile(args[1], updated, &problem)) { out << "FAIL  " << args[1] << ": " << problem << "\n"; return 1; }
            out << "WROTE " << args[1] << "\n";
            return 0;
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

        // One field that differs, or an entity or component that was added or removed
        struct Change {
            enum class Kind { EntityAdded, EntityRemoved, ComponentAdded, ComponentRemoved, FieldChanged } What;
            std::string Entity, Component, Field;
            Json Before, After;   // FieldChanged only; After is null when the field is gone
            bool FieldGone = false;

            std::string ToString() const
            {
                switch (What) {
                    case Kind::EntityAdded:      return "added entity " + Entity;
                    case Kind::EntityRemoved:    return "removed entity " + Entity;
                    case Kind::ComponentAdded:   return Entity + "." + Component + ": added";
                    case Kind::ComponentRemoved: return Entity + "." + Component + ": removed";
                    case Kind::FieldChanged:     break;
                }
                return Entity + "." + Component + "." + Field + ": " + Before.dump() + " -> " + (FieldGone ? "(gone)" : After.dump());
            }

            Json ToJson() const
            {
                static const char* kinds[] = { "entity_added", "entity_removed", "component_added", "component_removed", "field_changed" };
                Json json = { { "kind", kinds[(int)What] }, { "entity", Entity } };
                if (What != Kind::EntityAdded && What != Kind::EntityRemoved) json["component"] = Component;
                if (What == Kind::FieldChanged) { json["field"] = Field; json["before"] = Before; json["after"] = FieldGone ? Json(nullptr) : After; json["field_gone"] = FieldGone; }
                return json;
            }
        };

        std::vector<Change> Describe(const Snapshot& before, const Snapshot& after)
        {
            std::vector<Change> changes;
            auto find = [](const auto& list, const std::string& name) -> const typename std::decay_t<decltype(list)>::value_type* {
                for (const auto& item : list) if (item.first == name) return &item;
                return nullptr;
            };

            for (const auto& entity : before) {
                const auto* now = find(after, entity.first);
                if (!now) { changes.push_back({ Change::Kind::EntityRemoved, entity.first }); continue; }
                for (const auto& component : entity.second) {
                    const auto* then = find(now->second, component.first);
                    if (!then) { changes.push_back({ Change::Kind::ComponentRemoved, entity.first, component.first }); continue; }
                    if (component.second == then->second) continue;
                    for (auto it = component.second.begin(); it != component.second.end(); ++it)
                        if (!then->second.contains(it.key()) || then->second[it.key()] != it.value()) {
                            const bool gone = !then->second.contains(it.key());
                            changes.push_back({ Change::Kind::FieldChanged, entity.first, component.first, it.key(), it.value(), gone ? Json(nullptr) : then->second[it.key()], gone });
                        }
                }
                for (const auto& component : now->second)
                    if (!find(entity.second, component.first)) changes.push_back({ Change::Kind::ComponentAdded, entity.first, component.first });
            }
            for (const auto& entity : after)
                if (!find(before, entity.first)) changes.push_back({ Change::Kind::EntityAdded, entity.first });
            return changes;
        }

        bool ParseCount(const std::string& text, long long& value)
        {
            if (text.empty() || text.size() > 12) return false;
            size_t used = 0;
            try { value = std::stoll(text, &used); } catch (...) { return false; }
            return used == text.size();
        }

        int Simulate(std::vector<std::string> args, const ComponentRegistry& registry, std::ostream& out, std::ostream& err)
        {
            const bool asJson = TakeFlag(args, "--json");
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
                if (asJson) {
                    Json problems = Json::array();
                    for (const SceneProblem& problem : loaded.Problems) problems.push_back({ { "pointer", problem.Path }, { "message", problem.Message } });
                    PrintJson(out, { { "version", JsonVersion }, { "ok", false }, { "file", file }, { "error", "the scene has problems; fix them before simulating" }, { "scene_problems", std::move(problems) } });
                    return 1;
                }
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

            // A run that cannot start for a reason other than the scene's problems
            auto fail = [&](const std::string& what, const std::vector<std::string>& details) {
                if (asJson) PrintJson(out, { { "version", JsonVersion }, { "ok", false }, { "file", file }, { "error", what }, { "recording_problems", details } });
                else {
                    for (const std::string& detail : details) out << recordingFile << ": " << detail << "\n";
                    out << "FAIL  " << recordingFile << ": " << what << "\n";
                }
                return 1;
            };

            InputRecording recording;
            const bool hasRecording = !recordingFile.empty();
            if (hasRecording) {
                bool readable = false;
                std::string text = ReadWholeFile(recordingFile, readable);
                if (!readable) return fail("cannot open the file", {});
                RecordingReadResult read = ReadRecording(text);
                if (!read.Ok()) return fail(Plural(read.Problems.size(), "problem"), read.Problems);
                recording = std::move(read.Recording);

                // With --seed the point is to try the same input on another seed, so the recording's own is not compared
                std::string mismatch = RecordingMismatch(recording, scene.GetName(), loaded.TextHash, hasSeed ? recording.Seed : scene.GetSeed(), recording.StepsPerSecond);
                if (!mismatch.empty()) return fail(mismatch, {});
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

            const std::vector<Change> changes = Describe(before, after);
            const bool checksComparison = hasRecording && recording.HasChecksum && steps == recorded && !hasSeed;
            const std::string checksumText = Simulation::ChecksumText(checksum);

            int code = 0;
            if (checksComparison && recording.Checksum != checksum) code = 1;
            if (!expect.empty() && expect != checksumText) code = 1;

            if (asJson) {
                Json changeList = Json::array();
                for (const Change& change : changes) changeList.push_back(change.ToJson());
                Json overlapList = Json::array();
                for (const Touch& t : touches)
                    overlapList.push_back({ { "trigger", t.Pair.Trigger }, { "other", t.Pair.Other }, { "first_step", t.First }, { "last_step", t.Last }, { "steps_touching", t.Steps }, { "times_entered", t.Entered } });

                Json result = { { "version", JsonVersion }, { "ok", code == 0 }, { "file", file }, { "steps_run", steps }, { "seed", scene.GetSeed() },
                                { "input", hasRecording ? Json({ { "recording", recordingFile }, { "steps", recorded } }) : Json(nullptr) },
                                { "checksum", checksumText }, { "changes", std::move(changeList) }, { "overlaps", std::move(overlapList) } };
                if (checksComparison) result["recording_checksum"] = Simulation::ChecksumText(recording.Checksum), result["matches_recording"] = recording.Checksum == checksum;
                if (!expect.empty()) result["expected_checksum"] = expect, result["matches_expected"] = expect == checksumText;
                PrintJson(out, result);
                return code;
            }

            out << "SIM   " << file << ": " << Plural((size_t)steps, "step") << ", seed " << scene.GetSeed();
            if (hasRecording) out << ", input from " << recordingFile << " (" << Plural((size_t)recorded, "step") << (steps > recorded ? "; no input after that" : steps < recorded ? "; stopped early" : "") << ")";
            else out << ", no input";
            out << "\n";

            constexpr size_t MaxLines = 40;
            if (changes.empty()) out << "nothing changed\n";
            for (size_t i = 0; i < changes.size() && i < MaxLines; i++) out << "  " << changes[i].ToString() << "\n";
            if (changes.size() > MaxLines) out << "  ... and " << (changes.size() - MaxLines) << " more\n";
            if (!touches.empty()) {
                out << "overlaps (trigger > what it touched):\n";
                for (size_t i = 0; i < touches.size() && i < MaxLines; i++) {
                    const Touch& t = touches[i];
                    out << "  " << t.Pair.Trigger << " > " << t.Pair.Other << ": steps " << t.First << " to " << t.Last << ", touching for " << Plural((size_t)t.Steps, "step") << ", entered " << (t.Entered == 1 ? std::string("once") : std::to_string(t.Entered) + " times") << "\n";
                }
                if (touches.size() > MaxLines) out << "  ... and " << (touches.size() - MaxLines) << " more\n";
            }
            out << "checksum " << checksumText << "\n";

            if (checksComparison) {
                if (recording.Checksum == checksum) out << "OK    matches the checksum in the recording\n";
                else out << "DIFF  the recording says " << Simulation::ChecksumText(recording.Checksum) << ": this run is not the one it was made from\n";
            }
            if (!expect.empty()) {
                if (expect == checksumText) out << "OK    matches --expect\n";
                else out << "DIFF  expected " << expect << "\n";
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
        if (command == "describe") return DescribeCommand(rest, registry, out, err);
        if (command == "reference") return Reference(rest, registry, out, err);

        err << "marle-scene: unknown command '" << command << "'\n\n" << Usage;
        return 2;
    }

}
