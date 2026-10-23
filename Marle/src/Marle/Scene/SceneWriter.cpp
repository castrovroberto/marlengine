#include "mrlpch.h"
#include "Marle/Log.h"
#include "SceneWriter.h"
#include "SceneLoader.h"
#include <cmath>
#include <filesystem>
#include <fstream>

namespace Marle {

    namespace {

        struct Writer {
            std::vector<std::string>& Problems;

            void Report(const std::string& where, const std::string& what) { Problems.push_back(where + ": " + what); }

            // A string as a JSON literal; text that is not valid UTF-8 cannot be written
            std::string Quoted(const std::string& text, const std::string& where)
            {
                try {
                    return Json(text).dump();
                } catch (const Json::exception&) {
                    Report(where, "the text is not valid UTF-8");
                    return "\"\"";
                }
            }

            // A value on one line: { "key": value, "key": value }, [a, b]. Whole numbers have no ".0".
            std::string Inline(const Json& j, const std::string& where)
            {
                switch (j.type()) {
                    case Json::value_t::object: {
                        if (j.empty()) return "{}";
                        std::string out = "{ ";
                        bool first = true;
                        for (auto it = j.begin(); it != j.end(); ++it) {
                            if (!first) out += ", ";
                            first = false;
                            out += Quoted(it.key(), where) + ": " + Inline(it.value(), where + "/" + it.key());
                        }
                        return out + " }";
                    }
                    case Json::value_t::array: {
                        std::string out = "[";
                        for (size_t i = 0; i < j.size(); i++) {
                            if (i) out += ", ";
                            out += Inline(j[i], where + "/" + std::to_string(i));
                        }
                        return out + "]";
                    }
                    case Json::value_t::string:
                        return Quoted(j.get<std::string>(), where);
                    case Json::value_t::number_float: {
                        if (!std::isfinite(j.get<double>())) {
                            Report(where, "the number is not finite");
                            return "0";
                        }
                        std::string text = j.dump();
                        if (text.size() > 2 && text.compare(text.size() - 2, 2, ".0") == 0) text.resize(text.size() - 2);
                        return text;
                    }
                    default:
                        return j.dump();
                }
            }
        };

    }

    SceneSaveResult SaveSceneToText(const Scene& scene, const ComponentRegistry& registry, const SceneWriteOptions& options)
    {
        SceneSaveResult result;
        Writer writer{ result.Problems };

        if (scene.GetName().empty()) writer.Report("scene", "the scene has no name");

        std::string text = "{\n";
        if (!options.Schema.empty()) text += "  \"$schema\": " + writer.Quoted(options.Schema, "$schema") + ",\n";
        text += "  \"version\": " + std::to_string(SceneFormatVersion) + ",\n";
        text += "  \"name\": " + writer.Quoted(scene.GetName(), "scene name") + ",\n";
        text += "  \"entities\": ";

        std::vector<Entity> entities = scene.GetEntities();
        if (entities.empty()) {
            text += "[]\n}\n";
            result.Text = text;
            return result;
        }

        text += "[\n";
        for (size_t i = 0; i < entities.size(); i++) {
            Entity entity = entities[i];
            const std::string name = entity.GetName();
            const std::string where = "entity '" + name + "'";
            if (!IsValidEntityName(name)) writer.Report(where, "scene files cannot hold this name (1 to 64 characters, no control characters, no leading or trailing whitespace, no '/')");

            text += "    {\n      \"name\": " + writer.Quoted(name, where);

            std::vector<std::string> lines;
            for (const std::string& component : registry.GetComponentNames(entity)) {
                const std::string at = where + " component '" + component + "'";
                Json data = registry.Write(entity, component);

                // What is written must read back: check the same way the loader would
                Scene scratch;
                for (const ValidationError& error : registry.Read(scratch.CreateEntity("check"), component, data))
                    writer.Report(at + error.Path, error.Message);
                for (const FieldInfo& field : registry.Find(component)->Fields) {
                    if (!field.AssetPath || !data[field.Name].is_string()) continue;
                    std::string why = AssetPathProblem(data[field.Name].get<std::string>());
                    if (!why.empty()) writer.Report(at + "/" + field.Name, why);
                }

                lines.push_back("        " + writer.Quoted(component, at) + ": " + writer.Inline(data, at));
            }

            if (!lines.empty()) {
                text += ",\n      \"components\": {\n";
                for (size_t c = 0; c < lines.size(); c++)
                    text += lines[c] + (c + 1 < lines.size() ? ",\n" : "\n");
                text += "      }";
            }
            text += i + 1 < entities.size() ? "\n    },\n" : "\n    }\n";
        }
        text += "  ]\n}\n";

        result.Text = text;
        return result;
    }

    bool WriteTextFile(const std::string& file, const std::string& text, std::string* error)
    {
        auto fail = [&](const std::string& why) { if (error) *error = why; return false; };

        const std::string temporary = file + ".tmp";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out.write(text.data(), (std::streamsize)text.size());
            out.close();
            if (!out) {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                return fail("cannot write " + temporary);
            }
        }

        std::error_code ec;
        std::filesystem::rename(temporary, file, ec);
        if (ec) {
            std::filesystem::remove(temporary, ec);
            return fail("cannot replace " + file + " (" + ec.message() + ")");
        }
        return true;
    }

    bool SaveScene(const Scene& scene, const ComponentRegistry& registry, const std::string& file, const SceneWriteOptions& options)
    {
        SceneSaveResult result = SaveSceneToText(scene, registry, options);
        if (!result.Ok()) {
            for (const std::string& problem : result.Problems)
                MRL_CORE_ERROR("Cannot save scene '{}' to {}: {}", scene.GetName(), file, problem);
            return false;
        }

        std::string error;
        if (!WriteTextFile(file, result.Text, &error)) {
            MRL_CORE_ERROR("Cannot save scene '{}': {}", scene.GetName(), error);
            return false;
        }
        return true;
    }

}
