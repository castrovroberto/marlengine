#include "mrlpch.h"
#include "ComponentReference.h"

namespace Marle {

    Json DescribeComponent(const ComponentInfo& component)
    {
        Json fields = Json::array();
        for (const FieldInfo& field : component.Fields) {
            Json json = { { "name", field.Name }, { "description", field.Description }, { "type", FieldTypeName(field.Type) }, { "default", field.Default },
                          { "required", field.Required }, { "asset_path", field.AssetPath }, { "entity_reference", field.EntityReference } };
            if (field.Min) json["min"] = *field.Min;
            if (field.Max) json["max"] = *field.Max;
            if (!field.OneOf.empty()) json["one_of"] = field.OneOf;
            fields.push_back(std::move(json));
        }
        return { { "name", component.Name }, { "description", component.Description }, { "behavior", component.IsBehavior }, { "fields", std::move(fields) } };
    }

    std::string DescribeComponentText(const ComponentInfo& component)
    {
        const Json json = DescribeComponent(component);
        std::string text = component.Name + (component.IsBehavior ? " (behavior)" : "") + "\n  " + component.Description + "\n";
        if (component.Fields.empty()) return text + "\nno fields\n";

        text += "\nfields:\n";
        for (const Json& field : json["fields"]) {
            text += "  " + field["name"].get<std::string>() + ": " + field["type"].get<std::string>() + ", default " + field["default"].dump();
            if (field["required"]) text += ", required";
            if (field.contains("min") || field.contains("max")) {
                text += ", range ";
                text += field.contains("min") ? field["min"].dump() : std::string("any");
                text += " to ";
                text += field.contains("max") ? field["max"].dump() : std::string("any");
            }
            if (field["asset_path"]) text += ", asset path";
            if (field["entity_reference"]) text += ", entity reference";
            text += "\n";
            if (field.contains("one_of")) {
                std::vector<std::string> values = field["one_of"].get<std::vector<std::string>>();
                text += values.size() <= 12 ? "      one of: " + detail::Join(values) + "\n" : "      one of " + std::to_string(values.size()) + " values (see the schema's enum)\n";
            }
            text += "      " + field["description"].get<std::string>() + "\n";
        }
        return text;
    }

}
