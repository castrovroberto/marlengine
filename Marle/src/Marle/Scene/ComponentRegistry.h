#pragma once

#include "Marle/Core.h"
#include "Entity.h"
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <typeinfo>
#include <vector>

namespace Marle {

    // Scene data is JSON that keeps its keys in the order they were written.
    using Json = nlohmann::ordered_json;

    // One problem found while reading a component. `Path` points inside the component's data
    // (e.g. "/position/1", empty for the whole component).
    struct ValidationError {
        std::string Path;
        std::string Message;
    };

    enum class FieldType { Bool, Int, Float, String, Vec2, IVec2, Vec4, StringArray };

    EXPORT const char* FieldTypeName(FieldType type);   // "bool", "int", "float", "string", "vec2", "ivec2", "vec4", "string[]"

    // What the registry knows about one field, for validation, documentation and schemas.
    struct FieldInfo {
        std::string Name;
        std::string Description;
        FieldType Type = FieldType::Float;
        bool Required = false;                  // must appear in the data; otherwise the default is used
        bool AssetPath = false;                 // a string naming a file, relative to the working directory
        bool EntityReference = false;           // a string naming another entity in the scene; empty means none
        Json Default;                           // the value of a default-constructed component
        std::optional<double> Min, Max;         // applied to every number in the field
        std::vector<std::string> OneOf;         // for a string: the only values allowed, empty means any string
    };

    struct ComponentInfo {
        std::string Name;
        std::string Description;
        std::vector<FieldInfo> Fields;          // in declaration order
    };

    namespace detail {

        // "did you mean 'x'?" for a word that is close to one of the candidates, else empty
        EXPORT std::string SuggestionFor(const std::string& word, const std::vector<std::string>& candidates);
        EXPORT std::string Join(const std::vector<std::string>& words);   // "a, b, c"
        EXPORT void ReportRegistryError(const std::string& message);      // logged as a registration problem

        inline const char* JsonTypeName(const Json& j)
        {
            switch (j.type()) {
                case Json::value_t::null:            return "null";
                case Json::value_t::object:          return "an object";
                case Json::value_t::array:           return "an array";
                case Json::value_t::string:          return "a string";
                case Json::value_t::boolean:         return "a boolean";
                case Json::value_t::number_integer:
                case Json::value_t::number_unsigned:
                case Json::value_t::number_float:    return "a number";
                default:                             return "a value";
            }
        }

        // A float as the shortest decimal that reads back to the same float, so 0.1f is written
        // as 0.1 and not 0.10000000149011612.
        inline Json FloatToJson(float value)
        {
            char buffer[48];
            auto result = std::to_chars(buffer, buffer + sizeof(buffer) - 1, value);
            *result.ptr = '\0';
            return Json(std::strtod(buffer, nullptr));
        }

        inline bool CheckRange(double value, const FieldInfo& info, const std::string& path, std::vector<ValidationError>& errors)
        {
            if (info.Min && value < *info.Min) {
                errors.push_back({ path, "must be at least " + Json(*info.Min).dump() + ", got " + Json(value).dump() });
                return false;
            }
            if (info.Max && value > *info.Max) {
                errors.push_back({ path, "must be at most " + Json(*info.Max).dump() + ", got " + Json(value).dump() });
                return false;
            }
            return true;
        }

        inline bool ReadFloat(const Json& j, float& out, const FieldInfo& info, const std::string& path, std::vector<ValidationError>& errors)
        {
            if (!j.is_number()) {
                errors.push_back({ path, std::string("expected a number, got ") + JsonTypeName(j) });
                return false;
            }
            double value = j.get<double>();
            if (std::fabs(value) > std::numeric_limits<float>::max()) {
                errors.push_back({ path, "number is too large for a float" });
                return false;
            }
            if (!CheckRange(value, info, path, errors)) return false;
            out = (float)value;
            return true;
        }

        inline bool ReadInt(const Json& j, int& out, const FieldInfo& info, const std::string& path, std::vector<ValidationError>& errors)
        {
            if (!j.is_number_integer()) {
                errors.push_back({ path, std::string("expected an integer, got ") + (j.is_number() ? "a number with a fraction" : JsonTypeName(j)) });
                return false;
            }
            long long value = j.get<long long>();
            if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
                errors.push_back({ path, "integer is too large" });
                return false;
            }
            if (!CheckRange((double)value, info, path, errors)) return false;
            out = (int)value;
            return true;
        }

