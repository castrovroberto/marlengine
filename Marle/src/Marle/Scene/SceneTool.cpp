#include "mrlpch.h"
#include "SceneTool.h"
#include "SceneLoader.h"
#include "SceneSchema.h"
#include "SceneWriter.h"
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

    int RunSceneTool(const std::vector<std::string>& args, const ComponentRegistry& registry, std::ostream& out, std::ostream& err)
    {
        if (args.empty()) { err << Usage; return 2; }

        const std::string& command = args[0];
        const std::vector<std::string> rest(args.begin() + 1, args.end());
        if (command == "help" || command == "--help" || command == "-h") { out << Usage; return 0; }
        if (command == "validate") return Validate(rest, registry, out, err);
        if (command == "format") return Format(rest, registry, out, err);
        if (command == "schema") return Schema(rest, registry, out, err);

        err << "marle-scene: unknown command '" << command << "'\n\n" << Usage;
        return 2;
    }

}
