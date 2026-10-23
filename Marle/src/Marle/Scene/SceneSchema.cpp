#include "mrlpch.h"
#include "SceneSchema.h"
#include "SceneLoader.h"

namespace Marle {

    namespace {

        // The same rules as IsValidEntityName(): no '/', no control characters, no leading or
        // trailing whitespace (control characters and space at either end); length is checked apart.
        const char* EntityNamePattern = R"(^[^\u0000-\x20/\u007f](?:[^\u0000-\u001f/\u007f]*[^\u0000-\x20/\u007f])?$)";

        // The same rules as AssetPathProblem(): relative, no backslash, no '..' segment
        const char* AssetPathPattern = R"(^(?!/)(?!.:)(?![\s\S]*\\)(?!(?:[\s\S]*/)?\.\.(?:/|$))[\s\S]*$)";

        Json Numbers(const char* type, int count, const FieldInfo& field)
        {
            Json element = Json::object();
            element["type"] = type;
            if (field.Min) element["minimum"] = *field.Min;
            if (field.Max) element["maximum"] = *field.Max;

            Json array = Json::object();
            array["type"] = "array";
            array["items"] = element;
            array["minItems"] = count;
            array["maxItems"] = count;
            return array;
        }

        Json FieldSchema(const FieldInfo& field)
        {
            Json schema;
            switch (field.Type) {
                case FieldType::Bool:
                    schema = Json::object({ { "type", "boolean" } });
                    break;
                case FieldType::Int:
                case FieldType::Float:
                    schema = Json::object({ { "type", field.Type == FieldType::Int ? "integer" : "number" } });
                    if (field.Min) schema["minimum"] = *field.Min;
                    if (field.Max) schema["maximum"] = *field.Max;
                    break;
                case FieldType::String:
                    schema = Json::object({ { "type", "string" } });
                    if (!field.OneOf.empty()) schema["enum"] = field.OneOf;
                    if (field.AssetPath) schema["pattern"] = AssetPathPattern;
                    break;
                case FieldType::Vec2:  schema = Numbers("number", 2, field); break;
                case FieldType::Vec4:  schema = Numbers("number", 4, field); break;
                case FieldType::IVec2: schema = Numbers("integer", 2, field); break;
                case FieldType::StringArray:
                    schema = Json::object({ { "type", "array" }, { "items", Json::object({ { "type", "string" } }) } });
                    break;
            }
            std::string allowed;
            for (size_t i = 0; i < field.OneOf.size(); i++) allowed += (i ? ", " : "") + field.OneOf[i];
            schema["description"] = field.Description
                + (field.OneOf.empty() ? "" : field.OneOf.size() <= 12 ? " One of: " + allowed + "." : " One of " + std::to_string(field.OneOf.size()) + " values, listed in enum.")
                + (field.AssetPath ? " An asset file path, relative to the working directory, written with '/' and without '..'." : "")
                + (field.EntityReference ? " The name of another entity in the scene; empty means none." : "");
            schema["default"] = field.Default;
            return schema;
        }

        std::string DefinitionName(const std::string& component) { return "component_" + component; }

    }

    Json GenerateSceneSchema(const ComponentRegistry& registry)
    {
        Json components = Json::object();
        Json definitions = Json::object();

        Json entityComponents = Json::object({ { "type", "object" }, { "description", "The entity's components, keyed by component name." }, { "additionalProperties", false } });
        Json entityComponentProperties = Json::object();

        for (const ComponentInfo* info : registry.GetComponents()) {
            Json properties = Json::object();
            Json required = Json::array();
            for (const FieldInfo& field : info->Fields) {
                properties[field.Name] = FieldSchema(field);
                if (field.Required) required.push_back(field.Name);
            }

            Json definition = Json::object({ { "type", "object" }, { "description", info->Description }, { "additionalProperties", false }, { "properties", properties } });
            if (!required.empty()) definition["required"] = required;
            definitions[DefinitionName(info->Name)] = definition;
            entityComponentProperties[info->Name] = Json::object({ { "$ref", "#/definitions/" + DefinitionName(info->Name) } });
        }
        entityComponents["properties"] = entityComponentProperties;

        Json entity = Json::object({
            { "type", "object" },
            { "description", "A named thing in the scene, made of components." },
            { "additionalProperties", false },
            { "required", Json::array({ "name" }) },
            { "properties", Json::object({
                { "name", Json::object({
                    { "type", "string" },
                    { "minLength", 1 },
                    { "maxLength", 64 },
                    { "pattern", EntityNamePattern },
                    { "description", "Unique within the scene. 1 to 64 characters, no control characters, no leading or trailing whitespace, no '/'. Other entities refer to this one by this name." } }) },
                { "components", entityComponents } }) }
        });

        Json schema = Json::object();
        schema["$schema"] = "http://json-schema.org/draft-07/schema#";
        schema["title"] = "Marlengine scene file";
        schema["description"] = "A scene: its entities and their components. Generated from the component registry; see docs/scene-format.md. The engine's loader is the authority.";
        schema["type"] = "object";
        schema["additionalProperties"] = false;
        schema["required"] = Json::array({ "version", "name", "entities" });
        schema["properties"] = Json::object({
            { "$schema", Json::object({ { "type", "string" }, { "description", "The path or URL of this schema, for editors; the engine ignores it." } }) },
            { "version", Json::object({ { "const", SceneFormatVersion }, { "description", "The scene file format version." } }) },
            { "name", Json::object({ { "type", "string" }, { "minLength", 1 }, { "description", "The scene's name." } }) },
            { "components", Json::object({
                { "type", "object" },
                { "description", "Components of the scene itself (such as its random seed), keyed by component name. Same rules as an entity's components." },
                { "additionalProperties", false },
                { "properties", entityComponentProperties } }) },
            { "entities", Json::object({ { "type", "array" }, { "description", "The entities, in creation order." }, { "items", Json::object({ { "$ref", "#/definitions/entity" } }) } }) }
        });

        Json all = Json::object({ { "entity", entity } });
        for (auto it = definitions.begin(); it != definitions.end(); ++it) all[it.key()] = it.value();
        schema["definitions"] = all;
        return schema;
    }

    std::string SceneSchemaToText(const ComponentRegistry& registry)
    {
        return GenerateSceneSchema(registry).dump(2) + "\n";
    }

}