        // An array of exactly N numbers, every one read with `readOne`
        template<typename Element, int N, typename ReadOne>
        bool ReadArray(const Json& j, Element* out, ReadOne readOne, const char* kind, const std::string& path, std::vector<ValidationError>& errors)
        {
            if (!j.is_array() || j.size() != (size_t)N) {
                std::string got = j.is_array() ? "an array of " + std::to_string(j.size()) : JsonTypeName(j);
                errors.push_back({ path, "expected an array of " + std::to_string(N) + " " + kind + ", got " + got });
                return false;
            }
            Element values[N];
            bool ok = true;
            for (int i = 0; i < N; i++)
                ok = readOne(j[i], values[i], path + "/" + std::to_string(i)) && ok;
            if (ok)
                for (int i = 0; i < N; i++) out[i] = values[i];
            return ok;
        }

        // How each field type is read from and written to JSON. Reading reports problems and leaves
        // `out` unchanged if there are any.
        template<typename T> struct FieldTraits;

        template<> struct FieldTraits<bool> {
            static constexpr FieldType Type = FieldType::Bool;
            static Json ToJson(bool v) { return v; }
            static void Read(const Json& j, bool& out, const FieldInfo&, const std::string& path, std::vector<ValidationError>& errors)
            {
                if (!j.is_boolean()) errors.push_back({ path, std::string("expected true or false, got ") + JsonTypeName(j) });
                else out = j.get<bool>();
            }
        };

        template<> struct FieldTraits<int> {
            static constexpr FieldType Type = FieldType::Int;
            static Json ToJson(int v) { return v; }
            static void Read(const Json& j, int& out, const FieldInfo& info, const std::string& path, std::vector<ValidationError>& errors) { ReadInt(j, out, info, path, errors); }
        };

        template<> struct FieldTraits<float> {
            static constexpr FieldType Type = FieldType::Float;
            static Json ToJson(float v) { return FloatToJson(v); }
            static void Read(const Json& j, float& out, const FieldInfo& info, const std::string& path, std::vector<ValidationError>& errors) { ReadFloat(j, out, info, path, errors); }
        };

        template<> struct FieldTraits<std::string> {
            static constexpr FieldType Type = FieldType::String;
            static Json ToJson(const std::string& v) { return v; }
            static void Read(const Json& j, std::string& out, const FieldInfo& info, const std::string& path, std::vector<ValidationError>& errors)
            {
                if (!j.is_string()) {
                    errors.push_back({ path, std::string("expected a string, got ") + JsonTypeName(j) });
                    return;
                }
                const std::string& value = j.get_ref<const std::string&>();
                if (!info.OneOf.empty() && std::find(info.OneOf.begin(), info.OneOf.end(), value) == info.OneOf.end()) {
                    std::string hint = SuggestionFor(value, info.OneOf);
                    std::string allowed;
                    if (info.OneOf.size() <= 12)
                        for (size_t i = 0; i < info.OneOf.size(); i++) allowed += (i ? ", '" : "'") + info.OneOf[i] + "'";
                    else
                        allowed = "the " + std::to_string(info.OneOf.size()) + " allowed values (the schema lists them)";   // too many to print
                    errors.push_back({ path, "expected one of " + allowed + ", got '" + value + "'" + (hint.empty() ? "" : " (" + hint + ")") });
                    return;
                }
                out = value;
            }
        };

        template<> struct FieldTraits<glm::vec2> {
            static constexpr FieldType Type = FieldType::Vec2;
            static Json ToJson(const glm::vec2& v) { return Json::array({ FloatToJson(v.x), FloatToJson(v.y) }); }
            static void Read(const Json& j, glm::vec2& out, const FieldInfo& info, const std::string& path, std::vector<ValidationError>& errors)
            {
                ReadArray<float, 2>(j, &out.x, [&](const Json& e, float& v, const std::string& p) { return ReadFloat(e, v, info, p, errors); }, "numbers", path, errors);
            }
        };

