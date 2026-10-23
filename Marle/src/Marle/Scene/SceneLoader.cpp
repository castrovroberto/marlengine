#include "mrlpch.h"
#include "Marle/Log.h"
#include "SceneLoader.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace Marle {

    std::string SceneProblem::ToString() const
    {
        return File + (Path.empty() ? "" : " " + Path) + ": " + Message;
    }

    bool IsValidEntityName(const std::string& name)
    {
        // length counts characters (UTF-8 code points), not bytes
        size_t length = std::count_if(name.begin(), name.end(), [](unsigned char c) { return (c & 0xC0) != 0x80; });
        if (length == 0 || length > 64 || name.find('/') != std::string::npos) return false;
        if (std::isspace((unsigned char)name.front()) || std::isspace((unsigned char)name.back())) return false;
        return std::none_of(name.begin(), name.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
    }

    std::string AssetPathProblem(const std::string& path)
    {
        if (path.empty()) return "";
        if (path.find('\\') != std::string::npos) return "use '/' as the path separator";
        if (path[0] == '/' || (path.size() > 1 && path[1] == ':')) return "the path must be relative to the working directory";

        std::stringstream segments(path);
        for (std::string segment; std::getline(segments, segment, '/');)
            if (segment == "..") return "the path must not contain '..'";
        return "";
    }

    static std::string DescribeParseError(const std::exception& e)
    {
        std::string message = e.what();
        size_t bracket = message.find("] ");
        return bracket == std::string::npos ? message : message.substr(bracket + 2);
    }

    SceneLoadResult LoadScene(const std::string& file, const ComponentRegistry& registry)
    {
        std::ifstream stream(file, std::ios::binary);
        if (!stream) {
            SceneLoadResult result;
            result.Problems.push_back({ file, "", "cannot open the file" });
            return result;
        }
        std::stringstream text;
        text << stream.rdbuf();
        return LoadSceneFromText(text.str(), file, registry);
    }

    SceneLoadResult LoadSceneFromText(const std::string& text, const std::string& source, const ComponentRegistry& registry)
    {
        SceneLoadResult result;
        auto problem = [&](const std::string& path, const std::string& message) { result.Problems.push_back({ source, path, message }); };

        // Parse, noting repeated keys: a plain parse keeps the last one silently, which hides mistakes.
        struct Frame { bool IsArray = false; size_t Index = 0; std::string Key; std::set<std::string> Keys; };
        std::vector<Frame> frames;
        std::set<size_t> repeatedInEntity;     // entities with a repeated key, to be skipped
        bool repeatedElsewhere = false;

        auto pointer = [&] {
            std::string path;
            for (const Frame& f : frames) path += "/" + (f.IsArray ? std::to_string(f.Index) : f.Key);
            return path;
        };

        Json root;
        try {
            root = Json::parse(text, [&](int, Json::parse_event_t event, Json& parsed) {
                switch (event) {
                    case Json::parse_event_t::object_start: frames.push_back(Frame()); break;
                    case Json::parse_event_t::array_start: frames.push_back(Frame{ true }); break;
                    case Json::parse_event_t::key: {
                        Frame& f = frames.back();
                        f.Key = parsed.get<std::string>();
                        if (!f.Keys.insert(f.Key).second) {
                            std::string path = pointer();
                            problem(path, "the key '" + f.Key + "' appears more than once; keys must be unique");
                            if (path.rfind("/entities/", 0) == 0) repeatedInEntity.insert(std::strtoull(path.c_str() + 10, nullptr, 10));
                            else repeatedElsewhere = true;
                        }
                        break;
                    }
                    case Json::parse_event_t::value:
                        if (!frames.empty() && frames.back().IsArray) frames.back().Index++;
                        break;
                    case Json::parse_event_t::object_end:
                    case Json::parse_event_t::array_end:
                        frames.pop_back();
                        if (!frames.empty() && frames.back().IsArray) frames.back().Index++;
                        break;
                }
                return true;
            }, true, true);
        } catch (const Json::exception& e) {
            problem("", DescribeParseError(e));
            return result;
        }
        if (repeatedElsewhere) return result;

        // Top level
        if (!root.is_object()) {
            problem("", std::string("the top level must be an object, got ") + detail::JsonTypeName(root));
            return result;
        }

        const std::vector<std::string> topKeys = { "version", "name", "entities", "$schema" };
        for (auto it = root.begin(); it != root.end(); ++it) {
            if (std::find(topKeys.begin(), topKeys.end(), it.key()) != topKeys.end()) continue;
            std::string hint = detail::SuggestionFor(it.key(), topKeys);
            problem("/" + it.key(), "unknown key '" + it.key() + "'" + (hint.empty() ? "" : " (" + hint + ")") + "; valid keys: " + detail::Join(topKeys));
        }
        if (root.contains("$schema") && !root["$schema"].is_string())
            problem("/$schema", std::string("expected a string, got ") + detail::JsonTypeName(root["$schema"]));

        if (!root.contains("version")) { problem("", "missing required key 'version'"); return result; }
        if (!root["version"].is_number_integer()) { problem("/version", std::string("expected an integer, got ") + detail::JsonTypeName(root["version"])); return result; }
        long long version = root["version"].get<long long>();
        if (version != SceneFormatVersion) {
            problem("/version", version > SceneFormatVersion
                ? "this file is version " + std::to_string(version) + " but this engine reads version " + std::to_string(SceneFormatVersion) + "; it needs a newer engine"
                : "version " + std::to_string(version) + " is not valid; this engine reads version " + std::to_string(SceneFormatVersion));
            return result;
        }

        std::string name = "Untitled";
        if (!root.contains("name")) problem("", "missing required key 'name'");
        else if (!root["name"].is_string() || root["name"].get<std::string>().empty()) problem("/name", "expected a non-empty string");
        else name = root["name"].get<std::string>();

        result.Scene = std::make_unique<Scene>(name);
        Scene& scene = *result.Scene;

        if (!root.contains("entities")) { problem("", "missing required key 'entities'"); return result; }
        if (!root["entities"].is_array()) { problem("/entities", std::string("expected an array, got ") + detail::JsonTypeName(root["entities"])); return result; }

        // Entities
        const std::vector<std::string> entityKeys = { "name", "components" };
        size_t index = 0;
        for (const Json& entry : root["entities"]) {
            const std::string at = "/entities/" + std::to_string(index);
            const bool skip = repeatedInEntity.count(index) > 0;
            index++;
            if (skip) continue;   // its repeated key is already reported

            if (!entry.is_object()) { problem(at, std::string("an entity must be an object, got ") + detail::JsonTypeName(entry)); continue; }

            for (auto it = entry.begin(); it != entry.end(); ++it) {
                if (std::find(entityKeys.begin(), entityKeys.end(), it.key()) != entityKeys.end()) continue;
                std::string hint = detail::SuggestionFor(it.key(), entityKeys);
                problem(at + "/" + it.key(), "unknown key '" + it.key() + "'" + (hint.empty() ? "" : " (" + hint + ")") + "; valid keys: " + detail::Join(entityKeys));
            }

            if (!entry.contains("name")) { problem(at, "missing required key 'name'; the entity is skipped"); continue; }
            if (!entry["name"].is_string()) { problem(at + "/name", std::string("expected a string, got ") + detail::JsonTypeName(entry["name"]) + "; the entity is skipped"); continue; }
            const std::string entityName = entry["name"].get<std::string>();
            if (!IsValidEntityName(entityName)) {
                problem(at + "/name", "'" + entityName + "' is not a valid entity name (1 to 64 characters, no control characters, no leading or trailing whitespace, no '/'); the entity is skipped");
                continue;
            }
            if (scene.FindEntity(entityName)) { problem(at + "/name", "an entity named '" + entityName + "' already exists; the entity is skipped"); continue; }

            Entity entity = scene.CreateEntity(entityName);
            if (!entry.contains("components")) continue;
            const Json& components = entry["components"];
            if (!components.is_object()) { problem(at + "/components", std::string("expected an object, got ") + detail::JsonTypeName(components)); continue; }

            for (auto it = components.begin(); it != components.end(); ++it) {
                const std::string componentAt = at + "/components/" + it.key();
                const ComponentInfo* info = registry.Find(it.key());

                // An asset path that leaves the working directory is not loaded at all
                bool unsafe = false;
                if (info && it.value().is_object())
                    for (const FieldInfo& field : info->Fields) {
                        auto value = it.value().find(field.Name);
                        if (!field.AssetPath || value == it.value().end() || !value->is_string() || value->get<std::string>().empty()) continue;
                        std::string why = AssetPathProblem(value->get<std::string>());
                        if (!why.empty()) { problem(componentAt + "/" + field.Name, why + "; the component is skipped"); unsafe = true; }
                    }
                if (unsafe) continue;

                std::vector<ValidationError> errors = registry.Read(entity, it.key(), it.value());
                for (size_t i = 0; i < errors.size(); i++)
                    problem(componentAt + errors[i].Path, errors[i].Message + (i == 0 ? "; the component is skipped" : ""));
                if (!errors.empty()) continue;

                // A missing asset is reported, but the component loads so the entity still shows up
                // (a missing image is drawn as the error checker)
                for (const FieldInfo& field : info->Fields) {
                    auto value = it.value().find(field.Name);
                    if (!field.AssetPath || value == it.value().end() || value->get<std::string>().empty()) continue;
                    std::error_code ec;
                    if (!std::filesystem::is_regular_file(value->get<std::string>(), ec))
                        problem(componentAt + "/" + field.Name, "the file '" + value->get<std::string>() + "' does not exist");
                }
            }
        }

        return result;
    }

}
