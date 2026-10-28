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

    namespace {

        // A table cell: no line breaks, and a pipe would end the cell
        std::string Cell(std::string text)
        {
            std::string out;
            for (char c : text) {
                if (c == '|') out += "\\|";
                else if (c == '\n') out += ' ';
                else out += c;
            }
            return out;
        }

        std::string Number(const Json& value) { return value.dump(); }

        std::string Meaning(const Json& field)
        {
            std::string text = field["description"].get<std::string>();
            if (field["required"]) text += " Required.";
            if (field.contains("min") || field.contains("max")) {
                text += " Range ";
                text += field.contains("min") ? Number(field["min"]) : std::string("any");
                text += " to ";
                text += field.contains("max") ? Number(field["max"]) : std::string("any");
                text += ".";
            }
            if (field.contains("one_of")) {
                std::vector<std::string> values = field["one_of"].get<std::vector<std::string>>();
                if (values.size() <= 12) text += " One of: " + detail::Join(values) + ".";
                else text += " One of " + std::to_string(values.size()) + " values (see the schema).";
            }
            if (field["asset_path"]) text += " An asset file path.";
            if (field["entity_reference"]) text += " The name of an entity.";
            return text;
        }

    }

    std::string ComponentTableMarkdown(const ComponentRegistry& registry)
    {
        std::string table = "| Component | Field | Type | Default | Meaning |\n|---|---|---|---|---|\n";
        for (const ComponentInfo* info : registry.GetComponents()) {
            const Json json = DescribeComponent(*info);
            table += "| `" + info->Name + "` | | | | " + Cell(info->Description) + " |\n";
            for (const Json& field : json["fields"])
                table += "| | `" + field["name"].get<std::string>() + "` | " + field["type"].get<std::string>() + " | `" + Cell(field["default"].dump()) + "` | " + Cell(Meaning(field)) + " |\n";
        }
        return table;
    }

    bool ReplaceComponentTable(std::string& document, const std::string& table)
    {
        const std::string begin = ComponentTableBegin, end = ComponentTableEnd;
        const size_t b = document.find(begin);
        if (b == std::string::npos || document.find(begin, b + 1) != std::string::npos) return false;
        const size_t e = document.find(end, b);
        if (e == std::string::npos || document.find(end, e + 1) != std::string::npos) return false;
        document.replace(b + begin.size(), e - (b + begin.size()), "\n" + table);
        return true;
    }

}