        template<> struct FieldTraits<glm::vec4> {
            static constexpr FieldType Type = FieldType::Vec4;
            static Json ToJson(const glm::vec4& v) { return Json::array({ FloatToJson(v.x), FloatToJson(v.y), FloatToJson(v.z), FloatToJson(v.w) }); }
            static void Read(const Json& j, glm::vec4& out, const FieldInfo& info, const std::string& path, std::vector<ValidationError>& errors)
            {
                ReadArray<float, 4>(j, &out.x, [&](const Json& e, float& v, const std::string& p) { return ReadFloat(e, v, info, p, errors); }, "numbers", path, errors);
            }
        };

        template<> struct FieldTraits<glm::ivec2> {
            static constexpr FieldType Type = FieldType::IVec2;
            static Json ToJson(const glm::ivec2& v) { return Json::array({ v.x, v.y }); }
            static void Read(const Json& j, glm::ivec2& out, const FieldInfo& info, const std::string& path, std::vector<ValidationError>& errors)
            {
                ReadArray<int, 2>(j, &out.x, [&](const Json& e, int& v, const std::string& p) { return ReadInt(e, v, info, p, errors); }, "integers", path, errors);
            }
        };

        template<> struct FieldTraits<std::vector<std::string>> {
            static constexpr FieldType Type = FieldType::StringArray;
            static Json ToJson(const std::vector<std::string>& v)
            {
                Json array = Json::array();
                for (const std::string& s : v) array.push_back(s);
                return array;
            }
            static void Read(const Json& j, std::vector<std::string>& out, const FieldInfo&, const std::string& path, std::vector<ValidationError>& errors)
            {
                if (!j.is_array()) {
                    errors.push_back({ path, std::string("expected an array of strings, got ") + JsonTypeName(j) });
                    return;
                }
                std::vector<std::string> values;
                bool ok = true;
                for (size_t i = 0; i < j.size(); i++) {
                    if (j[i].is_string()) values.push_back(j[i].get<std::string>());
                    else { errors.push_back({ path + "/" + std::to_string(i), std::string("expected a string, got ") + JsonTypeName(j[i]) }); ok = false; }
                }
                if (ok) out = std::move(values);
            }
        };

        // Type-erased view of a registered component, held by the registry
        struct ComponentImpl {
            ComponentInfo Info;
            const std::type_info* Type = nullptr;

            virtual ~ComponentImpl() = default;
            virtual bool Has(const Entity& entity) const = 0;
            virtual void Read(Entity& entity, const Json& data, std::vector<ValidationError>& errors) const = 0;
            virtual Json Write(const Entity& entity, bool skipDefaults) const = 0;
        };

        template<typename T>
        struct TypedComponentImpl : ComponentImpl {
            struct FieldOps {
                std::function<void(const Json&, T&, const FieldInfo&, const std::string&, std::vector<ValidationError>&)> Read;
                std::function<Json(const T&)> Write;
            };
            std::vector<FieldOps> Ops;     // parallel to Info.Fields
            std::function<void(const T&, std::vector<ValidationError>&)> Validator;   // rules that span fields

            TypedComponentImpl(std::string name, std::string description)
            {
                Info.Name = std::move(name);
                Info.Description = std::move(description);
                Type = &typeid(T);
            }

            bool Has(const Entity& entity) const override { return entity.Has<T>(); }

            void Read(Entity& entity, const Json& data, std::vector<ValidationError>& errors) const override
            {
                if (!entity.IsValid()) {
                    errors.push_back({ "", "the entity is invalid" });
                    return;
                }
                if (!data.is_object()) {
                    errors.push_back({ "", std::string("expected an object, got ") + JsonTypeName(data) });
                    return;
                }

                std::vector<std::string> names;
                for (const FieldInfo& field : Info.Fields) names.push_back(field.Name);

                size_t before = errors.size();
                for (auto it = data.begin(); it != data.end(); ++it) {
                    bool known = false;
                    for (const std::string& name : names) known = known || name == it.key();
                    if (known) continue;

                    std::string hint = SuggestionFor(it.key(), names);
                    errors.push_back({ "/" + it.key(), "unknown field '" + it.key() + "'" + (hint.empty() ? "" : " (" + hint + ")") + "; valid fields: " + Join(names) });
                }

                T value;    // filled in field by field, and only added if everything was fine
                for (size_t i = 0; i < Info.Fields.size(); i++) {
                    const FieldInfo& field = Info.Fields[i];
                    auto it = data.find(field.Name);
                    if (it != data.end())
                        Ops[i].Read(*it, value, field, "/" + field.Name, errors);
                    else if (field.Required)
                        errors.push_back({ "/" + field.Name, "missing required field '" + field.Name + "'" });
                }

                if (errors.size() == before && Validator) Validator(value, errors);

                if (errors.size() > before) return;
                if (entity.Has<T>()) {
                    errors.push_back({ "", "the entity already has a " + Info.Name + " component" });
                    return;
                }
                entity.Add<T>(std::move(value));
            }

            Json Write(const Entity& entity, bool skipDefaults) const override
            {
                const T* component = entity.TryGet<T>();
                if (!component) return nullptr;

                Json out = Json::object();
                for (size_t i = 0; i < Info.Fields.size(); i++) {
                    Json value = Ops[i].Write(*component);
                    if (!(skipDefaults && value == Info.Fields[i].Default))
                        out[Info.Fields[i].Name] = std::move(value);
                }
                return out;
            }
        };

    }

    // Describes a component's fields. Returned by ComponentRegistry::Register; chain Field() calls,
    // each optionally followed by Required(), Min(), Max() or Range() for that field.
    template<typename T>
    class ComponentBuilder
    {
        public:
            // Adds a field backed by a member of T. Its default is the member's value in a
            // default-constructed T. Supported types: bool, int, float, std::string, glm::vec2,
            // glm::vec4, glm::ivec2. A repeated name is logged and ignored.
            template<typename M>
            ComponentBuilder& Field(M T::* member, const std::string& name, const std::string& description);

            ComponentBuilder& Required() { if (m_Last) m_Last->Required = true; return *this; }

            // Limits the last field, which must be a string, to these values. Reading anything else is an
            // error that lists them and suggests the closest; the schema gets an enum. The list must be
            // non-empty and without repeats, and must contain the field's default (the value in a
            // default-constructed component); otherwise the problem is logged and the field stays free.
            ComponentBuilder& OneOf(std::vector<std::string> values)
            {
                if (!m_Last) return *this;
                if (m_Last->Type != FieldType::String) {
                    detail::ReportRegistryError("field '" + m_Last->Name + "' is not a string, so it cannot be limited to a set of values");
                } else if (values.empty()) {
                    detail::ReportRegistryError("field '" + m_Last->Name + "': OneOf needs at least one value");
                } else if (std::set<std::string>(values.begin(), values.end()).size() != values.size()) {
                    detail::ReportRegistryError("field '" + m_Last->Name + "': OneOf has a value that is listed twice");
                } else if (m_Last->EntityReference) {
                    detail::ReportRegistryError("field '" + m_Last->Name + "' is an entity reference, so it cannot be limited to a set of values");
                } else if (std::find(values.begin(), values.end(), m_Last->Default.get<std::string>()) == values.end()) {
                    detail::ReportRegistryError("field '" + m_Last->Name + "': its default '" + m_Last->Default.get<std::string>() + "' is not one of the allowed values");
                } else {
                    m_Last->OneOf = std::move(values);
                }
                return *this;
            }

            // Marks the last field, which must be a string, as the name of another entity in the scene
            // (empty means none). After every entity is loaded, the scene loader reports a name that
            // matches no entity, with the path of the reference; the component still loads. A field
            // cannot also be an asset path or limited to a set of values.
            ComponentBuilder& EntityReference()
            {
                if (!m_Last) return *this;
                if (m_Last->Type != FieldType::String)
                    detail::ReportRegistryError("field '" + m_Last->Name + "' is not a string, so it cannot be an entity reference");
                else if (m_Last->AssetPath || !m_Last->OneOf.empty())
                    detail::ReportRegistryError("field '" + m_Last->Name + "' is already an asset path or limited to a set of values, so it cannot be an entity reference");
                else
                    m_Last->EntityReference = true;
                return *this;
            }

            // Marks the last field, which must be a string, as the path of an asset file. The scene
            // loader checks that it stays inside the working directory and that the file exists.
            ComponentBuilder& AssetPath()
            {
                if (m_Last && m_Last->Type != FieldType::String)
                    detail::ReportRegistryError("field '" + m_Last->Name + "' is not a string, so it cannot be an asset path");
                else if (m_Last && m_Last->EntityReference)
                    detail::ReportRegistryError("field '" + m_Last->Name + "' is an entity reference, so it cannot also be an asset path");
                else if (m_Last)
                    m_Last->AssetPath = true;
                return *this;
            }
            ComponentBuilder& Min(double value) { if (m_Last) m_Last->Min = value; return *this; }
            ComponentBuilder& Max(double value) { if (m_Last) m_Last->Max = value; return *this; }
            ComponentBuilder& Range(double min, double max) { return Min(min).Max(max); }

            // A check for rules that span fields, such as rows that must be the same length. It runs only
            // when every field was read without problems, and reports problems with paths inside the
            // component (e.g. "/rows/3"). The generated schema cannot express these rules.
            ComponentBuilder& Validate(std::function<void(const T&, std::vector<ValidationError>&)> check)
            {
                m_Impl->Validator = std::move(check);
                return *this;
            }

        private:
            friend class ComponentRegistry;
            explicit ComponentBuilder(std::shared_ptr<detail::TypedComponentImpl<T>> impl) : m_Impl(std::move(impl)) {}

            std::shared_ptr<detail::TypedComponentImpl<T>> m_Impl;
            FieldInfo* m_Last = nullptr;    // the field the next Required/Min/Max applies to
    };

    // The components that scene files can use, by name. Each registration is the single source for
    // reading, writing, validating and documenting that component. No GL context needed.
    class EXPORT ComponentRegistry
    {
        public:
            // Starts a registration. The name must be a letter followed by letters and digits and
            // unused, and T must not already be registered; otherwise the problem is logged and the
            // returned builder is detached (it does nothing).
            template<typename T>
            ComponentBuilder<T> Register(const std::string& name, const std::string& description);

            // nullptr if there is no component with this name
            const ComponentInfo* Find(const std::string& name) const;

            // Registration order
            std::vector<std::string> GetNames() const;
            std::vector<const ComponentInfo*> GetComponents() const;

            // Reads `data` (an object) into a new component on the entity. Returns every problem found:
            // an unknown component, an unknown or mistyped field, a value out of range, a missing
            // required field, or a duplicate component. If there is any, nothing is added.
            std::vector<ValidationError> Read(Entity entity, const std::string& component, const Json& data) const;

            // The component's fields as an object in declaration order; with skipDefaults, only the
            // ones that differ from their defaults. null if the entity lacks the component.
            Json Write(const Entity& entity, const std::string& component, bool skipDefaults = false) const;

            // The names of the components the entity has, in registration order
            std::vector<std::string> GetComponentNames(const Entity& entity) const;

        private:
            bool Add(const std::shared_ptr<detail::ComponentImpl>& impl);

            std::vector<std::shared_ptr<detail::ComponentImpl>> m_Components;
    };

    // Registers Transform, SpriteRenderer, Camera, Tilemap and Random.
    EXPORT void RegisterBuiltinComponents(ComponentRegistry& registry);

    template<typename T>
    template<typename M>
    ComponentBuilder<T>& ComponentBuilder<T>::Field(M T::* member, const std::string& name, const std::string& description)
    {
        using Traits = detail::FieldTraits<M>;

        m_Last = nullptr;
        for (const FieldInfo& existing : m_Impl->Info.Fields) {
            if (existing.Name == name) {
                detail::ReportRegistryError("component '" + m_Impl->Info.Name + "' already has a field named '" + name + "'");
                return *this;
            }
        }

        FieldInfo info;
        info.Name = name;
        info.Description = description;
        info.Type = Traits::Type;
        info.Default = Traits::ToJson(T{}.*member);
        m_Impl->Info.Fields.push_back(std::move(info));
        m_Last = &m_Impl->Info.Fields.back();

        typename detail::TypedComponentImpl<T>::FieldOps ops;
        ops.Read = [member](const Json& j, T& out, const FieldInfo& field, const std::string& path, std::vector<ValidationError>& errors) {
            Traits::Read(j, out.*member, field, path, errors);
        };
        ops.Write = [member](const T& in) { return Traits::ToJson(in.*member); };
        m_Impl->Ops.push_back(std::move(ops));
        return *this;
    }

    template<typename T>
    ComponentBuilder<T> ComponentRegistry::Register(const std::string& name, const std::string& description)
    {
        auto impl = std::make_shared<detail::TypedComponentImpl<T>>(name, description);
        Add(impl);   // if refused the builder still works on `impl`, which nothing else references
        return ComponentBuilder<T>(std::move(impl));
    }

}
