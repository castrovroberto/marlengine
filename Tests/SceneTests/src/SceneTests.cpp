// Scene and Entity: naming, components, handle safety and iteration. Headless.
// Exits non-zero if any check fails.
#include "Marle/Scene/Scene.h"
#include "Marle/Scene/Components.h"
#include "Marle/Scene/ComponentRegistry.h"
#include "Marle/Scene/SceneLoader.h"
#include "Marle/Scene/SceneWriter.h"
#include "Marle/Scene/SceneSchema.h"
#include "Marle/Scene/SceneTool.h"
#include "Marle/Scene/SceneReloader.h"
#include "Marle/Scene/RandomGenerator.h"
#include "Marle/Core/KeyNames.h"
#include "Marle/Core/KeyCodes.h"
#include <nlohmann/json-schema.hpp>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sys/wait.h>
#include <sstream>
#include <string>
#include <vector>

using namespace Marle;

static int fails = 0;
#define CHECK(c, name) do { if (!(c)) { fails++; printf("FAIL: %s\n", name); } else { printf("ok:   %s\n", name); } } while (0)

struct Position { float x = 0.0f, y = 0.0f; Position() = default; Position(float x, float y) : x(x), y(y) {} };
struct Health { int points = 10; };
struct Gravity { float Strength = 9.8f; };
struct Follow { std::string Target; glm::vec2 Offset = { 0.0f, 0.0f }; };
struct Binding { std::string Key = "Q"; };
struct Pad { std::string Keys = "both"; std::string Label; int Count = 0; };
struct Door { std::string Target; int Level = 1; bool Locked = false; };   // Target is required

static std::string ReadFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

static std::vector<std::string> SplitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::stringstream stream(text);
    for (std::string line; std::getline(stream, line);) lines.push_back(line);
    return lines;
}

// MARLE_UPDATE_GOLDEN=1 rewrites the committed generated files (the schema and the canonical fixture)
// instead of failing when they are stale.
static bool UpdateGolden()
{
    const char* value = std::getenv("MARLE_UPDATE_GOLDEN");
    return value && value[0] == '1';
}

static void WriteFile(const std::string& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

// Checks scene JSON against a JSON Schema and collects every error as "pointer: message"
class CollectingHandler : public nlohmann::json_schema::basic_error_handler {
    public:
        std::vector<std::string> Errors;
        void error(const nlohmann::json::json_pointer& pointer, const nlohmann::json& instance, const std::string& message) override
        {
            nlohmann::json_schema::basic_error_handler::error(pointer, instance, message);
            Errors.push_back(pointer.to_string() + ": " + message);
        }
};

static std::vector<std::string> SchemaErrors(const Json& schema, const Json& instance)
{
    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(nlohmann::json::parse(schema.dump()));
    CollectingHandler handler;
    validator.validate(nlohmann::json::parse(instance.dump()), handler);
    return handler.Errors;
}

static Json ParseWithComments(const std::string& text) { return Json::parse(text, nullptr, true, true); }

// The entity a pointer like "/entities/7/..." is inside, or -1
static int EntityIndexOf(const std::string& pointer)
{
    if (pointer.rfind("/entities/", 0) != 0) return -1;
    return std::atoi(pointer.c_str() + 10);
}

struct ToolRun { int Code; std::string Out, Err; };

static ToolRun RunTool(const ComponentRegistry& registry, const std::vector<std::string>& args)
{
    std::ostringstream out, err;
    int code = RunSceneTool(args, registry, out, err);
    return { code, out.str(), err.str() };
}

static bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// Runs a shell command, returning its exit status and everything it printed
static ToolRun RunProcess(const std::string& command)
{
    ToolRun run{ -1, "", "" };
    FILE* pipe = popen((command + " 2>&1").c_str(), "r");
    if (!pipe) return run;
    char buffer[4096];
    while (size_t n = fread(buffer, 1, sizeof(buffer), pipe)) run.Out.append(buffer, n);
    int status = pclose(pipe);
    run.Code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return run;
}

static bool HasProblem(const SceneLoadResult& result, const std::string& path, const std::string& text)
{
    for (const SceneProblem& p : result.Problems)
        if (p.Path == path && p.Message.find(text) != std::string::npos) return true;
    return false;
}

static bool HasError(const std::vector<ValidationError>& errors, const std::string& path, const std::string& text)
{
    for (const ValidationError& e : errors)
        if (e.Path == path && e.Message.find(text) != std::string::npos) return true;
    return false;
}

int main(int argc, char** argv)
{
    // naming
    {
        Scene scene("garden");
        CHECK(scene.GetName() == "garden" && Scene().GetName() == "Untitled", "a scene has a name, with a default");
        CHECK(scene.GetEntityCount() == 0 && scene.GetEntities().empty(), "a new scene is empty");

        Entity player = scene.CreateEntity("player");
        CHECK(player.IsValid() && static_cast<bool>(player) && player.GetName() == "player", "a created entity is valid and keeps its name");
        CHECK(scene.GetEntityCount() == 1, "the entity count goes up");
        CHECK(scene.FindEntity("player") == player, "FindEntity returns the same entity");
        CHECK(!scene.FindEntity("nobody").IsValid(), "FindEntity on an unknown name is an invalid entity");

        Entity duplicate = scene.CreateEntity("player");
        CHECK(!duplicate.IsValid(), "a duplicate name gives an invalid entity");
        CHECK(scene.GetEntityCount() == 1 && player.IsValid() && scene.FindEntity("player") == player, "a rejected duplicate leaves the scene unchanged");

        Entity unnamed = scene.CreateEntity("");
        CHECK(!unnamed.IsValid() && scene.GetEntityCount() == 1, "an empty name is rejected");
        CHECK(scene.CreateEntity("Player").IsValid(), "names are case sensitive");
    }

    // components
    {
        Scene scene;
        Entity e = scene.CreateEntity("e");
        CHECK(!e.Has<Position>() && e.TryGet<Position>() == nullptr, "a new entity has no components");

        Position& added = e.Add<Position>(3.0f, 4.0f);
        CHECK(e.Has<Position>() && added.x == 3.0f && added.y == 4.0f, "Add constructs the component from its arguments");
        CHECK(!e.Has<Health>(), "other components are not affected");

        e.Get<Position>().x = 7.0f;
        CHECK(e.TryGet<Position>() && e.TryGet<Position>()->x == 7.0f, "Get returns the stored component");
        const Entity& view = e;
        CHECK(view.TryGet<Position>() && view.TryGet<Position>()->y == 4.0f, "TryGet works on a const handle");

        Position& again = e.Add<Position>(99.0f, 99.0f);
        CHECK(&again == e.TryGet<Position>() && again.x == 7.0f, "adding a component twice keeps the existing one unchanged");

        Health& scratch = e.Get<Health>();
        scratch.points = 1;
        CHECK(!e.Has<Health>(), "Get on a missing component does not add it");
        CHECK(e.Get<Health>().points == 10, "the scratch component for a missing one is reset each time");

        CHECK(e.Remove<Position>() && !e.Has<Position>(), "Remove takes a component off");
        CHECK(!e.Remove<Position>(), "removing a missing component reports false");
    }

    // handle safety
    {
        Scene scene;
        Entity a = scene.CreateEntity("a");
        Entity copy = a;
        a.Add<Position>(1.0f, 2.0f);

        CHECK(scene.DestroyEntity(a), "DestroyEntity reports success");
        CHECK(!a.IsValid() && !copy.IsValid(), "the handle and its copies go invalid");
        CHECK(!scene.FindEntity("a").IsValid() && scene.GetEntityCount() == 0, "a destroyed entity is gone from the scene");
        CHECK(!scene.DestroyEntity(a), "destroying an already destroyed entity reports false");

        CHECK(!a.Has<Position>() && a.TryGet<Position>() == nullptr && !a.Remove<Position>() && a.GetName().empty(), "queries on a stale handle answer no");
        a.Add<Position>(5.0f, 5.0f);   // logs, must not crash or revive it
        CHECK(!a.IsValid() && !a.Has<Position>(), "Add on a stale handle does nothing");

        // the name is free again, and a new entity (likely reusing the slot) is not the old one
        Entity b = scene.CreateEntity("a");
        CHECK(b.IsValid() && b.GetName() == "a" && !a.IsValid(), "a name can be reused, the old handle stays invalid");
        CHECK(a != b && a == copy, "a new entity differs from a stale handle");
        CHECK(!b.Has<Position>(), "a new entity does not inherit the old one's components");

        Entity none;
        CHECK(!none.IsValid() && none.GetName().empty() && !none.Has<Position>() && none == Entity(), "a default handle is invalid and safe to query");
        none.Add<Health>();
        CHECK(!none.IsValid(), "Add on a default handle does nothing");
    }

    // scene lifetime and ownership
    {
        Entity survivor;
        {
            Scene scene;
            survivor = scene.CreateEntity("x");
            survivor.Add<Health>();
            CHECK(survivor.IsValid() && survivor.Has<Health>(), "the handle works while its scene lives");
        }
        CHECK(!survivor.IsValid() && !survivor.Has<Health>() && survivor.GetName().empty(), "a handle is invalid once its scene is gone");
        survivor.Add<Health>();   // logs, must not crash
        CHECK(!survivor.IsValid(), "Add after the scene is gone does nothing");

        Scene one, two;
        Entity mine = one.CreateEntity("same");
        Entity theirs = two.CreateEntity("same");
        CHECK(mine.IsValid() && theirs.IsValid() && mine != theirs, "scenes have their own name spaces and entities differ");
        CHECK(!two.DestroyEntity(mine) && mine.IsValid(), "a scene refuses to destroy another scene's entity");
        CHECK(one.DestroyEntity(mine) && theirs.IsValid() && two.FindEntity("same") == theirs, "destroying in one scene leaves the other alone");
    }

    // iteration
    {
        Scene scene;
        Entity a = scene.CreateEntity("a"), b = scene.CreateEntity("b"), c = scene.CreateEntity("c"), d = scene.CreateEntity("d");
        a.Add<Position>(1.0f, 0.0f);
        b.Add<Health>();
        c.Add<Position>(3.0f, 0.0f); c.Add<Health>();
        d.Add<Position>(4.0f, 0.0f);

        std::vector<std::string> seen;
        scene.ForEach<Position>([&](Entity e, Position& p) { seen.push_back(e.GetName()); p.y = 1.0f; });
        CHECK(seen.size() == 3 && c.Get<Position>().y == 1.0f && !b.Has<Position>(), "ForEach<T> visits entities with T and lets them change it");

        int both = 0;
        scene.ForEach<Position, Health>([&](Entity e, Position&, Health&) { both += e.GetName() == "c"; });
        CHECK(both == 1, "ForEach<A, B> visits only entities with both");

        scene.DestroyEntity(b);
        std::string order;
        for (Entity e : scene.GetEntities()) order += e.GetName();
        CHECK(order == "acd", "GetEntities is in creation order, skipping destroyed entities");
        Entity e = scene.CreateEntity("e");
        order.clear();
        for (Entity x : scene.GetEntities()) order += x.GetName();
        CHECK(order == "acde" && e.IsValid(), "a new entity goes last");
    }

    // many entities
    {
        Scene scene;
        for (int i = 0; i < 2000; i++) scene.CreateEntity("entity" + std::to_string(i)).Add<Health>().points = i;
        CHECK(scene.GetEntityCount() == 2000 && scene.FindEntity("entity1234").Get<Health>().points == 1234, "lookup by name works across many entities");
        for (int i = 0; i < 2000; i += 2) scene.DestroyEntity(scene.FindEntity("entity" + std::to_string(i)));
        CHECK(scene.GetEntityCount() == 1000 && !scene.FindEntity("entity10").IsValid() && scene.FindEntity("entity11").Get<Health>().points == 11, "destroying half keeps the rest intact");
    }

    // component registry: registration
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        std::vector<std::string> names = registry.GetNames();
        CHECK(names.size() == 5 && names[0] == "Transform" && names[1] == "SpriteRenderer" && names[2] == "Camera" && names[3] == "Tilemap" && names[4] == "Random", "the built-in components register in order");

        const ComponentInfo* transform = registry.Find("Transform");
        CHECK(transform && !transform->Description.empty() && transform->Fields.size() == 3, "a component has a description and its fields");
        CHECK(transform->Fields[0].Name == "position" && transform->Fields[0].Type == FieldType::Vec2 && transform->Fields[0].Default == Json::array({ 0.0, 0.0 }), "a field has a name, type and default taken from the component");
        CHECK(transform->Fields[2].Name == "scale" && transform->Fields[2].Default == Json::array({ 1.0, 1.0 }) && !transform->Fields[0].Description.empty(), "defaults come from the default-constructed component, every field has a description");
        CHECK(registry.Find("Camera")->Fields[0].Min == 0.01 && registry.Find("Camera")->Fields[0].Max == 100.0, "a field can have a range");
        CHECK(registry.Find("SpriteRenderer")->Fields[3].Type == FieldType::Vec4 && registry.Find("SpriteRenderer")->Fields[1].Type == FieldType::IVec2, "vec4 and ivec2 fields are described");
        CHECK(registry.Find("Nope") == nullptr, "Find on an unknown name is null");

        ComponentRegistry other;
        other.Register<Health>("Health", "Hit points.").Field(&Health::points, "points", "How many.");
        other.Register<Health>("Health", "again");
        CHECK(other.GetNames().size() == 1, "a duplicate name is refused");
        other.Register<Health>("Vitality", "same type");
        CHECK(other.GetNames().size() == 1, "one C++ type cannot be registered twice");
        other.Register<Position>("not valid", "bad name");
        other.Register<Position>("", "empty name");
        other.Register<Position>("9lives", "starts with a digit");
        CHECK(other.GetNames().size() == 1, "invalid component names are refused");
        ComponentBuilder<Position> detached = other.Register<Position>("Health", "refused");
        detached.Field(&Position::x, "x", "ignored");
        CHECK(other.Find("Position") == nullptr && other.Find("Health")->Fields.size() == 1, "a refused registration changes nothing");

        other.Register<Position>("Position", "Somewhere.").Field(&Position::x, "x", "East.").Field(&Position::x, "x", "again").Field(&Position::y, "y", "North.");
        CHECK(other.Find("Position")->Fields.size() == 2, "a repeated field name is ignored");
    }

    // component registry: reading
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        Scene scene;

        Entity e = scene.CreateEntity("e");
        auto errors = registry.Read(e, "Transform", Json::parse(R"({"position": [1, 2], "rotation": 0.5})"));
        const Transform* t = e.TryGet<Transform>();
        CHECK(errors.empty() && t && t->Position == glm::vec2(1.0f, 2.0f) && t->Rotation == 0.5f, "valid data becomes a component");
        CHECK(t && t->Scale == glm::vec2(1.0f, 1.0f), "a field that is left out keeps its default");

        Entity empty = scene.CreateEntity("empty");
        CHECK(registry.Read(empty, "Camera", Json::object()).empty() && empty.Has<Camera>() && empty.Get<Camera>().Zoom == 1.0f, "an empty object gives a component with all defaults");

        Entity typo = scene.CreateEntity("typo");
        errors = registry.Read(typo, "SpriteRenderer", Json::parse(R"({"texure": "a.png", "tin": [1, 1, 1, 1]})"));
        CHECK(HasError(errors, "/texure", "did you mean 'texture'") && HasError(errors, "/tin", "did you mean 'tint'") && errors.size() == 2, "unknown fields are errors that suggest the field meant");
        CHECK(HasError(errors, "/texure", "valid fields: texture, cell, cellSize, tint") && !typo.Has<SpriteRenderer>(), "the error lists the valid fields and the component is not added");

        Entity wrong = scene.CreateEntity("wrong");
        errors = registry.Read(wrong, "Transform", Json::parse(R"({"position": "here", "rotation": [1]})"));
        CHECK(errors.size() == 2 && HasError(errors, "/position", "expected an array of 2 numbers, got a string") && HasError(errors, "/rotation", "expected a number, got an array"), "every wrong type is reported at once");
        CHECK(!wrong.Has<Transform>(), "a component with any error is not added");

        errors = registry.Read(wrong, "Transform", Json::parse(R"({"position": [1], "scale": [1, 2, 3]})"));
        CHECK(HasError(errors, "/position", "an array of 2 numbers, got an array of 1") && HasError(errors, "/scale", "got an array of 3"), "a vector of the wrong length is an error");
        errors = registry.Read(wrong, "Transform", Json::parse(R"({"position": [1, "x"]})"));
        CHECK(errors.size() == 1 && HasError(errors, "/position/1", "expected a number, got a string"), "an error in one element points at that element");

        errors = registry.Read(wrong, "Camera", Json::parse(R"({"zoom": 0, "primary": "yes"})"));
        CHECK(HasError(errors, "/zoom", "must be at least 0.01, got 0") && HasError(errors, "/primary", "expected true or false, got a string"), "range and bool errors are reported");
        errors = registry.Read(wrong, "Camera", Json::parse(R"({"zoom": 1000})"));
        CHECK(HasError(errors, "/zoom", "must be at most 100"), "a value above its maximum is an error");
        errors = registry.Read(wrong, "SpriteRenderer", Json::parse(R"({"tint": [2, 0, 0, 1], "cell": [-1, 0]})"));
        CHECK(HasError(errors, "/tint/0", "must be at most 1") && HasError(errors, "/cell/0", "must be at least 0"), "the range applies to every element of a vector");
        errors = registry.Read(wrong, "SpriteRenderer", Json::parse(R"({"cell": [1.5, 2]})"));
        CHECK(HasError(errors, "/cell/0", "expected an integer, got a number with a fraction"), "an int field rejects a fraction");
        errors = registry.Read(wrong, "SpriteRenderer", Json::parse(R"({"cell": [1, 99999999999]})"));
        CHECK(HasError(errors, "/cell/1", "too large"), "an int field rejects a number that does not fit");
        errors = registry.Read(wrong, "SpriteRenderer", Json::parse(R"({"texture": 5})"));
        CHECK(HasError(errors, "/texture", "expected a string, got a number"), "a string field rejects other types");
        errors = registry.Read(wrong, "Transform", Json::parse(R"({"rotation": 1e300})"));
        CHECK(HasError(errors, "/rotation", "too large for a float"), "a float field rejects a number that does not fit");
        errors = registry.Read(wrong, "Transform", Json::array());
        CHECK(errors.size() == 1 && HasError(errors, "", "expected an object, got an array"), "the data of a component must be an object");
        errors = registry.Read(wrong, "Transform", Json());
        CHECK(HasError(errors, "", "expected an object, got null"), "null data is an error");
        CHECK(!wrong.Has<Transform>() && !wrong.Has<Camera>() && !wrong.Has<SpriteRenderer>(), "none of the failed reads added anything");

        errors = registry.Read(e, "Transform", Json::parse(R"({"position": [9, 9]})"));
        CHECK(HasError(errors, "", "already has a Transform") && e.Get<Transform>().Position == glm::vec2(1.0f, 2.0f), "a second component of the same kind is an error and changes nothing");

        errors = registry.Read(wrong, "Transfrom", Json::object());
        CHECK(errors.size() == 1 && HasError(errors, "", "did you mean 'Transform'") && HasError(errors, "", "known components: Transform, SpriteRenderer, Camera"), "an unknown component suggests and lists the known ones");
        errors = registry.Read(Entity(), "Transform", Json::object());
        CHECK(HasError(errors, "", "the entity is invalid"), "reading into an invalid entity is an error");

        // a client component with a required field
        registry.Register<Door>("Door", "Leads somewhere.")
            .Field(&Door::Target, "target", "Name of the entity to go to.").Required()
            .Field(&Door::Level, "level", "Key level needed.").Range(1, 5)
            .Field(&Door::Locked, "locked", "Starts locked.");
        Entity door = scene.CreateEntity("door");
        errors = registry.Read(door, "Door", Json::parse(R"({"level": 2})"));
        CHECK(HasError(errors, "/target", "missing required field 'target'") && !door.Has<Door>(), "a missing required field is an error");
        CHECK(registry.Find("Door")->Fields[0].Required && !registry.Find("Door")->Fields[1].Required, "required is recorded in the field info");
        errors = registry.Read(door, "Door", Json::parse(R"({"target": "cellar", "locked": true})"));
        CHECK(errors.empty() && door.Get<Door>().Target == "cellar" && door.Get<Door>().Level == 1 && door.Get<Door>().Locked, "a client component reads like a built-in one");
    }

    // component registry: writing
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        Scene scene;
        Entity e = scene.CreateEntity("e");

        CHECK(registry.Write(e, "Transform").is_null() && registry.Write(e, "Nope").is_null(), "writing an absent or unknown component gives null");
        CHECK(registry.GetComponentNames(e).empty(), "an entity with no components lists none");

        registry.Read(e, "Transform", Json::parse(R"({"position": [1, 2], "rotation": 0.1})"));
        registry.Read(e, "Camera", Json::parse(R"({"primary": true})"));
        Json written = registry.Write(e, "Transform");
        CHECK(written == Json::parse(R"({"position": [1.0, 2.0], "rotation": 0.1, "scale": [1.0, 1.0]})"), "a component writes all its fields");
        CHECK(written.dump() == R"({"position":[1.0,2.0],"rotation":0.1,"scale":[1.0,1.0]})", "fields are written in declaration order");
        CHECK(written["rotation"].dump() == "0.1", "a float is written as the shortest number that reads back the same");

        Json sparse = registry.Write(e, "Transform", true);
        CHECK(sparse.dump() == R"({"position":[1.0,2.0],"rotation":0.1})", "skipDefaults leaves out fields that equal their default");
        CHECK(registry.Write(e, "Camera", true).dump() == R"({"primary":true})", "skipDefaults keeps only the changed field");
        Entity plain = scene.CreateEntity("plain");
        registry.Read(plain, "Camera", Json::object());
        CHECK(registry.Write(plain, "Camera", true) == Json::object() && registry.Write(plain, "Camera").size() == 2, "an all-default component writes as an empty object with skipDefaults");

        std::vector<std::string> present = registry.GetComponentNames(e);
        CHECK(present.size() == 2 && present[0] == "Transform" && present[1] == "Camera", "GetComponentNames lists the entity's components in registration order");

        // read what was written
        Entity copy = scene.CreateEntity("copy");
        CHECK(registry.Read(copy, "Transform", written).empty() && registry.Write(copy, "Transform") == written, "what is written reads back to the same component");
        Entity from_sparse = scene.CreateEntity("from_sparse");
        CHECK(registry.Read(from_sparse, "Transform", sparse).empty() && registry.Write(from_sparse, "Transform") == written, "the sparse form reads back to the same component");

        // every float of every built-in field survives a write and read exactly
        Entity odd = scene.CreateEntity("odd");
        odd.Add<SpriteRenderer>().Tint = { 0.1f, 0.2f, 0.3f, 1.0f / 3.0f };
        Entity odd2 = scene.CreateEntity("odd2");
        CHECK(registry.Read(odd2, "SpriteRenderer", registry.Write(odd, "SpriteRenderer")).empty() && odd2.Get<SpriteRenderer>().Tint == odd.Get<SpriteRenderer>().Tint, "floats round-trip exactly");
        CHECK(registry.Write(odd, "SpriteRenderer")["tint"].dump() == "[0.1,0.2,0.3,0.33333334]", "floats stay short in the written text");
    }

    // JSON text
    {
        const char* text = "{\n  // a comment\n  \"position\": [3, 4] /* inline */\n}";
        Json parsed = Json::parse(text, nullptr, true, true);
        CHECK(parsed["position"][1] == 4, "scene JSON can contain comments");
        bool threw = false;
        try { Json bad = Json::parse("{\"position\": [3, 4,]}"); (void)bad; } catch (const Json::exception&) { threw = true; }
        CHECK(threw, "a trailing comma is a parse error");
    }

    // scene loader: valid files
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);

        SceneLoadResult example = LoadScene("Assets/Scenes/example.json", registry);
        for (const SceneProblem& problem : example.Problems) printf("      %s\n", problem.ToString().c_str());
        CHECK(example.Ok() && example.Scene->GetName() == "Example garden", "Assets/Scenes/example.json loads with no problems");
        CHECK(example.Scene && example.Scene->GetEntityCount() == 8, "every entity in the example loads");
        CHECK(example.Scene->FindEntity("player").Has<Transform>() && example.Scene->FindEntity("camera").Get<Camera>().Primary && !example.Scene->FindEntity("spawn_point").Has<Transform>(), "the example's components read as written");
        CHECK(example.Scene->FindEntity("grass_east").Get<SpriteRenderer>().Cell == glm::ivec2(1, 0) && example.Scene->FindEntity("spinner").Get<Transform>().Rotation == 0.7854f, "values keep their types");
        std::string order;
        for (Entity e : example.Scene->GetEntities()) order += e.GetName() + " ";
        CHECK(order == "camera player grass_west grass_east pond spinner marker spawn_point ", "entities are created in file order");

        // the first jsonc block in docs/scene-format.md is a complete, valid scene file
        std::ifstream doc("docs/scene-format.md");
        std::stringstream docText;
        docText << doc.rdbuf();
        std::string all = docText.str();
        size_t begin = all.find("```jsonc\n"), end = begin == std::string::npos ? std::string::npos : all.find("\n```", begin);
        CHECK(doc.is_open() && begin != std::string::npos && end != std::string::npos, "the spec has a jsonc example");
        if (begin != std::string::npos && end != std::string::npos) {
            SceneLoadResult snippet = LoadSceneFromText(all.substr(begin + 8, end - begin - 8), "docs/scene-format.md", registry);
            for (const SceneProblem& problem : snippet.Problems) printf("      %s\n", problem.ToString().c_str());
            CHECK(snippet.Ok() && snippet.Scene->FindEntity("player").Get<Transform>().Position == glm::vec2(10.0f, 20.0f), "the spec's own example is a valid scene");
        }

        SceneLoadResult empty = LoadSceneFromText(R"({"version": 1, "name": "x", "entities": []})", "inline", registry);
        CHECK(empty.Ok() && empty.Scene->GetName() == "x" && empty.Scene->GetEntityCount() == 0, "a scene with no entities is valid");
        SceneLoadResult withSchema = LoadSceneFromText(R"({"$schema": "scene.schema.json", "version": 1, "name": "x", "entities": []})", "inline", registry);
        CHECK(withSchema.Ok(), "$schema is allowed");

        std::string big = R"({"version": 1, "name": "big", "entities": [)";
        for (int i = 0; i < 2000; i++) big += std::string(i ? "," : "") + R"({"name": "e)" + std::to_string(i) + R"(", "components": {"Transform": {"position": [1, 2]}}})";
        SceneLoadResult many = LoadSceneFromText(big + "]}", "inline", registry);
        CHECK(many.Ok() && many.Scene->GetEntityCount() == 2000 && many.Scene->FindEntity("e1999").Has<Transform>(), "a large scene loads");

        CHECK(IsValidEntityName("player") && IsValidEntityName("Tile 3-b.x") && IsValidEntityName(std::string(64, 'a')), "ordinary names are valid");
        std::string accented64, accented65;
        for (int i = 0; i < 64; i++) accented64 += "\xC3\xA9";   // 64 characters, 128 bytes
        accented65 = accented64 + "\xC3\xA9";
        CHECK(IsValidEntityName(accented64) && !IsValidEntityName(accented65), "the length limit counts characters, not bytes");
        CHECK(!IsValidEntityName("") && !IsValidEntityName(std::string(65, 'a')) && !IsValidEntityName("a/b") && !IsValidEntityName(" a") && !IsValidEntityName("a ") && !IsValidEntityName("a\tb") && !IsValidEntityName("a\nb"), "empty, long, slash, edge-whitespace and control-character names are not");

        const FieldInfo& texture = registry.Find("SpriteRenderer")->Fields[0];
        CHECK(texture.AssetPath && !registry.Find("SpriteRenderer")->Fields[1].AssetPath && !registry.Find("Transform")->Fields[0].AssetPath, "only the texture field is marked as an asset path");
    }

    // scene loader: files that cannot be used as a whole
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        const std::string dir = "Tests/SceneTests/fixtures/";

        SceneLoadResult missing = LoadScene(dir + "does_not_exist.json", registry);
        CHECK(!missing.Scene && !missing.Ok() && missing.Problems.size() == 1 && HasProblem(missing, "", "cannot open the file"), "a missing file gives no scene and says so");

        SceneLoadResult syntax = LoadScene(dir + "bad_syntax.json", registry);
        for (const SceneProblem& problem : syntax.Problems) printf("      %s\n", problem.ToString().c_str());
        CHECK(!syntax.Scene && syntax.Problems.size() == 1 && HasProblem(syntax, "", "line 5"), "a syntax error gives no scene and names the line");

        SceneLoadResult array = LoadScene(dir + "top_level_array.json", registry);
        CHECK(!array.Scene && HasProblem(array, "", "the top level must be an object, got an array"), "a top level that is not an object gives no scene");

        SceneLoadResult future = LoadScene(dir + "future_version.json", registry);
        CHECK(!future.Scene && HasProblem(future, "/version", "version 2 but this engine reads version 1"), "a newer version is rejected, naming both");

        CHECK(!LoadScene(dir + "missing_version.json", registry).Scene && HasProblem(LoadScene(dir + "missing_version.json", registry), "", "missing required key 'version'"), "a missing version gives no scene");
        CHECK(!LoadScene(dir + "version_not_integer.json", registry).Scene && HasProblem(LoadScene(dir + "version_not_integer.json", registry), "/version", "expected an integer, got a string"), "a version that is not an integer gives no scene");

        SceneLoadResult repeated = LoadScene(dir + "repeated_top_key.json", registry);
        CHECK(!repeated.Scene && HasProblem(repeated, "/name", "'name' appears more than once"), "a repeated top-level key gives no scene");
    }

    // scene loader: problems that leave a usable scene
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        const std::string dir = "Tests/SceneTests/fixtures/";

        SceneLoadResult notArray = LoadScene(dir + "entities_not_array.json", registry);
        CHECK(notArray.Scene && notArray.Scene->GetName() == "Entities is not a list" && notArray.Scene->GetEntityCount() == 0 && HasProblem(notArray, "/entities", "expected an array, got an object"), "entities that is not an array gives an empty scene and a problem");

        SceneLoadResult top = LoadScene(dir + "top_level_problems.json", registry);
        CHECK(top.Scene && top.Scene->GetName() == "Untitled" && top.Scene->FindEntity("a"), "a missing name falls back to Untitled and the entities still load");
        CHECK(HasProblem(top, "", "missing required key 'name'") && HasProblem(top, "/extra", "unknown key 'extra'") && HasProblem(top, "/$schema", "expected a string, got a number") && top.Problems.size() == 3, "unknown top-level keys, a bad $schema and a missing name are each reported");

        SceneLoadResult r = LoadScene(dir + "mixed_errors.json", registry);
        for (const SceneProblem& problem : r.Problems) printf("      %s\n", problem.ToString().c_str());
        CHECK(r.Scene && !r.Ok() && r.Scene->GetName() == "Mixed errors", "a file full of mistakes still loads a scene");
        printf("      %d problems\n", (int)r.Problems.size());
        CHECK(r.Problems.size() == 14, "every mistake is reported once");

        std::string loaded;
        for (Entity e : r.Scene->GetEntities()) loaded += e.GetName() + " ";
        CHECK(loaded == "good_first unknown_component wrong_type unknown_field missing_texture unsafe_path range extra_key components_not_object good_last ", "the good entities load, in file order, and the bad ones are skipped");

        Scene& scene = *r.Scene;
        CHECK(scene.FindEntity("good_first").Get<Transform>().Position == glm::vec2(1.0f, 2.0f), "a good entity before the mistakes is untouched");
        CHECK(HasProblem(r, "/entities/1/components/Transfrom", "did you mean 'Transform'") && !scene.FindEntity("unknown_component").Has<Transform>() && scene.FindEntity("unknown_component").Get<Camera>().Zoom == 2.0f, "an unknown component is skipped with a suggestion, its siblings load");
        CHECK(HasProblem(r, "/entities/2/components/Transform/position", "expected an array of 2 numbers") && HasProblem(r, "/entities/2/components/Transform/rotation", "expected a number") && !scene.FindEntity("wrong_type").Has<Transform>() && scene.FindEntity("wrong_type").Has<Camera>(), "a component with wrong types is skipped whole, with a path for each, and its siblings load");
        CHECK(HasProblem(r, "/entities/3/components/SpriteRenderer/texure", "did you mean 'texture'") && !scene.FindEntity("unknown_field").Has<SpriteRenderer>(), "an unknown field skips the component, with a suggestion");
        CHECK(HasProblem(r, "/entities/4/name", "an entity named 'good_first' already exists") && scene.GetEntityCount() == 10, "a duplicate entity name is skipped and the first one stays");
        CHECK(HasProblem(r, "/entities/5/name", "'bad/name' is not a valid entity name") && !scene.FindEntity("bad/name"), "an invalid entity name is skipped");
        CHECK(HasProblem(r, "/entities/6", "an entity must be an object, got a string"), "an entity that is not an object is skipped");
        CHECK(HasProblem(r, "/entities/7/components/SpriteRenderer/texture", "the file 'Assets/Textures/nope.png' does not exist") && scene.FindEntity("missing_texture").Has<SpriteRenderer>(), "a missing asset is reported but the component loads");
        CHECK(HasProblem(r, "/entities/8/components/SpriteRenderer/texture", "must not contain '..'") && !scene.FindEntity("unsafe_path").Has<SpriteRenderer>() && scene.FindEntity("unsafe_path").Has<Camera>(), "an asset path that leaves the working directory skips the component");
        CHECK(HasProblem(r, "/entities/9/components/Camera/zoom", "'zoom' appears more than once") && !scene.FindEntity("repeated_key"), "a repeated key skips the entity that contains it");
        CHECK(HasProblem(r, "/entities/10/components/Camera/zoom", "must be at least 0.01") && !scene.FindEntity("range").Has<Camera>(), "an out-of-range value skips the component");
        CHECK(HasProblem(r, "/entities/11/colour", "unknown key 'colour'") && scene.FindEntity("extra_key").Get<Camera>().Primary, "an unknown entity key is reported and the entity still loads");
        CHECK(HasProblem(r, "/entities/12", "missing required key 'name'"), "an entity without a name is skipped");
        CHECK(HasProblem(r, "/entities/13/components", "expected an object, got an array") && scene.FindEntity("components_not_object"), "components that is not an object is reported and the entity still loads");
        CHECK(scene.FindEntity("good_last").Has<SpriteRenderer>(), "a good entity after the mistakes loads");

        bool formatted = !r.Problems.empty();
        for (const SceneProblem& p : r.Problems)
            formatted = formatted && p.File == dir + "mixed_errors.json" && p.ToString() == p.File + (p.Path.empty() ? "" : " " + p.Path) + ": " + p.Message;
        CHECK(formatted && r.Problems[1].ToString().find("mixed_errors.json /entities/1/components/Transfrom: unknown component 'Transfrom'") != std::string::npos, "a problem prints as file, path and message");

        // asset path rules
        auto assetProblem = [&](const std::string& path) {
            Json scene = Json::parse(R"({"version": 1, "name": "x", "entities": [{"name": "a", "components": {"SpriteRenderer": {"texture": ""}}}]})");
            scene["entities"][0]["components"]["SpriteRenderer"]["texture"] = path;
            return LoadSceneFromText(scene.dump(), "inline", registry);
        };
        CHECK(HasProblem(assetProblem("a\\b.png"), "/entities/0/components/SpriteRenderer/texture", "use '/'"), "a backslash in an asset path is rejected");
        CHECK(HasProblem(assetProblem("/etc/passwd"), "/entities/0/components/SpriteRenderer/texture", "must be relative"), "an absolute asset path is rejected");
        CHECK(HasProblem(assetProblem("C:/images/a.png"), "/entities/0/components/SpriteRenderer/texture", "must be relative"), "a drive letter in an asset path is rejected");
        CHECK(HasProblem(assetProblem("Assets/../../x.png"), "/entities/0/components/SpriteRenderer/texture", "must not contain '..'"), "'..' in the middle of an asset path is rejected");
        SceneLoadResult dots = assetProblem("Assets/a..b.png");
        CHECK(dots.Scene->FindEntity("a").Has<SpriteRenderer>() && HasProblem(dots, "/entities/0/components/SpriteRenderer/texture", "does not exist"), "'..' inside a name is just a name");
        CHECK(assetProblem("").Ok() && assetProblem("Assets/Textures/test_sprite.tga").Ok(), "an empty texture and an existing file are fine");
        CHECK(HasProblem(assetProblem("Assets/Textures"), "/entities/0/components/SpriteRenderer/texture", "does not exist"), "a directory is not a file");
    }

    // scene writer: canonical form
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);

        Scene scene("Golden");
        Entity a = scene.CreateEntity("a");
        a.Add<Camera>();                                   // added first, but written second
        a.Add<Transform>().Position = { 0.1f, -2.5f };
        a.Add<Health>();                                   // not registered: not written
        scene.CreateEntity("bare");
        SpriteRenderer& sprite = scene.CreateEntity("sprite").Add<SpriteRenderer>();
        sprite.Texture = "Assets/Textures/test_sprite.tga";
        sprite.Tint = { 1.0f, 0.5f, 0.25f, 0.1f };

        const std::string golden = R"({
  "version": 1,
  "name": "Golden",
  "entities": [
    {
      "name": "a",
      "components": {
        "Transform": { "position": [0.1, -2.5], "rotation": 0, "scale": [1, 1] },
        "Camera": { "zoom": 1, "primary": false }
      }
    },
    {
      "name": "bare"
    },
    {
      "name": "sprite",
      "components": {
        "SpriteRenderer": { "texture": "Assets/Textures/test_sprite.tga", "cell": [0, 0], "cellSize": [0, 0], "tint": [1, 0.5, 0.25, 0.1] }
      }
    }
  ]
}
)";
        SceneSaveResult saved = SaveSceneToText(scene, registry);
        for (const std::string& problem : saved.Problems) printf("      %s\n", problem.c_str());
        CHECK(saved.Ok() && saved.Text == golden, "a scene is written in canonical form");

        CHECK(SaveSceneToText(Scene("x"), registry).Text == "{\n  \"version\": 1,\n  \"name\": \"x\",\n  \"entities\": []\n}\n", "an empty scene writes an empty entities list");
        SceneWriteOptions withSchema;
        withSchema.Schema = "scene.schema.json";
        CHECK(SaveSceneToText(Scene("x"), registry, withSchema).Text == "{\n  \"$schema\": \"scene.schema.json\",\n  \"version\": 1,\n  \"name\": \"x\",\n  \"entities\": []\n}\n", "$schema is written first when asked for");
        CHECK(LoadSceneFromText(SaveSceneToText(Scene("x"), registry, withSchema).Text, "inline", registry).Ok(), "a file with $schema loads");

        // what is written loads back, and writing that gives the same text
        SceneLoadResult back = LoadSceneFromText(golden, "golden", registry);
        CHECK(back.Ok() && SaveSceneToText(*back.Scene, registry).Text == golden, "loading the canonical text and writing it again changes nothing");
        CHECK(back.Scene->FindEntity("a").Get<Transform>().Position == glm::vec2(0.1f, -2.5f) && back.Scene->FindEntity("sprite").Get<SpriteRenderer>().Tint.w == 0.1f, "the values read back exactly");

        // the canonical fixture is the example without its comments (and its $schema key)
        if (UpdateGolden()) WriteFile("Tests/SceneTests/fixtures/canonical.json", SaveSceneToText(*LoadScene("Assets/Scenes/example.json", registry).Scene, registry).Text);
        std::string canonical = ReadFile("Tests/SceneTests/fixtures/canonical.json");
        SceneLoadResult fromFixture = LoadScene("Tests/SceneTests/fixtures/canonical.json", registry);
        CHECK(!canonical.empty() && fromFixture.Ok() && SaveSceneToText(*fromFixture.Scene, registry).Text == canonical, "a canonical file round-trips byte for byte");
        SceneLoadResult fromExample = LoadScene("Assets/Scenes/example.json", registry);
        CHECK(fromExample.Ok() && SaveSceneToText(*fromExample.Scene, registry).Text == canonical, "the example, written out, is the canonical fixture (only its comments are gone)");
        CHECK(canonical.back() == '\n' && canonical.find('\r') == std::string::npos && canonical.find("\n\n") == std::string::npos && canonical.compare(0, 3, "\xEF\xBB\xBF") != 0, "one trailing newline, LF line endings, no BOM, no blank lines");

        // a change to one component changes only that component's line
        std::vector<std::string> before = SplitLines(canonical);
        fromFixture.Scene->FindEntity("player").Get<Transform>().Position = { 1030.0f, 520.5f };
        std::vector<std::string> after = SplitLines(SaveSceneToText(*fromFixture.Scene, registry).Text);
        int changed = 0;
        std::string changedLine;
        for (size_t i = 0; i < before.size() && i < after.size(); i++)
            if (before[i] != after[i]) { changed++; changedLine = after[i]; }
        CHECK(before.size() == after.size() && changed == 1 && changedLine.find("[1030, 520.5]") != std::string::npos, "changing one component changes exactly one line");

        // order does not depend on how the scene was built
        Scene reordered("Order");
        reordered.CreateEntity("second");
        Entity first = reordered.CreateEntity("first");
        first.Add<Camera>(); first.Add<SpriteRenderer>(); first.Add<Transform>();
        std::string text = SaveSceneToText(reordered, registry).Text;
        CHECK(text.find("\"second\"") < text.find("\"first\"") && text.find("Transform") < text.find("SpriteRenderer") && text.find("SpriteRenderer") < text.find("Camera"), "entities are written in creation order and components in registration order");
        reordered.DestroyEntity(reordered.FindEntity("second"));
        reordered.CreateEntity("third");
        text = SaveSceneToText(reordered, registry).Text;
        CHECK(text.find("\"first\"") < text.find("\"third\"") && text.find("second") == std::string::npos, "destroyed entities are gone and new ones go last");

        // numbers
        Scene numbers("Numbers");
        Entity n = numbers.CreateEntity("n");
        n.Add<Transform>() = Transform{ { 16777216.0f, -3.0f }, 1e-7f, { 3e38f, 123456.789f } };
        n.Add<Camera>().Zoom = 0.33333334f;
        std::string numText = SaveSceneToText(numbers, registry).Text;
        CHECK(numText.find("[16777216, -3]") != std::string::npos && numText.find("\"zoom\": 0.33333334") != std::string::npos && numText.find(".0,") == std::string::npos && numText.find(".0 ") == std::string::npos && numText.find(".0]") == std::string::npos, "whole numbers have no decimal point and others are the shortest text that reads back");
        SceneLoadResult numBack = LoadSceneFromText(numText, "numbers", registry);
        const Transform& nt = numBack.Scene->FindEntity("n").Get<Transform>();
        CHECK(numBack.Ok() && nt.Position == glm::vec2(16777216.0f, -3.0f) && nt.Rotation == 1e-7f && nt.Scale == glm::vec2(3e38f, 123456.789f) && numBack.Scene->FindEntity("n").Get<Camera>().Zoom == 0.33333334f, "extreme and awkward floats read back exactly");

        // text
        Scene text2("say \"hi\" \\ ñ ✓");
        text2.CreateEntity("ünïcode ✓").Add<SpriteRenderer>().Texture = "Assets/ñandú \"q\".png";
        std::string textText = SaveSceneToText(text2, registry).Text;
        CHECK(textText.find(R"("name": "say \"hi\" \\ ñ ✓")") != std::string::npos && textText.find("ünïcode ✓") != std::string::npos, "quotes and backslashes are escaped, UTF-8 is kept as it is");
        SceneLoadResult textBack = LoadSceneFromText(textText, "text", registry);
        CHECK(textBack.Scene && textBack.Scene->GetName() == "say \"hi\" \\ ñ ✓" && textBack.Scene->FindEntity("ünïcode ✓").Get<SpriteRenderer>().Texture == "Assets/ñandú \"q\".png", "names and strings read back unchanged");
    }

    // scene writer: refusing what would not read back
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);

        auto problemsOf = [&](auto&& build) {
            Scene scene("Bad");
            build(scene);
            return SaveSceneToText(scene, registry);
        };
        auto mentions = [](const SceneSaveResult& r, const std::string& text) {
            for (const std::string& p : r.Problems) if (p.find(text) != std::string::npos) return true;
            return false;
        };

        SceneSaveResult slash = problemsOf([](Scene& s) { s.CreateEntity("a/b"); });
        CHECK(!slash.Ok() && mentions(slash, "entity 'a/b': scene files cannot hold this name"), "an entity name scene files cannot hold is refused");
        CHECK(!problemsOf([](Scene& s) { s.CreateEntity(" lead"); }).Ok() && !problemsOf([](Scene& s) { s.CreateEntity(std::string(65, 'x')); }).Ok(), "leading whitespace and a too long name are refused");

        SceneSaveResult nan = problemsOf([](Scene& s) { s.CreateEntity("e").Add<Transform>().Position.x = std::nanf(""); });
        CHECK(!nan.Ok() && mentions(nan, "entity 'e' component 'Transform'/position/0: the number is not finite"), "a number that is not finite is refused, with where it is");
        SceneSaveResult inf = problemsOf([](Scene& s) { s.CreateEntity("e").Add<Transform>().Rotation = INFINITY; });
        CHECK(!inf.Ok() && mentions(inf, "/rotation: the number is not finite"), "infinity is refused too");

        SceneSaveResult range = problemsOf([](Scene& s) { s.CreateEntity("e").Add<Camera>().Zoom = 0.0f; });
        CHECK(!range.Ok() && mentions(range, "entity 'e' component 'Camera'/zoom: must be at least 0.01"), "a value outside its range is refused");

        SceneSaveResult unsafe = problemsOf([](Scene& s) { s.CreateEntity("e").Add<SpriteRenderer>().Texture = "../outside.png"; });
        CHECK(!unsafe.Ok() && mentions(unsafe, "entity 'e' component 'SpriteRenderer'/texture: the path must not contain '..'"), "an unsafe asset path is refused");

        SceneSaveResult utf8 = problemsOf([](Scene& s) { s.CreateEntity("e").Add<SpriteRenderer>().Texture = "bad\xff.png"; });
        CHECK(!utf8.Ok() && mentions(utf8, "not valid UTF-8"), "text that is not valid UTF-8 is refused");
        CHECK(!SaveSceneToText(Scene(""), registry).Ok(), "a scene without a name is refused");

        SceneSaveResult several = problemsOf([](Scene& s) { s.CreateEntity("a/b"); s.CreateEntity("e").Add<Camera>().Zoom = -1.0f; });
        CHECK(several.Problems.size() == 2, "every problem is reported, not just the first");
    }

    // scene writer: files
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        const std::string file = (std::filesystem::temp_directory_path() / "marle_scene_writer_test.json").string();
        std::filesystem::remove(file);

        Scene scene("On disk");
        scene.CreateEntity("hero").Add<Transform>().Position = { 4.0f, 5.0f };
        CHECK(SaveScene(scene, registry, file), "a scene saves to a file");
        CHECK(ReadFile(file) == SaveSceneToText(scene, registry).Text && !std::filesystem::exists(file + ".tmp"), "the file holds exactly the canonical text and no temporary file is left");
        SceneLoadResult loaded = LoadScene(file, registry);
        CHECK(loaded.Ok() && loaded.Scene->FindEntity("hero").Get<Transform>().Position == glm::vec2(4.0f, 5.0f), "the saved file loads");

        scene.FindEntity("hero").Get<Transform>().Position = { 7.0f, 8.0f };
        CHECK(SaveScene(scene, registry, file) && LoadScene(file, registry).Scene->FindEntity("hero").Get<Transform>().Position == glm::vec2(7.0f, 8.0f), "saving again replaces the file");

        std::string good = ReadFile(file);
        scene.FindEntity("hero").Get<Transform>().Rotation = std::nanf("");
        CHECK(!SaveScene(scene, registry, file) && ReadFile(file) == good && !std::filesystem::exists(file + ".tmp"), "a scene that cannot be written leaves the existing file as it was");

        CHECK(!SaveScene(Scene("x"), registry, (std::filesystem::temp_directory_path() / "marle_no_such_dir" / "x.json").string()), "an unwritable path reports failure");
        std::filesystem::remove(file);
    }

    // scene schema: generation
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);

        Json schema = GenerateSceneSchema(registry);
        CHECK(schema == GenerateSceneSchema(registry) && SceneSchemaToText(registry) == SceneSchemaToText(registry), "the schema is generated deterministically");
        std::string text = SceneSchemaToText(registry);
        CHECK(text.back() == '\n' && text[text.size() - 2] != '\n' && text.find('\r') == std::string::npos && Json::parse(text) == schema, "the schema text is JSON with one trailing newline");
        CHECK(schema["$schema"] == "http://json-schema.org/draft-07/schema#" && schema["type"] == "object" && schema["additionalProperties"] == false, "it is a draft-07 schema that forbids unknown keys");

        std::string order;
        for (auto it = schema["definitions"].begin(); it != schema["definitions"].end(); ++it) order += it.key() + " ";
        CHECK(order == "entity component_Transform component_SpriteRenderer component_Camera component_Tilemap component_Random ", "components are described in registration order");
        CHECK(schema["definitions"]["entity"]["properties"]["components"]["properties"]["Camera"]["$ref"] == "#/definitions/component_Camera", "an entity's components point at their definitions");

        bool everyField = true, described = !schema["description"].get<std::string>().empty();
        for (const ComponentInfo* info : registry.GetComponents()) {
            const Json& definition = schema["definitions"]["component_" + info->Name];
            described = described && definition["description"] == info->Description && definition["additionalProperties"] == false;
            for (const FieldInfo& field : info->Fields) {
                const Json& property = definition["properties"][field.Name];
                everyField = everyField && !property.is_null() && property["default"] == field.Default && property["description"].get<std::string>().find(field.Description) == 0 && !field.Description.empty();
            }
            everyField = everyField && definition["properties"].size() == info->Fields.size();
        }
        CHECK(everyField && described, "every field has its default and description, every component its description");

        const Json& sprite = schema["definitions"]["component_SpriteRenderer"]["properties"];
        const Json& camera = schema["definitions"]["component_Camera"]["properties"];
        CHECK(schema["definitions"]["component_Transform"]["properties"]["position"] == Json::parse(R"({"type": "array", "items": {"type": "number"}, "minItems": 2, "maxItems": 2, "description": "World position [x, y], in world units.", "default": [0.0, 0.0]})"), "a vec2 is an array of exactly two numbers");
        CHECK(sprite["cell"]["items"] == Json::parse(R"({"type": "integer", "minimum": 0.0})") && sprite["tint"]["minItems"] == 4 && sprite["tint"]["items"]["maximum"] == 1.0, "an ivec2 holds integers, a vec4 four numbers, and ranges apply to each element");
        CHECK(camera["zoom"]["type"] == "number" && camera["zoom"]["minimum"] == 0.01 && camera["zoom"]["maximum"] == 100.0 && camera["primary"]["type"] == "boolean", "numbers and bools carry their range and type");
        CHECK(sprite["texture"]["type"] == "string" && sprite["texture"].contains("pattern") && !sprite["cell"].contains("pattern"), "only asset paths have the path pattern");
        CHECK(!schema["definitions"]["component_Camera"].contains("required"), "a component with no required fields has no required list");

        // a client component shows up, with its required fields
        registry.Register<Door>("Door", "Leads somewhere.")
            .Field(&Door::Target, "target", "Name of the entity to go to.").Required()
            .Field(&Door::Level, "level", "Key level needed.").Range(1, 5);
        Json withDoor = GenerateSceneSchema(registry);
        CHECK(withDoor["definitions"]["component_Door"]["required"] == Json::array({ "target" }) && withDoor["definitions"]["component_Door"]["properties"]["level"]["maximum"] == 5.0 && withDoor["definitions"]["entity"]["properties"]["components"]["properties"].contains("Door"), "a registered client component is in the schema");

        // the committed schema is the generated one
        ComponentRegistry builtins;
        RegisterBuiltinComponents(builtins);
        if (UpdateGolden()) WriteFile("Assets/Scenes/scene.schema.json", SceneSchemaToText(builtins));
        CHECK(ReadFile("Assets/Scenes/scene.schema.json") == SceneSchemaToText(builtins), "Assets/Scenes/scene.schema.json is up to date (MARLE_UPDATE_GOLDEN=1 regenerates it)");
    }

    // scene schema: validating scene files
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        const Json schema = GenerateSceneSchema(registry);
        auto errorsFor = [&](const std::string& json) { return SchemaErrors(schema, ParseWithComments(json)); };
        auto entityWith = [&](const std::string& body) { return R"({"version": 1, "name": "x", "entities": [)" + body + "]}"; };
        auto componentWith = [&](const std::string& component) { return entityWith(R"({"name": "e", "components": {)" + component + "}}"); };
        auto mentions = [](const std::vector<std::string>& errors, const std::string& text) {
            for (const std::string& e : errors) if (e.find(text) != std::string::npos) return true;
            return false;
        };

        // valid files
        CHECK(errorsFor(ReadFile("Assets/Scenes/example.json")).empty(), "the example scene validates against the schema");
        CHECK(errorsFor(ReadFile("Tests/SceneTests/fixtures/canonical.json")).empty(), "the canonical fixture validates");
        CHECK(errorsFor(ReadFile("Assets/Scenes/scene.schema.json")).size() > 0, "the schema file is not itself a scene");
        CHECK(errorsFor(R"({"version": 1, "name": "x", "entities": []})").empty() && errorsFor(R"({"$schema": "scene.schema.json", "version": 1, "name": "x", "entities": [{"name": "a"}]})").empty(), "an empty scene, $schema and an entity without components validate");

        Scene written("Written");
        written.CreateEntity("n").Add<Transform>() = Transform{ { 16777216.0f, -3.0f }, 1e-7f, { 3e38f, 0.1f } };
        written.FindEntity("n").Add<Camera>().Zoom = 0.33333334f;
        written.CreateEntity("t").Add<SpriteRenderer>().Texture = "Assets/ñandú.png";
        written.CreateEntity(std::string(64, 'a'));
        CHECK(errorsFor(SaveSceneToText(written, registry).Text).empty(), "what the writer produces validates");

        // what it rejects, and where it says so
        CHECK(!errorsFor(R"({"version": 2, "name": "x", "entities": []})").empty() && !errorsFor(R"({"name": "x", "entities": []})").empty() && !errorsFor(R"({"version": 1, "entities": []})").empty() && !errorsFor(R"({"version": 1, "name": "", "entities": []})").empty() && !errorsFor(R"({"version": 1, "name": "x"})").empty(), "a wrong or missing version, name or entities is rejected");
        CHECK(!errorsFor(R"({"version": 1, "name": "x", "entities": [], "extra": 1})").empty() && !errorsFor(R"([])").empty() && !errorsFor(entityWith(R"({"name": "a", "colour": "red"})")).empty() && !errorsFor(entityWith(R"({"components": {}})")).empty() && !errorsFor(entityWith(R"("a")")).empty(), "unknown keys, a wrong top level, a nameless entity and a non-object entity are rejected");
        CHECK(!errorsFor(componentWith(R"("Transfrom": {})")).empty() && !errorsFor(componentWith(R"("Transform": [])")).empty() && !errorsFor(entityWith(R"({"name": "e", "components": []})")).empty(), "an unknown component and a component or components that is not an object are rejected");
        CHECK(mentions(errorsFor(componentWith(R"("Camera": {"zoom": 0})")), "/entities/0/components/Camera/zoom"), "an out-of-range value is rejected, with its pointer");
        CHECK(!errorsFor(componentWith(R"("Camera": {"zoom": 101})")).empty() && errorsFor(componentWith(R"("Camera": {"zoom": 0.01})")).empty() && errorsFor(componentWith(R"("Camera": {"zoom": 100})")).empty(), "the range ends are inclusive");
        CHECK(!errorsFor(componentWith(R"("Camera": {"primary": "yes"})")).empty() && !errorsFor(componentWith(R"("Camera": {"zom": 1})")).empty() && !errorsFor(componentWith(R"("SpriteRenderer": {"texture": 5})")).empty(), "a wrong type and an unknown field are rejected");
        CHECK(!errorsFor(componentWith(R"("Transform": {"position": [1]})")).empty() && !errorsFor(componentWith(R"("Transform": {"position": [1, 2, 3]})")).empty() && !errorsFor(componentWith(R"("Transform": {"position": [1, "x"]})")).empty() && errorsFor(componentWith(R"("Transform": {"position": [1, 2]})")).empty(), "a vector must be exactly the right length of numbers");
        CHECK(!errorsFor(componentWith(R"("SpriteRenderer": {"cell": [1.5, 0]})")).empty() && !errorsFor(componentWith(R"("SpriteRenderer": {"cell": [-1, 0]})")).empty() && !errorsFor(componentWith(R"("SpriteRenderer": {"tint": [2, 0, 0, 1]})")).empty() && errorsFor(componentWith(R"("SpriteRenderer": {"cell": [3, 0], "tint": [1, 0, 0, 0.5]})")).empty(), "integers and ranges apply to each element of a vector");

        // entity names
        auto nameErrors = [&](const std::string& name) { return errorsFor(entityWith(R"({"name": )" + Json(name).dump() + "}")); };
        std::string accented64, accented65;
        for (int i = 0; i < 64; i++) accented64 += "\xC3\xA9";
        accented65 = accented64 + "\xC3\xA9";
        CHECK(nameErrors("player").empty() && nameErrors("Tile 3-b.x").empty() && nameErrors("a").empty() && nameErrors(std::string(64, 'a')).empty() && nameErrors(accented64).empty(), "ordinary names validate, up to 64 characters");
        CHECK(!nameErrors("").empty() && !nameErrors(std::string(65, 'a')).empty() && !nameErrors(accented65).empty() && !nameErrors("a/b").empty() && !nameErrors(" a").empty() && !nameErrors("a ").empty() && !nameErrors("a\tb").empty() && !nameErrors("\ta").empty() && !nameErrors("a\n").empty(), "empty, too long, slash, edge-whitespace and control-character names are rejected");
        for (const char* name : { "player", "a", "a b", "a/b", " a", "a ", "", "a\tb", "x.y-z_1" })
            CHECK(nameErrors(name).empty() == IsValidEntityName(name), (std::string("the schema and the loader agree about the name '") + name + "'").c_str());

        // asset paths
        auto pathErrors = [&](const std::string& path) { return errorsFor(componentWith(R"("SpriteRenderer": {"texture": )" + Json(path).dump() + "}")); };
        CHECK(pathErrors("").empty() && pathErrors("Assets/Textures/a.png").empty() && pathErrors("Assets/a..b.png").empty() && pathErrors("a.png").empty() && pathErrors("dir/.hidden").empty(), "relative asset paths validate");
        CHECK(!pathErrors("../x.png").empty() && !pathErrors("a/../b.png").empty() && !pathErrors("a/..").empty() && !pathErrors("..").empty() && !pathErrors("/etc/passwd").empty() && !pathErrors("C:/x.png").empty() && !pathErrors("a\\b.png").empty(), "paths that leave the working directory are rejected");
        for (const char* path : { "", "a.png", "A/b/c.png", "a..b", "..", "../x", "a/../b", "a/..", "/x", "C:/x", "a\\b", "..x", "x..", ".../x" })
            CHECK(pathErrors(path).empty() == AssetPathProblem(path).empty(), (std::string("the schema and the loader agree about the path '") + path + "'").c_str());

        // required fields of a client component
        registry.Register<Door>("Door", "Leads somewhere.").Field(&Door::Target, "target", "Where to.").Required().Field(&Door::Level, "level", "Key level.").Range(1, 5);
        const Json withDoor = GenerateSceneSchema(registry);
        CHECK(!SchemaErrors(withDoor, ParseWithComments(componentWith(R"("Door": {"level": 2})"))).empty() && SchemaErrors(withDoor, ParseWithComments(componentWith(R"("Door": {"target": "cellar"})"))).empty() && !SchemaErrors(withDoor, ParseWithComments(componentWith(R"("Door": {"target": "c", "level": 9})"))).empty(), "a required field must be present");

        // the schema agrees with the loader on every fixture file
        // (it cannot see duplicate entity names, missing asset files or keys repeated within an object)
        const std::string dir = "Tests/SceneTests/fixtures/";
        bool agree = true;
        for (const char* name : { "canonical.json", "future_version.json", "missing_version.json", "version_not_integer.json", "top_level_array.json", "entities_not_array.json", "top_level_problems.json" }) {
            bool schemaValid = SchemaErrors(schema, ParseWithComments(ReadFile(dir + name))).empty();
            bool loaderValid = LoadScene(dir + name, registry).Ok();
            if (schemaValid != loaderValid) { printf("      schema and loader disagree about %s\n", name); agree = false; }
        }
        CHECK(agree, "the schema and the loader agree about every fixture file");
        CHECK(SchemaErrors(schema, ParseWithComments(ReadFile(dir + "repeated_top_key.json"))).empty() && !LoadScene(dir + "repeated_top_key.json", registry).Ok(), "a repeated key is the one thing only the loader can see");

        // and about every entity in the file full of mistakes
        Json mixed = ParseWithComments(ReadFile(dir + "mixed_errors.json"));
        std::set<int> schemaBad, loaderBad;
        for (const std::string& e : SchemaErrors(schema, mixed)) if (EntityIndexOf(e) >= 0) schemaBad.insert(EntityIndexOf(e));
        for (const SceneProblem& p : LoadScene(dir + "mixed_errors.json", registry).Problems) if (EntityIndexOf(p.Path) >= 0) loaderBad.insert(EntityIndexOf(p.Path));
        for (int onlyLoader : { 4, 7, 9 }) loaderBad.erase(onlyLoader);   // duplicate name, missing file, repeated key
        std::string schemaList, loaderList;
        for (int i : schemaBad) schemaList += std::to_string(i) + " ";
        for (int i : loaderBad) loaderList += std::to_string(i) + " ";
        printf("      entities the schema rejects: %s\n      entities the loader reports (less the three it alone can see): %s\n", schemaList.c_str(), loaderList.c_str());
        CHECK(schemaBad == loaderBad && !schemaBad.empty(), "the schema rejects exactly the entities in the mixed file that the loader reports");
    }

    // marle-scene: validate
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        const std::string dir = "Tests/SceneTests/fixtures/";
        const std::filesystem::path work = std::filesystem::temp_directory_path() / "marle_scene_tool_test";
        std::filesystem::remove_all(work);
        std::filesystem::create_directories(work / "sub");

        ToolRun ok = RunTool(registry, { "validate", "Assets/Scenes/example.json" });
        CHECK(ok.Code == 0 && ok.Out == "OK    Assets/Scenes/example.json: 8 entities, 15 components\n" && ok.Err.empty(), "validate says OK for a good file, with counts");

        ToolRun bad = RunTool(registry, { "validate", dir + "mixed_errors.json" });
        CHECK(bad.Code == 1 && Contains(bad.Out, dir + "mixed_errors.json /entities/10/components/Camera/zoom: must be at least 0.01, got 0.0; the component is skipped\n"), "validate prints every problem as file, path and message, and exits 1");
        CHECK(Contains(bad.Out, "FAIL  " + dir + "mixed_errors.json: 14 problems (loaded 10 entities, 7 components)\n") && bad.Err.empty(), "validate ends a bad file with a summary of what loaded");

        ToolRun missing = RunTool(registry, { "validate", dir + "nope.json" });
        CHECK(missing.Code == 1 && Contains(missing.Out, "cannot open the file") && Contains(missing.Out, "FAIL  " + dir + "nope.json: 1 problem (nothing could be loaded)"), "validate reports a missing file");
        CHECK(Contains(RunTool(registry, { "validate", dir + "bad_syntax.json" }).Out, "parse error at line 5"), "validate reports where a syntax error is");

        ToolRun two = RunTool(registry, { "validate", "Assets/Scenes/example.json", dir + "future_version.json" });
        CHECK(two.Code == 1 && Contains(two.Out, "OK    Assets/Scenes/example.json") && Contains(two.Out, "needs a newer engine") && Contains(two.Out, "2 files, 1 with problems\n"), "validate checks several files and counts the ones with problems");

        WriteFile((work / "one.json").string(), R"({"version": 1, "name": "t", "entities": [{"name": "a", "components": {"Camera": {}}}]})");
        CHECK(RunTool(registry, { "validate", (work / "one.json").generic_string() }).Out == "OK    " + (work / "one.json").generic_string() + ": 1 entity, 1 component\n", "counts say '1 entity, 1 component'");

        // directories
        ToolRun scenes = RunTool(registry, { "validate", "Assets/Scenes" });
        CHECK(scenes.Code == 0 && scenes.Out == ok.Out + "OK    Assets/Scenes/world.json: 8 entities, 16 components\n2 files, 0 with problems\n", "a directory means its scene files, and the schema file is not one of them");
        WriteFile((work / "b.json").string(), R"({"version": 1, "name": "b", "entities": []})");
        WriteFile((work / "sub" / "a.json").string(), R"({"version": 1, "name": "a", "entities": []})");
        WriteFile((work / "notes.txt").string(), "not a scene");
        WriteFile((work / "x.schema.json").string(), "{ not even json");
        std::string w = work.generic_string();
        ToolRun tree = RunTool(registry, { "validate", w + "/b.json", w + "/sub" });
        CHECK(tree.Code == 0 && tree.Out.find("b.json") < tree.Out.find("sub/a.json") && Contains(tree.Out, "2 files, 0 with problems"), "files are checked in the order given, a directory's in sorted order");
        ToolRun all = RunTool(registry, { "validate", w });
        CHECK(all.Code == 0 && Contains(all.Out, "3 files, 0 with problems") && Contains(all.Out, "one.json") && Contains(all.Out, "sub/a.json") && !Contains(all.Out, "notes.txt") && !Contains(all.Out, "x.schema.json"), "a directory is searched in subdirectories, and only .json files other than *.schema.json count");
        std::filesystem::create_directories(work / "empty");
        ToolRun none = RunTool(registry, { "validate", w + "/empty" });
        CHECK(none.Code == 0 && none.Out == "no scene files found\n", "an empty directory is not an error");

        // the command line
        ToolRun noFiles = RunTool(registry, { "validate" });
        CHECK(noFiles.Code == 2 && Contains(noFiles.Err, "no files given") && Contains(noFiles.Err, "usage: marle-scene") && noFiles.Out.empty(), "validate without files is a usage error on stderr");
        ToolRun unknown = RunTool(registry, { "frobnicate" });
        CHECK(unknown.Code == 2 && Contains(unknown.Err, "unknown command 'frobnicate'"), "an unknown command is a usage error");
        CHECK(RunTool(registry, {}).Code == 2 && !RunTool(registry, {}).Err.empty(), "no command is a usage error");
        ToolRun help = RunTool(registry, { "help" });
        CHECK(help.Code == 0 && Contains(help.Out, "validate") && Contains(help.Out, "format") && Contains(help.Out, "schema") && RunTool(registry, { "--help" }).Out == help.Out, "help lists the commands");

        std::filesystem::remove_all(work);
    }

    // marle-scene: format
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        const std::string dir = "Tests/SceneTests/fixtures/";
        const std::filesystem::path work = std::filesystem::temp_directory_path() / "marle_scene_tool_format_test";
        std::filesystem::remove_all(work);
        std::filesystem::create_directories(work);
        const std::string w = work.generic_string();
        const std::string compact = R"({"version":1,"name":"t","entities":[{"name":"a","components":{"Camera":{"zoom":2}}}]})";
        auto canonicalOf = [&](const std::string& text, const std::string& schema = "") {
            SceneWriteOptions options;
            options.Schema = schema;
            return SaveSceneToText(*LoadSceneFromText(text, "x", registry).Scene, registry, options).Text;
        };

        // a file that is not canonical is rewritten, once
        WriteFile(w + "/a.json", compact);
        ToolRun run = RunTool(registry, { "format", w + "/a.json" });
        CHECK(run.Code == 0 && run.Out == "DONE  " + w + "/a.json: reformatted\n" && ReadFile(w + "/a.json") == canonicalOf(compact), "format rewrites a file in canonical form");
        ToolRun again = RunTool(registry, { "format", w + "/a.json" });
        CHECK(again.Code == 0 && again.Out == "OK    " + w + "/a.json: already canonical\n", "formatting again changes nothing");
        CHECK(RunTool(registry, { "format", dir + "canonical.json" }).Out == "OK    " + dir + "canonical.json: already canonical\n", "the canonical fixture is already canonical");

        // --check reports but never writes
        WriteFile(w + "/c.json", compact);
        ToolRun check = RunTool(registry, { "format", "--check", w + "/c.json" });
        CHECK(check.Code == 1 && check.Out == "DIFF  " + w + "/c.json: would be reformatted\n" && ReadFile(w + "/c.json") == compact, "--check reports a file that would change and leaves it alone");
        CHECK(RunTool(registry, { "format", "--check", w + "/a.json" }).Code == 0, "--check passes for a canonical file");

        // $schema
        WriteFile(w + "/s.json", R"({"$schema":"../scene.schema.json","version":1,"name":"t","entities":[]})");
        RunTool(registry, { "format", w + "/s.json" });
        CHECK(ReadFile(w + "/s.json") == canonicalOf(R"({"version":1,"name":"t","entities":[]})", "../scene.schema.json") && Contains(ReadFile(w + "/s.json"), "\"$schema\": \"../scene.schema.json\""), "an existing $schema key is kept");
        WriteFile(w + "/n.json", compact);
        RunTool(registry, { "format", "--schema", "my.schema.json", w + "/n.json" });
        CHECK(ReadFile(w + "/n.json") == canonicalOf(compact, "my.schema.json"), "--schema adds a $schema key");

        // comments
        std::filesystem::copy_file("Assets/Scenes/example.json", w + "/commented.json");
        std::string commented = ReadFile(w + "/commented.json");
        ToolRun keep = RunTool(registry, { "format", w + "/commented.json" });
        CHECK(keep.Code == 0 && Contains(keep.Out, "KEEP  " + w + "/commented.json: has comments") && ReadFile(w + "/commented.json") == commented, "a file with comments is left alone");
        CHECK(RunTool(registry, { "format", "--check", w + "/commented.json" }).Code == 0, "--check does not count a file with comments as needing a change");
        ToolRun drop = RunTool(registry, { "format", "--drop-comments", w + "/commented.json" });
        std::string dropped = ReadFile(w + "/commented.json");
        CHECK(drop.Code == 0 && Contains(drop.Out, "DONE") && !Contains(dropped, "//") && Contains(dropped, "\"$schema\": \"scene.schema.json\"") && LoadScene(w + "/commented.json", registry).Scene->GetEntityCount() == 8, "--drop-comments formats it anyway, keeping $schema and every entity");

        // a file with problems is never rewritten
        std::filesystem::copy_file(dir + "mixed_errors.json", w + "/mixed.json");
        std::string mixed = ReadFile(w + "/mixed.json");
        ToolRun skip = RunTool(registry, { "format", "--drop-comments", w + "/mixed.json" });
        CHECK(skip.Code == 1 && Contains(skip.Out, "SKIP  " + w + "/mixed.json: 14 problems; fix them first") && Contains(skip.Out, "/entities/10/components/Camera/zoom") && ReadFile(w + "/mixed.json") == mixed, "a file with problems is reported and left exactly as it was");

        // directories, and the command line
        std::filesystem::create_directories(work / "dir");
        WriteFile(w + "/dir/one.json", compact);
        WriteFile(w + "/dir/two.json", compact);
        ToolRun folder = RunTool(registry, { "format", w + "/dir" });
        CHECK(folder.Code == 0 && Contains(folder.Out, "2 files, 2 changed, 0 skipped") && ReadFile(w + "/dir/two.json") == canonicalOf(compact), "format handles every file in a directory");
        CHECK(RunTool(registry, { "format" }).Code == 2 && RunTool(registry, { "format", "--bogus", "x" }).Code == 2 && RunTool(registry, { "format", "--schema" }).Code == 2, "format without files, with an unknown option or with an incomplete one is a usage error");
        ToolRun gone = RunTool(registry, { "format", w + "/nope.json" });
        CHECK(gone.Code == 1 && Contains(gone.Out, "cannot open the file"), "a missing file is a failure");

        std::filesystem::remove_all(work);
    }

    // marle-scene: schema
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        const std::filesystem::path work = std::filesystem::temp_directory_path() / "marle_scene_tool_schema_test";
        std::filesystem::remove_all(work);
        std::filesystem::create_directories(work);
        const std::string file = (work / "schema.json").generic_string();

        ToolRun print = RunTool(registry, { "schema" });
        CHECK(print.Code == 0 && print.Out == SceneSchemaToText(registry) && print.Err.empty(), "schema prints the generated schema");
        ToolRun write = RunTool(registry, { "schema", "--output", file });
        CHECK(write.Code == 0 && write.Out == "DONE  " + file + ": schema written\n" && ReadFile(file) == print.Out, "schema --output writes it to a file");
        CHECK(RunTool(registry, { "schema", "--check", file }).Out == "OK    " + file + ": up to date\n" && RunTool(registry, { "schema", "--check", file }).Code == 0, "schema --check passes for an up to date file");
        CHECK(RunTool(registry, { "schema", "--check", "Assets/Scenes/scene.schema.json" }).Code == 0, "the committed schema is up to date");

        WriteFile(file, print.Out + " ");
        ToolRun stale = RunTool(registry, { "schema", "--check", file });
        CHECK(stale.Code == 1 && Contains(stale.Out, "DIFF  " + file + ": out of date") && Contains(stale.Out, "marle-scene schema --output " + file), "schema --check fails for a stale file and says how to fix it");
        CHECK(RunTool(registry, { "schema", "--check", (work / "missing.json").generic_string() }).Code == 1, "schema --check fails for a missing file");
        CHECK(RunTool(registry, { "schema", "--output" }).Code == 2 && RunTool(registry, { "schema", "--frob", "x" }).Code == 2 && RunTool(registry, { "schema", "extra" }).Code == 2, "a bad schema command line is a usage error");
        CHECK(RunTool(registry, { "schema", "--output", (work / "no_such_dir" / "s.json").generic_string() }).Code == 1, "an unwritable schema path is a failure");

        // a game with components of its own gets them in its schema and validation
        ComponentRegistry game;
        RegisterBuiltinComponents(game);
        game.Register<Door>("Door", "Leads somewhere.").Field(&Door::Target, "target", "Where to.").Required();
        CHECK(Contains(RunTool(game, { "schema" }).Out, "component_Door") && !Contains(print.Out, "component_Door"), "the tool uses whatever registry it is given");
        WriteFile((work / "door.json").string(), R"({"version": 1, "name": "d", "entities": [{"name": "d", "components": {"Door": {"target": "x"}}}]})");
        CHECK(RunTool(game, { "validate", (work / "door.json").generic_string() }).Code == 0 && RunTool(registry, { "validate", (work / "door.json").generic_string() }).Code == 1, "a scene with a client component validates only with that component registered");

        std::filesystem::remove_all(work);
    }

    // marle-scene: the real executable
    {
        std::filesystem::path self(argv[0]);
        std::string tool = (self.parent_path().parent_path() / "SceneTool" / "marle-scene").string();
        CHECK(std::filesystem::exists(tool), "the marle-scene executable is built next to the tests");

        ToolRun good = RunProcess(tool + " validate Assets/Scenes/example.json");
        CHECK(good.Code == 0 && good.Out == "OK    Assets/Scenes/example.json: 8 entities, 15 components\n", "marle-scene validate succeeds on a good scene");
        ToolRun broken = RunProcess(tool + " validate Tests/SceneTests/fixtures/mixed_errors.json");
        CHECK(broken.Code == 1 && Contains(broken.Out, "FAIL  Tests/SceneTests/fixtures/mixed_errors.json: 14 problems"), "marle-scene validate exits 1 on a bad scene");
        CHECK(RunProcess(tool + " bogus").Code == 2 && RunProcess(tool).Code == 2, "marle-scene exits 2 on a bad command line");
        ToolRun schema = RunProcess(tool + " schema");
        CHECK(schema.Code == 0 && schema.Out == ReadFile("Assets/Scenes/scene.schema.json"), "marle-scene schema prints the committed schema");
        CHECK(RunProcess(tool + " format --check Assets/Scenes").Code == 0 && RunProcess(tool + " schema --check Assets/Scenes/scene.schema.json").Code == 0, "marle-scene format --check and schema --check pass on the repository's scenes");
    }

    // tilemap component
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        Scene scene;
        const std::string sheet = "Assets/Textures/test_sheet.tga";

        CHECK(TilemapCell('0') == 0 && TilemapCell('9') == 9 && TilemapCell('a') == 10 && TilemapCell('z') == 35 && TilemapCell('.') == -1, "tile characters are base 36 digits, '.' is no tile");
        CHECK(TilemapCell('A') == -2 && TilemapCell(' ') == -2 && TilemapCell('-') == -2 && TilemapCell('#') == -2 && TilemapCell('\t') == -2, "anything else is not a tile character");

        Entity map = scene.CreateEntity("map");
        auto errors = registry.Read(map, "Tilemap", Json::parse(R"({"texture": "Assets/Textures/test_sheet.tga", "cellSize": [32, 32], "tileSize": [64, 64], "rows": ["01.f", "a..z"]})"));
        const Tilemap* t = map.TryGet<Tilemap>();
        CHECK(errors.empty() && t && t->Texture == sheet && t->Rows.size() == 2 && t->Rows[0] == "01.f" && t->Rows[1] == "a..z" && t->TileSize == glm::vec2(64.0f, 64.0f), "a tilemap reads its sheet, sizes and rows");

        Entity defaults = scene.CreateEntity("defaults");
        CHECK(registry.Read(defaults, "Tilemap", Json::object()).empty() && defaults.Get<Tilemap>().Rows.empty() && defaults.Get<Tilemap>().Texture.empty() && defaults.Get<Tilemap>().CellSize == glm::vec2(32.0f, 32.0f) && defaults.Get<Tilemap>().TileSize == glm::vec2(64.0f, 64.0f), "an empty tilemap has no rows and sensible sizes");
        Entity emptyRows = scene.CreateEntity("emptyRows");
        CHECK(registry.Read(emptyRows, "Tilemap", Json::parse(R"({"rows": []})")).empty() && registry.Read(scene.CreateEntity("blankRow"), "Tilemap", Json::parse(R"({"rows": [""]})")).empty(), "no rows, or a row with no tiles, is fine");

        Entity bad = scene.CreateEntity("bad");
        errors = registry.Read(bad, "Tilemap", Json::parse(R"({"rows": "0123"})"));
        CHECK(errors.size() == 1 && HasError(errors, "/rows", "expected an array of strings, got a string"), "rows must be an array");
        errors = registry.Read(bad, "Tilemap", Json::parse(R"({"rows": ["01", 5, null]})"));
        CHECK(errors.size() == 2 && HasError(errors, "/rows/1", "expected a string, got a number") && HasError(errors, "/rows/2", "expected a string, got null"), "every row that is not a string is reported");
        errors = registry.Read(bad, "Tilemap", Json::parse(R"({"rows": ["abc", "ab", "abcd"]})"));
        CHECK(errors.size() == 2 && HasError(errors, "/rows/1", "this row has 2 tiles but the first row has 3") && HasError(errors, "/rows/2", "has 4 tiles"), "rows of different lengths are reported against the first row");
        errors = registry.Read(bad, "Tilemap", Json::parse(R"({"rows": ["ab", "aB", "a ", "a-"]})"));
        CHECK(errors.size() == 3 && HasError(errors, "/rows/1", "'B' at position 1 is not a tile character") && HasError(errors, "/rows/2", "' ' at position 1") && HasError(errors, "/rows/3", "'-' at position 1"), "a character that is not a tile is reported with its position");
        CHECK(!bad.Has<Tilemap>(), "a tilemap with any problem is not added");

        errors = registry.Read(bad, "Tilemap", Json::parse(R"({"cellSize": [0, 32], "tileSize": [64, 0], "rows": ["a", "bc"]})"));
        CHECK(HasError(errors, "/cellSize/0", "must be at least 1") && HasError(errors, "/tileSize/1", "must be at least 0.001") && errors.size() == 2, "sizes must be positive, and the rows are only checked once the fields themselves are fine");

        // writing
        Json written = registry.Write(map, "Tilemap");
        CHECK(written["rows"] == Json::array({ "01.f", "a..z" }) && written["tileSize"] == Json::array({ 64.0, 64.0 }), "a tilemap writes its rows as an array of strings");
        CHECK(registry.Write(defaults, "Tilemap", true) == Json::object(), "an all-default tilemap writes as an empty object with skipDefaults");
        CHECK(registry.Find("Tilemap")->Fields[3].Type == FieldType::StringArray && std::string(FieldTypeName(FieldType::StringArray)) == "string[]" && registry.Find("Tilemap")->Fields[0].AssetPath, "rows is a string[] field and the sheet is an asset path");

        // the writer lays a component with a list of strings out as a block, one string per line
        Scene ground("Ground");
        Entity g = ground.CreateEntity("ground");
        g.Add<Transform>();
        Tilemap& tm = g.Add<Tilemap>();
        tm.Texture = sheet;
        tm.Rows = { "01", "2." };
        ground.CreateEntity("flat").Add<Tilemap>();
        const std::string golden = R"({
  "version": 1,
  "name": "Ground",
  "entities": [
    {
      "name": "ground",
      "components": {
        "Transform": { "position": [0, 0], "rotation": 0, "scale": [1, 1] },
        "Tilemap": {
          "texture": "Assets/Textures/test_sheet.tga",
          "cellSize": [32, 32],
          "tileSize": [64, 64],
          "rows": [
            "01",
            "2."
          ]
        }
      }
    },
    {
      "name": "flat",
      "components": {
        "Tilemap": {
          "texture": "",
          "cellSize": [32, 32],
          "tileSize": [64, 64],
          "rows": []
        }
      }
    }
  ]
}
)";
        SceneSaveResult saved = SaveSceneToText(ground, registry);
        for (const std::string& problem : saved.Problems) printf("      %s\n", problem.c_str());
        CHECK(saved.Ok() && saved.Text == golden, "a tilemap is written as a block with one row per line");

        SceneLoadResult back = LoadSceneFromText(golden, "golden", registry);
        CHECK(back.Ok() && SaveSceneToText(*back.Scene, registry).Text == golden && back.Scene->FindEntity("ground").Get<Tilemap>().Rows[1] == "2.", "the block form loads and writes back identically");
        back.Scene->FindEntity("ground").Get<Tilemap>().Rows[1] = "3.";
        std::vector<std::string> before = SplitLines(golden), after = SplitLines(SaveSceneToText(*back.Scene, registry).Text);
        int changed = 0;
        for (size_t i = 0; i < before.size() && i < after.size(); i++) changed += before[i] != after[i];
        CHECK(before.size() == after.size() && changed == 1, "changing one row changes exactly one line");

        // a tilemap that would not read back is refused
        Scene broken("Broken");
        broken.CreateEntity("m").Add<Tilemap>().Rows = { "ab", "a" };
        SceneSaveResult refused = SaveSceneToText(broken, registry);
        CHECK(!refused.Ok() && refused.Problems[0].find("entity 'm' component 'Tilemap'/rows/1: this row has 1 tiles") != std::string::npos, "a tilemap with rows of different lengths is refused by the writer");
        Scene unsafe("Unsafe");
        unsafe.CreateEntity("m").Add<Tilemap>().Texture = "../sheet.png";
        CHECK(!SaveSceneToText(unsafe, registry).Ok(), "an unsafe sheet path is refused by the writer");

        // the loader reports where, and the schema accepts a good tilemap
        SceneLoadResult load = LoadSceneFromText(R"({"version": 1, "name": "x", "entities": [{"name": "m", "components": {"Tilemap": {"rows": ["ab", "a"]}}}]})", "inline", registry);
        CHECK(HasProblem(load, "/entities/0/components/Tilemap/rows/1", "this row has 1 tiles") && !load.Scene->FindEntity("m").Has<Tilemap>(), "the loader reports a bad tilemap with its path and skips the component");
        const Json schema = GenerateSceneSchema(registry);
        CHECK(SchemaErrors(schema, Json::parse(golden)).empty(), "the schema accepts a good tilemap");
        CHECK(!SchemaErrors(schema, Json::parse(R"({"version": 1, "name": "x", "entities": [{"name": "m", "components": {"Tilemap": {"rows": "ab"}}}]})")).empty() && !SchemaErrors(schema, Json::parse(R"({"version": 1, "name": "x", "entities": [{"name": "m", "components": {"Tilemap": {"rows": ["ab", 5]}}}]})")).empty() && !SchemaErrors(schema, Json::parse(R"({"version": 1, "name": "x", "entities": [{"name": "m", "components": {"Tilemap": {"cellSize": [0, 1]}}}]})")).empty(), "the schema rejects rows that are not strings, and a zero size");
        CHECK(schema["definitions"]["component_Tilemap"]["properties"]["rows"]["items"] == Json::parse(R"({"type": "string"})") && schema["definitions"]["component_Tilemap"]["properties"]["rows"]["type"] == "array", "rows is an array of strings in the schema");
    }

    // the Sandbox world
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        const std::string file = "Assets/Scenes/world.json";

        SceneLoadResult world = LoadScene(file, registry);
        for (const SceneProblem& problem : world.Problems) printf("      %s\n", problem.ToString().c_str());
        CHECK(world.Ok() && world.Scene->GetName() == "The Sunken Orangerie", "the Sandbox world loads with no problems");

        std::string names;
        for (Entity e : world.Scene->GetEntities()) names += e.GetName() + " ";
        CHECK(names == "camera world spinner_0 spinner_1 spinner_2 spinner_3 player satellite ", "its entities are the ones WorldLayer drives, in draw order");

        Entity camera = world.Scene->FindEntity("camera"), player = world.Scene->FindEntity("player"), map = world.Scene->FindEntity("world");
        CHECK(camera.Get<Camera>().Primary && camera.Get<Camera>().Zoom == 1.0f && camera.Get<Transform>().Position == player.Get<Transform>().Position && player.Get<Transform>().Position == glm::vec2(1024.0f, 518.0f), "the camera is primary and starts on the player");
        CHECK(player.Get<SpriteRenderer>().Texture == "Assets/Textures/test_sprite.tga" && player.Get<Transform>().Scale == glm::vec2(48.0f, 48.0f) && world.Scene->FindEntity("satellite").Get<SpriteRenderer>().Texture.empty(), "the player is a textured sprite and the satellite a flat color");
        CHECK(world.Scene->FindEntity("spinner_1").Get<Transform>().Position == glm::vec2(1024.0f, 938.0f) && world.Scene->FindEntity("spinner_3").Get<SpriteRenderer>().Cell == glm::ivec2(3, 3), "the spinners sit around the pond on sheet cell [3, 3]");

        const Tilemap& tiles = map.Get<Tilemap>();
        CHECK(tiles.Rows.size() == 24 && tiles.Rows[0].size() == 32 && tiles.TileSize == glm::vec2(64.0f, 64.0f) && tiles.CellSize == glm::vec2(32.0f, 32.0f) && map.Get<Transform>().Position == glm::vec2(0.0f), "the map is 32 x 24 tiles of 64 world units, from the origin");

        // it is the map the Sandbox used to generate in code (a hash of the tile position, a pond in the
        // middle, a border), row 0 of the file being the top row
        unsigned mismatches = 0;
        for (int y = 0; y < 24; y++)
            for (int x = 0; x < 32; x++) {
                unsigned h = (unsigned)(x * 73856093) ^ (unsigned)(y * 19349663);
                h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15;
                float dx = x - 32 * 0.5f + 0.5f, dy = y - 24 * 0.5f + 0.5f;
                int row, column = h % 4;
                if (dx * dx + dy * dy < 20.0f) row = 2;
                else if (x == 0 || y == 0 || x == 31 || y == 23) row = 3;
                else row = (h >> 8) % 4 == 0 ? 1 : 0;
                char expected = "0123456789abcdef"[row * 4 + column];
                mismatches += tiles.Rows[23 - y][x] != expected;
            }
        CHECK(mismatches == 0, "every tile is the one the hash used to give");

        CHECK(SaveSceneToText(*world.Scene, registry).Text == ReadFile(file), "the file is in canonical form");
        ComponentRegistry again;
        RegisterBuiltinComponents(again);
        CHECK(SchemaErrors(GenerateSceneSchema(again), ParseWithComments(ReadFile(file))).empty(), "and it validates against the schema");
        CHECK(RunTool(registry, { "format", "--check", file }).Code == 0 && RunTool(registry, { "validate", file }).Code == 0, "marle-scene validates it and finds it already formatted");
    }

    // scene reloader
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        const std::filesystem::path work = std::filesystem::temp_directory_path() / "marle_scene_reloader_test";
        std::filesystem::remove_all(work);
        std::filesystem::create_directories(work);
        const std::string file = (work / "scene.json").generic_string();

        // every write gets its own modification time, so the tests do not depend on timestamp granularity
        int tick = 0;
        auto Write = [&](const std::string& text) {
            WriteFile(file, text);
            std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now() + std::chrono::minutes(++tick));
        };
        const std::string one = R"({"version": 1, "name": "one", "entities": [{"name": "a", "components": {"Transform": {"position": [1, 2]}}}]})";
        const std::string two = R"({"version": 1, "name": "two", "entities": [{"name": "b"}, {"name": "c"}]})";
        const std::string three = R"({"version": 1, "name": "three", "entities": []})";

        Write(one);
        SceneReloader reloader(file, registry);
        SceneLoadResult first = reloader.Load();
        CHECK(first.Ok() && first.Scene->GetName() == "one" && first.Scene->FindEntity("a"), "the first load gives the scene");
        CHECK(reloader.GetStatus().File == file && reloader.GetStatus().Loads == 1 && reloader.GetStatus().Failures == 0 && reloader.GetStatus().LastOk && reloader.GetStatus().Problems.empty() && !reloader.GetStatus().Missing, "and the status says so");

        SceneReload idle = reloader.Poll();
        CHECK(!idle.Attempted && !idle.Scene && reloader.GetStatus().Loads == 1, "a file that has not changed is not loaded again");

        // a good edit is applied
        Write(two);
        SceneReload changed = reloader.Poll();
        CHECK(changed.Attempted && changed.Scene && changed.Scene->GetName() == "two" && changed.Scene->GetEntityCount() == 2 && changed.Scene->FindEntity("c") && !changed.Scene->FindEntity("a"), "an edited file gives the new scene");
        CHECK(reloader.GetStatus().Loads == 2 && reloader.GetStatus().LastOk && first.Scene->FindEntity("a"), "the old scene is untouched and the load is counted");
        CHECK(!reloader.Poll().Attempted, "and the same edit is not loaded twice");

        // strict: any problem at all refuses the reload, and the problems are kept
        Write("{ \"version\": 1, \"name\": \"x\", \"entities\": [ { \"name\": \"a\", }, ] }");
        SceneReload syntax = reloader.Poll();
        CHECK(syntax.Attempted && !syntax.Scene && !reloader.GetStatus().LastOk && reloader.GetStatus().Failures == 1 && reloader.GetStatus().Problems.size() == 1 && Contains(reloader.GetStatus().Problems[0].Message, "parse error"), "a syntax error is refused, with the reason in the status");
        CHECK(!reloader.Poll().Attempted && !reloader.GetStatus().LastOk && reloader.GetStatus().Problems.size() == 1 && reloader.GetStatus().Failures == 1, "a refused file is not retried until it changes, and its problems stay in the status");
        CHECK(reloader.GetStatus().KeptPrevious && !first.Scene->GetName().empty(), "the status says the scene from before is still the one running");

        Write(R"({"version": 1, "name": "x", "entities": [{"name": "a", "components": {"Camera": {"zom": 2}}}, {"name": "ok"}]})");
        SceneReload typo = reloader.Poll();
        CHECK(typo.Attempted && !typo.Scene && reloader.GetStatus().Failures == 2 && HasProblem(SceneLoadResult{ nullptr, reloader.GetStatus().Problems }, "/entities/0/components/Camera/zom", "did you mean 'zoom'"), "a mistyped field refuses the whole reload even though the rest would load");
        Write(R"({"version": 1, "name": "x", "entities": [{"name": "a", "components": {"SpriteRenderer": {"texture": "Assets/Textures/not_there_yet.png"}}}]})");
        SceneReload asset = reloader.Poll();
        CHECK(asset.Attempted && !asset.Scene && reloader.GetStatus().Failures == 3 && Contains(reloader.GetStatus().Problems[0].Message, "does not exist"), "a missing asset file refuses the reload too");

        // fixing the file is applied, and the problems clear
        Write(three);
        SceneReload fixed = reloader.Poll();
        CHECK(fixed.Attempted && fixed.Scene && fixed.Scene->GetName() == "three" && reloader.GetStatus().LastOk && reloader.GetStatus().Problems.empty() && reloader.GetStatus().Loads == 3 && reloader.GetStatus().Failures == 3 && !reloader.GetStatus().KeptPrevious, "a fixed file is applied, the problems are cleared and nothing is kept back");

        // a file that goes missing (an editor saving through a rename) is waited for, not a failure
        std::filesystem::remove(file);
        SceneReload gone = reloader.Poll();
        CHECK(!gone.Attempted && !gone.Scene && reloader.GetStatus().Missing && reloader.GetStatus().Failures == 3 && reloader.GetStatus().LastOk, "a missing file leaves things as they were and says it is missing");
        CHECK(!reloader.Poll().Attempted && reloader.GetStatus().Missing, "and keeps waiting");
        Write(one);
        SceneReload back = reloader.Poll();
        CHECK(back.Attempted && back.Scene && back.Scene->GetName() == "one" && !reloader.GetStatus().Missing, "when the file is back it is loaded");

        // forcing, and a file replaced the way the engine writes files
        SceneReload forced = reloader.Poll(true);
        CHECK(forced.Attempted && forced.Scene && forced.Scene->GetName() == "one", "a forced poll reloads a file that has not changed");
        WriteTextFile(file, two);
        std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now() + std::chrono::minutes(++tick));
        SceneReload replaced = reloader.Poll();
        CHECK(replaced.Attempted && replaced.Scene && replaced.Scene->GetName() == "two", "a file replaced through a temporary file and a rename is noticed");

        // a second reloader: the first load is lenient, and a file that is not there yet is picked up
        Write(R"({"version": 1, "name": "partial", "entities": [{"name": "a", "components": {"Camera": {"zom": 2}}}, {"name": "b"}]})");
        SceneReloader lenient(file, registry);
        SceneLoadResult partial = lenient.Load();
        CHECK(partial.Scene && !partial.Ok() && partial.Scene->FindEntity("b") && lenient.GetStatus().Loads == 1 && !lenient.GetStatus().LastOk && lenient.GetStatus().Problems.size() == 1 && !lenient.GetStatus().KeptPrevious, "the first load returns what loaded even with problems, and records them (nothing is being kept back)");
        Write(two);
        CHECK(lenient.Poll().Scene != nullptr && lenient.GetStatus().LastOk && lenient.GetStatus().Problems.empty(), "and the next valid edit is applied");

        std::filesystem::remove(file);
        SceneReloader absent(file, registry);
        SceneLoadResult none = absent.Load();
        CHECK(!none.Scene && !absent.GetStatus().LastOk && absent.GetStatus().Loads == 0 && Contains(absent.GetStatus().Problems[0].Message, "cannot open the file"), "loading a file that does not exist gives no scene and says why");
        CHECK(!absent.Poll().Attempted && absent.GetStatus().Missing, "it keeps waiting for the file");
        Write(one);
        SceneReload arrived = absent.Poll();
        CHECK(arrived.Attempted && arrived.Scene && arrived.Scene->GetName() == "one" && absent.GetStatus().Loads == 1 && !absent.GetStatus().Missing, "and loads it when it appears");

        std::filesystem::remove_all(work);
    }

    // scene-level components: the scene's own entity
    {
        Scene scene("s");
        Entity own = scene.GetSceneEntity();
        CHECK(own.IsValid() && own.GetName() == "(scene)" && scene.GetEntityCount() == 0 && scene.GetEntities().empty() && !scene.FindEntity("(scene)"), "a scene has its own entity, which is not one of its entities");
        own.Add<Health>().points = 3;
        Entity a = scene.CreateEntity("a");
        a.Add<Health>().points = 5;
        CHECK(scene.GetEntityCount() == 1 && scene.GetEntities().size() == 1 && scene.GetEntities()[0].GetName() == "a", "its components do not add to the entity list");
        int visited = 0, points = 0;
        scene.ForEach<Health>([&](Entity, Health& h) { visited++; points += h.points; });
        CHECK(visited == 1 && points == 5, "ForEach does not visit the scene's own entity");
        CHECK(!scene.DestroyEntity(own) && own.IsValid() && own.Get<Health>().points == 3, "the scene's own entity cannot be destroyed");
        CHECK(scene.GetSceneEntity() == own && scene.GetSceneEntity() != a, "it is the same entity every time");
        Scene other("o");
        CHECK(other.GetSceneEntity() != own && !other.GetSceneEntity().Has<Health>() && !other.DestroyEntity(own), "each scene has its own, and one scene cannot touch another's");
        Entity survivor = Scene("gone").GetSceneEntity();
        CHECK(!survivor.IsValid() && !survivor.Has<Health>(), "its handle goes invalid with the scene");
    }

    // scene-level components: loading
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        registry.Register<Gravity>("Gravity", "Pull.").Field(&Gravity::Strength, "strength", "How hard.").Range(0, 100);
        registry.Register<Door>("Door", "Leads somewhere.").Field(&Door::Target, "target", "Where to.").Required();
        auto load = [&](const std::string& components, const std::string& entities = "[]") {
            return LoadSceneFromText(R"({"version": 1, "name": "s", "components": )" + components + R"(, "entities": )" + entities + "}", "inline", registry);
        };

        SceneLoadResult ok = load(R"({"Gravity": {"strength": 3}})", R"([{"name": "a", "components": {"Gravity": {"strength": 5}}}])");
        CHECK(ok.Ok() && ok.Scene->GetSceneEntity().Get<Gravity>().Strength == 3.0f && ok.Scene->FindEntity("a").Get<Gravity>().Strength == 5.0f && ok.Scene->GetEntityCount() == 1, "a scene's own components load, beside an entity with the same component");
        SceneLoadResult none = LoadSceneFromText(R"({"version": 1, "name": "s", "entities": []})", "inline", registry);
        CHECK(none.Ok() && registry.GetComponentNames(none.Scene->GetSceneEntity()).empty(), "a scene without components has none of its own");
        CHECK(load("{}").Ok(), "an empty components object is fine");

        SceneLoadResult notObject = load(R"([])", R"([{"name": "a"}])");
        CHECK(HasProblem(notObject, "/components", "expected an object, got an array") && notObject.Scene->FindEntity("a"), "components that is not an object is reported and the entities still load");

        SceneLoadResult unknown = load(R"({"Gravty": {}, "Door": {"target": "x"}})", R"([{"name": "a"}])");
        CHECK(HasProblem(unknown, "/components/Gravty", "did you mean 'Gravity'") && unknown.Scene->GetSceneEntity().Has<Door>() && unknown.Scene->FindEntity("a"), "an unknown scene component is skipped with a suggestion, its siblings load");
        SceneLoadResult wrong = load(R"({"Gravity": {"strength": "strong"}})");
        CHECK(HasProblem(wrong, "/components/Gravity/strength", "expected a number, got a string") && !wrong.Scene->GetSceneEntity().Has<Gravity>(), "a wrong type skips the component, with its path");
        SceneLoadResult range = load(R"({"Gravity": {"strength": 500}})");
        CHECK(HasProblem(range, "/components/Gravity/strength", "must be at most 100") && !range.Scene->GetSceneEntity().Has<Gravity>(), "a value out of range skips the component");
        SceneLoadResult required = load(R"({"Door": {}})");
        CHECK(HasProblem(required, "/components/Door/target", "missing required field 'target'"), "a missing required field is reported");
        SceneLoadResult unsafe = load(R"({"SpriteRenderer": {"texture": "../x.png"}, "Camera": {}})");
        CHECK(HasProblem(unsafe, "/components/SpriteRenderer/texture", "must not contain '..'") && !unsafe.Scene->GetSceneEntity().Has<SpriteRenderer>() && unsafe.Scene->GetSceneEntity().Has<Camera>(), "an unsafe asset path skips the component");
        SceneLoadResult missingFile = load(R"({"SpriteRenderer": {"texture": "Assets/Textures/nope.png"}})");
        CHECK(HasProblem(missingFile, "/components/SpriteRenderer/texture", "does not exist") && missingFile.Scene->GetSceneEntity().Has<SpriteRenderer>(), "a missing asset file is reported but the component loads");

        // a key repeated inside the scene's components skips them all, and the entities still load
        SceneLoadResult repeated = LoadSceneFromText(R"({"version": 1, "name": "s", "components": {"Gravity": {"strength": 1}, "Gravity": {"strength": 2}, "Camera": {}}, "entities": [{"name": "a"}]})", "inline", registry);
        CHECK(repeated.Scene && HasProblem(repeated, "/components/Gravity", "'Gravity' appears more than once") && registry.GetComponentNames(repeated.Scene->GetSceneEntity()).empty() && repeated.Scene->FindEntity("a"), "a repeated key skips the scene's components, and the entities still load");
        CHECK(!LoadSceneFromText(R"({"version": 1, "name": "s", "components": {}, "components": {}, "entities": []})", "inline", registry).Scene, "a repeated top-level components key is still fatal");
        CHECK(HasProblem(LoadSceneFromText(R"({"version": 1, "name": "s", "componnets": {}, "entities": []})", "inline", registry), "/componnets", "did you mean 'components'"), "a misspelled components key gets a suggestion");

        // the fixture
        SceneLoadResult fixture = LoadScene("Tests/SceneTests/fixtures/scene_components_errors.json", registry);
        for (const SceneProblem& problem : fixture.Problems) printf("      %s\n", problem.ToString().c_str());
        Entity own = fixture.Scene->GetSceneEntity();
        CHECK(fixture.Problems.size() == 3 && own.Get<Camera>().Zoom == 2.0f && fixture.Scene->FindEntity("a").Get<Camera>().Primary, "the fixture's good scene component and entity load");
        CHECK(HasProblem(fixture, "/components/Transfrom", "did you mean 'Transform'") && HasProblem(fixture, "/components/Tilemap/rows/1", "this row has 1 tiles") && HasProblem(fixture, "/components/SpriteRenderer/texture", "must not contain '..'") && registry.GetComponentNames(own).size() == 1, "and the three bad ones are skipped, each reported with its path");
    }

    // scene-level components: writing
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        registry.Register<Gravity>("Gravity", "Pull.").Field(&Gravity::Strength, "strength", "How hard.").Range(0, 100);

        Scene scene("Rules");
        scene.GetSceneEntity().Add<Gravity>().Strength = 3.0f;
        scene.GetSceneEntity().Add<Camera>();                 // added second, written first: registration order
        scene.CreateEntity("a");
        const std::string golden = R"({
  "version": 1,
  "name": "Rules",
  "components": {
    "Camera": { "zoom": 1, "primary": false },
    "Gravity": { "strength": 3 }
  },
  "entities": [
    {
      "name": "a"
    }
  ]
}
)";
        SceneSaveResult saved = SaveSceneToText(scene, registry);
        for (const std::string& problem : saved.Problems) printf("      %s\n", problem.c_str());
        CHECK(saved.Ok() && saved.Text == golden, "a scene's own components are written between the name and the entities");
        CHECK(SaveSceneToText(Scene("x"), registry).Text == "{\n  \"version\": 1,\n  \"name\": \"x\",\n  \"entities\": []\n}\n", "a scene without its own components has no components key");

        SceneLoadResult back = LoadSceneFromText(golden, "golden", registry);
        CHECK(back.Ok() && SaveSceneToText(*back.Scene, registry).Text == golden, "the canonical text loads and writes back identically");
        back.Scene->GetSceneEntity().Get<Gravity>().Strength = 4.0f;
        std::vector<std::string> before = SplitLines(golden), after = SplitLines(SaveSceneToText(*back.Scene, registry).Text);
        int changed = 0;
        for (size_t i = 0; i < before.size() && i < after.size(); i++) changed += before[i] != after[i];
        CHECK(before.size() == after.size() && changed == 1, "changing one scene component changes exactly one line");

        // a component with a list of strings is a block, indented for the scene level
        Scene blocks("Blocks");
        Tilemap& map = blocks.GetSceneEntity().Add<Tilemap>();
        map.Rows = { "ab", "cd" };
        const std::string block = R"({
  "version": 1,
  "name": "Blocks",
  "components": {
    "Tilemap": {
      "texture": "",
      "cellSize": [32, 32],
      "tileSize": [64, 64],
      "rows": [
        "ab",
        "cd"
      ]
    }
  },
  "entities": []
}
)";
        CHECK(SaveSceneToText(blocks, registry).Text == block && LoadSceneFromText(block, "block", registry).Ok(), "a block component is indented for the scene level and reads back");

        // what would not read back is refused, naming the scene's component
        Scene bad("Bad");
        bad.GetSceneEntity().Add<Camera>().Zoom = 0.0f;
        SceneSaveResult refused = SaveSceneToText(bad, registry);
        CHECK(!refused.Ok() && refused.Problems[0].find("scene component 'Camera'/zoom: must be at least 0.01") != std::string::npos, "a scene component outside its range is refused, naming it");
    }

    // scene-level components: the schema and the tool
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        const Json schema = GenerateSceneSchema(registry);

        const Json& sceneComponents = schema["properties"]["components"];
        CHECK(sceneComponents["type"] == "object" && sceneComponents["additionalProperties"] == false && sceneComponents["properties"] == schema["definitions"]["entity"]["properties"]["components"]["properties"], "the schema lets the scene carry the same components an entity can");
        std::string order;
        for (auto it = schema["properties"].begin(); it != schema["properties"].end(); ++it) order += it.key() + " ";
        CHECK(order == "$schema version name components entities ", "and lists the keys in file order");

        auto errorsFor = [&](const std::string& json) { return SchemaErrors(schema, ParseWithComments(json)); };
        CHECK(errorsFor(R"({"version": 1, "name": "x", "components": {"Camera": {"zoom": 2}}, "entities": []})").empty(), "a scene with its own components validates");
        CHECK(!errorsFor(R"({"version": 1, "name": "x", "components": {"Nope": {}}, "entities": []})").empty() && !errorsFor(R"({"version": 1, "name": "x", "components": {"Camera": {"zoom": 0}}, "entities": []})").empty() && !errorsFor(R"({"version": 1, "name": "x", "components": [], "entities": []})").empty(), "an unknown scene component, a bad value and a components that is not an object are rejected");

        // the schema and the loader agree about the scene's own components in the fixture
        const std::string file = "Tests/SceneTests/fixtures/scene_components_errors.json";
        bool schemaBad = false, loaderBad = false;
        for (const std::string& e : SchemaErrors(schema, ParseWithComments(ReadFile(file)))) schemaBad = schemaBad || e.rfind("/components", 0) == 0;
        for (const SceneProblem& p : LoadScene(file, registry).Problems) loaderBad = loaderBad || p.Path.rfind("/components", 0) == 0;
        CHECK(schemaBad && loaderBad, "the schema and the loader both reject the bad scene components in the fixture");

        // the tool
        const std::filesystem::path work = std::filesystem::temp_directory_path() / "marle_scene_components_test";
        std::filesystem::remove_all(work);
        std::filesystem::create_directories(work);
        const std::string w = work.generic_string();
        const std::string compact = R"({"version":1,"name":"t","components":{"Camera":{"zoom":2}},"entities":[{"name":"a","components":{"Camera":{}}}]})";
        WriteFile(w + "/a.json", compact);
        CHECK(RunTool(registry, { "validate", w + "/a.json" }).Out == "OK    " + w + "/a.json: 1 entity, 2 components\n", "validate counts the scene's own components too");
        RunTool(registry, { "format", w + "/a.json" });
        std::string formatted = ReadFile(w + "/a.json");
        CHECK(Contains(formatted, "  \"components\": {\n    \"Camera\": { \"zoom\": 2, \"primary\": false }\n  },\n") && LoadScene(w + "/a.json", registry).Ok() && RunTool(registry, { "format", "--check", w + "/a.json" }).Code == 0, "format keeps and canonicalizes the scene's own components");
        std::filesystem::remove_all(work);
    }

    // field enums: a string that is one of a fixed set
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        registry.Register<Pad>("Pad", "A pad.")
            .Field(&Pad::Keys, "keys", "Which keys move it.").OneOf({ "wasd", "arrows", "both" })
            .Field(&Pad::Label, "label", "Free text.")
            .Field(&Pad::Count, "count", "A number.");
        const ComponentInfo* info = registry.Find("Pad");

        CHECK(info->Fields[0].OneOf == std::vector<std::string>({ "wasd", "arrows", "both" }) && info->Fields[0].Default == "both" && info->Fields[1].OneOf.empty(), "a field records its allowed values, a free string has none");

        Scene scene;
        for (const char* value : { "wasd", "arrows", "both" }) {
            Entity e = scene.CreateEntity(value);
            CHECK(registry.Read(e, "Pad", Json::parse(std::string(R"({"keys": ")") + value + R"("})")).empty() && e.Get<Pad>().Keys == value, (std::string("the value '") + value + "' is accepted").c_str());
        }
        Entity free = scene.CreateEntity("free");
        CHECK(registry.Read(free, "Pad", Json::parse(R"({"label": "anything at all", "keys": "both"})")).empty() && free.Get<Pad>().Label == "anything at all", "a string with no allowed set takes anything");
        Entity omitted = scene.CreateEntity("omitted");
        CHECK(registry.Read(omitted, "Pad", Json::object()).empty() && omitted.Get<Pad>().Keys == "both", "a field left out takes its default, which is one of the allowed values");

        Entity bad = scene.CreateEntity("bad");
        auto errors = registry.Read(bad, "Pad", Json::parse(R"({"keys": "wsad"})"));
        CHECK(errors.size() == 1 && HasError(errors, "/keys", "expected one of 'wasd', 'arrows', 'both', got 'wsad'") && HasError(errors, "/keys", "did you mean 'wasd'?"), "any other value is an error that lists the allowed ones and suggests the closest");
        errors = registry.Read(bad, "Pad", Json::parse(R"({"keys": "Both"})"));
        CHECK(HasError(errors, "/keys", "did you mean 'both'?"), "a different case is suggested too, but is still an error");
        errors = registry.Read(bad, "Pad", Json::parse(R"({"keys": "nonsense"})"));
        CHECK(errors.size() == 1 && HasError(errors, "/keys", "got 'nonsense'") && !HasError(errors, "/keys", "did you mean"), "a value that is nothing like them gets no suggestion");
        errors = registry.Read(bad, "Pad", Json::parse(R"({"keys": ""})"));
        CHECK(HasError(errors, "/keys", "got ''"), "an empty string is not an allowed value unless it is listed");
        errors = registry.Read(bad, "Pad", Json::parse(R"({"keys": 5, "count": "x"})"));
        CHECK(errors.size() == 2 && HasError(errors, "/keys", "expected a string, got a number") && HasError(errors, "/count", "expected an integer"), "a value that is not a string is a type error, reported with the others");
        CHECK(!bad.Has<Pad>(), "a component with a bad value is not added");

        // writing
        CHECK(registry.Write(free, "Pad")["keys"] == "both" && registry.Write(free, "Pad", true)["label"] == "anything at all" && !registry.Write(free, "Pad", true).contains("keys"), "an allowed value is written, and skipDefaults leaves out the default one");
        Scene refuse("Refuse");
        refuse.CreateEntity("e").Add<Pad>().Keys = "nope";
        SceneSaveResult refused = SaveSceneToText(refuse, registry);
        CHECK(!refused.Ok() && refused.Problems[0].find("entity 'e' component 'Pad'/keys: expected one of") != std::string::npos, "the writer refuses a value outside the set, so what is written reads back");

        // the loader and the tool
        SceneLoadResult load = LoadSceneFromText(R"({"version": 1, "name": "x", "entities": [{"name": "e", "components": {"Pad": {"keys": "arrow"}}}]})", "inline", registry);
        CHECK(HasProblem(load, "/entities/0/components/Pad/keys", "did you mean 'arrows'?") && !load.Scene->FindEntity("e").Has<Pad>(), "the loader reports it with its path and skips the component");
        const std::filesystem::path work = std::filesystem::temp_directory_path() / "marle_scene_enum_test";
        std::filesystem::remove_all(work);
        std::filesystem::create_directories(work);
        WriteFile((work / "a.json").string(), R"({"version": 1, "name": "x", "entities": [{"name": "e", "components": {"Pad": {"keys": "wsad"}}}]})");
        ToolRun run = RunTool(registry, { "validate", (work / "a.json").generic_string() });
        CHECK(run.Code == 1 && Contains(run.Out, "/entities/0/components/Pad/keys: expected one of 'wasd', 'arrows', 'both', got 'wsad' (did you mean 'wasd'?); the component is skipped"), "marle-scene validate prints it");
        std::filesystem::remove_all(work);

        // the schema
        const Json schema = GenerateSceneSchema(registry);
        const Json& keys = schema["definitions"]["component_Pad"]["properties"]["keys"];
        CHECK(keys["type"] == "string" && keys["enum"] == Json::array({ "wasd", "arrows", "both" }) && keys["default"] == "both" && keys["description"] == "Which keys move it. One of: wasd, arrows, both.", "the schema has an enum, and the description lists the values");
        CHECK(!schema["definitions"]["component_Pad"]["properties"]["label"].contains("enum"), "a free string has no enum");
        auto padErrors = [&](const std::string& value) { return SchemaErrors(schema, Json::parse(R"({"version": 1, "name": "x", "entities": [{"name": "e", "components": {"Pad": {"keys": )" + Json(value).dump() + "}}}]}")); };
        bool agree = true;
        for (const char* value : { "wasd", "arrows", "both", "Both", "wsad", "", "x", "wasd ", "arrows\n" }) {
            bool schemaOk = padErrors(value).empty();
            Scene probe;
            bool loaderOk = registry.Read(probe.CreateEntity("p"), "Pad", Json::parse(R"({"keys": )" + Json(std::string(value)).dump() + "}")).empty();
            if (schemaOk != loaderOk) { printf("      schema and loader disagree about '%s'\n", value); agree = false; }
        }
        CHECK(agree, "the schema and the loader accept exactly the same values");

        // setting a set up wrongly is logged and leaves the field free
        ComponentRegistry other;
        other.Register<Pad>("Pad", "A pad.")
            .Field(&Pad::Keys, "keys", "k").OneOf({ "a", "b" })                    // the default 'both' is not in the set
            .Field(&Pad::Label, "label", "l").OneOf({})                            // empty
            .Field(&Pad::Count, "count", "c").OneOf({ "1" });                      // not a string
        CHECK(other.Find("Pad")->Fields[0].OneOf.empty() && other.Find("Pad")->Fields[1].OneOf.empty() && other.Find("Pad")->Fields[2].OneOf.empty(), "a set that is empty, on a non-string, or without the default is refused");
        ComponentRegistry repeats;
        repeats.Register<Pad>("Pad", "A pad.").Field(&Pad::Keys, "keys", "k").OneOf({ "both", "both" });
        CHECK(repeats.Find("Pad")->Fields[0].OneOf.empty(), "a set with a repeated value is refused");
        ComponentRegistry empties;
        empties.Register<Pad>("Pad", "A pad.").Field(&Pad::Label, "label", "l").OneOf({ "", "x" });
        Scene emptyScene;
        Entity pe = emptyScene.CreateEntity("e");
        CHECK(empties.Find("Pad")->Fields[0].OneOf.size() == 2 && empties.Read(pe, "Pad", Json::parse(R"({"label": ""})")).empty() && !empties.Read(emptyScene.CreateEntity("f"), "Pad", Json::parse(R"({"label": "y"})")).empty(), "an empty string can be an allowed value when the default is empty");
        ComponentBuilder<Pad> detached = registry.Register<Pad>("Pad", "again");   // refused: name in use
        detached.Field(&Pad::Keys, "keys", "k").OneOf({ "both" });
        CHECK(registry.Find("Pad")->Fields[0].OneOf.size() == 3, "a set on a refused registration changes nothing");
    }

    // entity references: a field that names another entity
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        registry.Register<Follow>("Follow", "Goes after another entity.")
            .Field(&Follow::Target, "target", "The entity to follow.").EntityReference()
            .Field(&Follow::Offset, "offset", "Where from it.");
        CHECK(registry.Find("Follow")->Fields[0].EntityReference && !registry.Find("Follow")->Fields[1].EntityReference && !registry.Find("Transform")->Fields[0].EntityReference, "only the marked field is an entity reference");

        // the fixture: every case in one file
        SceneLoadResult load = LoadScene("Tests/SceneTests/fixtures/entity_references.json", registry);
        for (const SceneProblem& problem : load.Problems) printf("      %s\n", problem.ToString().c_str());
        Scene& scene = *load.Scene;
        CHECK(load.Problems.size() == 7 && scene.GetEntityCount() == 10, "the fixture has seven problems and ten entities");
        const std::string at = "/entities/";
        CHECK(!HasProblem(load, "/components/Follow/target", "no entity") && !HasProblem(load, at + "0/components/Follow/target", "no entity") && !HasProblem(load, at + "1/components/Follow/target", "no entity"), "a reference to an entity later in the file, and back, is fine, from an entity and from the scene itself");
        CHECK(!HasProblem(load, at + "2/components/Follow/target", "no entity") && !HasProblem(load, at + "3/components/Follow/target", "no entity"), "an empty reference means none, and an entity can refer to itself");
        CHECK(HasProblem(load, at + "4/components/Follow/target", "no entity named 'playr' exists in the scene (did you mean 'player'?)") && scene.FindEntity("typo").Get<Follow>().Target == "playr", "a name that matches no entity is reported with a suggestion, and the component still loads");
        CHECK(HasProblem(load, at + "5/components/Follow/target", "no entity named 'nobody' exists") && !HasProblem(load, at + "5/components/Follow/target", "did you mean") && scene.FindEntity("ghost").Has<Follow>(), "a name that is nothing like any entity has no suggestion");
        CHECK(HasProblem(load, at + "7/components/Follow/target", "no entity named 'bad/name' exists"), "an entity that was skipped does not exist for references either");
        CHECK(HasProblem(load, at + "8/components/Follow/target", "did you mean 'player'?") , "names are case sensitive, and the right case is suggested");
        CHECK(HasProblem(load, at + "9/components/Follow/target", "expected a string, got a number") && !scene.FindEntity("type_error").Has<Follow>() && !HasProblem(load, at + "9/components/Follow/target", "no entity"), "a component that failed to load has no reference to check");
        CHECK(HasProblem(load, at + "10/components/Follow/target", "no entity named '(scene)' exists"), "the scene's own entity is not an entity to refer to");
        CHECK(load.Problems.back().Path == at + "10/components/Follow/target" && load.Problems[0].Path == at + "6/name" && load.Problems[1].Path == at + "9/components/Follow/target", "reference problems come after the others");

        // a reference in the scene's own components
        SceneLoadResult sceneRef = LoadSceneFromText(R"({"version": 1, "name": "x", "components": {"Follow": {"target": "nobody"}}, "entities": [{"name": "a"}]})", "inline", registry);
        CHECK(HasProblem(sceneRef, "/components/Follow/target", "no entity named 'nobody'") && sceneRef.Scene->GetSceneEntity().Has<Follow>(), "a dangling reference in the scene's own components is reported too");
        CHECK(LoadSceneFromText(R"({"version": 1, "name": "x", "entities": [{"name": "a", "components": {"Follow": {"target": "b"}}}, {"name": "b"}]})", "inline", registry).Ok(), "a forward reference loads clean");

        // the tool
        ToolRun run = RunTool(registry, { "validate", "Tests/SceneTests/fixtures/entity_references.json" });
        CHECK(run.Code == 1 && Contains(run.Out, "/entities/4/components/Follow/target: no entity named 'playr' exists in the scene (did you mean 'player'?)") && Contains(run.Out, "FAIL  Tests/SceneTests/fixtures/entity_references.json: 7 problems"), "marle-scene validate reports them");

        // strict reload refuses a file with a dangling reference
        const std::filesystem::path work = std::filesystem::temp_directory_path() / "marle_scene_reference_test";
        std::filesystem::remove_all(work);
        std::filesystem::create_directories(work);
        const std::string file = (work / "scene.json").generic_string();
        WriteFile(file, R"({"version": 1, "name": "ok", "entities": [{"name": "a"}, {"name": "b", "components": {"Follow": {"target": "a"}}}]})");
        SceneReloader reloader(file, registry);
        CHECK(reloader.Load().Ok(), "a scene with valid references loads");
        WriteFile(file, R"({"version": 1, "name": "bad", "entities": [{"name": "a"}, {"name": "b", "components": {"Follow": {"target": "c"}}}]})");
        std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now() + std::chrono::minutes(5));
        SceneReload refused = reloader.Poll();
        CHECK(refused.Attempted && !refused.Scene && reloader.GetStatus().KeptPrevious && HasProblem(SceneLoadResult{ nullptr, reloader.GetStatus().Problems }, "/entities/1/components/Follow/target", "no entity named 'c'"), "strict reload refuses a dangling reference and keeps the scene that is running");
        std::filesystem::remove_all(work);

        // the writer
        Scene good("Good");
        good.GetSceneEntity().Add<Follow>().Target = "b";
        good.CreateEntity("a").Add<Follow>().Target = "b";   // b does not exist yet when a is made
        good.CreateEntity("b").Add<Follow>();                // an empty reference is fine
        const std::string golden = R"({
  "version": 1,
  "name": "Good",
  "components": {
    "Follow": { "target": "b", "offset": [0, 0] }
  },
  "entities": [
    {
      "name": "a",
      "components": {
        "Follow": { "target": "b", "offset": [0, 0] }
      }
    },
    {
      "name": "b",
      "components": {
        "Follow": { "target": "", "offset": [0, 0] }
      }
    }
  ]
}
)";
        SceneSaveResult saved = SaveSceneToText(good, registry);
        for (const std::string& problem : saved.Problems) printf("      %s\n", problem.c_str());
        CHECK(saved.Ok() && saved.Text == golden, "references, forward ones and empty ones, are written as they are");
        SceneLoadResult back = LoadSceneFromText(golden, "golden", registry);
        CHECK(back.Ok() && SaveSceneToText(*back.Scene, registry).Text == golden, "and they read back clean");

        Scene dangling("Dangling");
        dangling.CreateEntity("a").Add<Follow>().Target = "ghost";
        SceneSaveResult refusal = SaveSceneToText(dangling, registry);
        CHECK(!refusal.Ok() && refusal.Problems[0].find("entity 'a' component 'Follow'/target: no entity named 'ghost' exists in the scene") != std::string::npos, "the writer refuses a reference to an entity that does not exist, so what is written loads clean");
        Scene sceneLevel("SceneLevel");
        sceneLevel.GetSceneEntity().Add<Follow>().Target = "ghost";
        CHECK(!SaveSceneToText(sceneLevel, registry).Ok(), "and the same for the scene's own components");

        // the schema describes it but cannot see whether the entity exists
        const Json schema = GenerateSceneSchema(registry);
        CHECK(schema["definitions"]["component_Follow"]["properties"]["target"]["description"] == "The entity to follow. The name of another entity in the scene; empty means none." && schema["definitions"]["component_Follow"]["properties"]["target"]["type"] == "string", "the schema says the field is the name of another entity");
        CHECK(SchemaErrors(schema, ParseWithComments(ReadFile("Tests/SceneTests/fixtures/entity_references.json"))).size() == 2, "the schema sees only what it can (here the two bad entities), not which names exist");

        // setting it up wrongly is logged and ignored
        ComponentRegistry other;
        other.Register<Follow>("Follow", "f")
            .Field(&Follow::Offset, "offset", "o").EntityReference()                 // not a string
            .Field(&Follow::Target, "target", "t").OneOf({ "" }).EntityReference();   // already a set of values
        CHECK(!other.Find("Follow")->Fields[0].EntityReference && !other.Find("Follow")->Fields[1].EntityReference, "an entity reference on a non-string, or on a field limited to a set, is refused");
        ComponentRegistry combos;
        combos.Register<Follow>("Follow", "f").Field(&Follow::Target, "target", "t").EntityReference().AssetPath().OneOf({ "" });
        CHECK(combos.Find("Follow")->Fields[0].EntityReference && !combos.Find("Follow")->Fields[0].AssetPath && combos.Find("Follow")->Fields[0].OneOf.empty(), "an entity reference cannot also be an asset path or a set of values");
    }

    // random numbers: the generator
    {
        // The expected numbers come from an independent implementation of the published algorithm (the
        // minimal PCG32 from pcg-random.org, in Python); seed 42, stream 54 is the reference demo's vector.
        auto firstOf = [](RandomGenerator g, int n) { std::vector<uint32_t> v; for (int i = 0; i < n; i++) v.push_back(g.NextU32()); return v; };
        CHECK(firstOf(RandomGenerator(42, 54), 8) == std::vector<uint32_t>({ 0xa15c02b7, 0x7b47f409, 0xba1d3330, 0x83d2f293, 0xbfa4784b, 0xcbed606e, 0xbfc6a3ad, 0x812fff6d }), "seed 42, stream 54 gives the published PCG32 reference numbers");
        CHECK(firstOf(RandomGenerator(0), 6) == std::vector<uint32_t>({ 0x47c28b93, 0xb98f6a27, 0x7d3dcb1e, 0xf0761116, 0x9cc33f5b, 0xbe0e744d }), "seed 0 gives its numbers");
        CHECK(firstOf(RandomGenerator(1), 6) == std::vector<uint32_t>({ 0x9b6bdda9, 0x0c31fc48, 0xce97f8ef, 0x822c03d4, 0x943e8cf2, 0x1b75dde3 }) && firstOf(RandomGenerator(), 6) == firstOf(RandomGenerator(1), 6), "seed 1 gives its numbers, and is the default");
        CHECK(firstOf(RandomGenerator(1, 1), 3) == std::vector<uint32_t>({ 0xc9828f91, 0x1592e274, 0xc0262657 }) && firstOf(RandomGenerator(1, 1), 1) != firstOf(RandomGenerator(1), 1), "another stream gives other numbers for the same seed");
        CHECK(firstOf(RandomGenerator(0xFFFFFFFFull), 3) == std::vector<uint32_t>({ 0x1836f28a, 0x41720992, 0x67039735 }), "a seed that needs all 32 bits works");

        // always the same
        RandomGenerator a(1234), b(1234), c(1235);
        bool same = true, differs = false;
        for (int i = 0; i < 1000; i++) { uint32_t x = a.NextU32(), y = b.NextU32(), z = c.NextU32(); same = same && x == y; differs = differs || x != z; }
        CHECK(same && differs, "two generators with the same seed agree, with another seed they do not");
        RandomGenerator restart(77);
        std::vector<uint32_t> run = firstOf(restart, 20);
        for (int i = 0; i < 50; i++) restart.NextU32();
        restart.Seed(77);
        CHECK(firstOf(restart, 20) == run, "seeding again restarts the sequence");

        // saving and restoring the state
        RandomGenerator saved(5);
        for (int i = 0; i < 10; i++) saved.NextU32();
        RandomGenerator::State state = saved.GetState();
        std::vector<uint32_t> after, replayed;
        for (int i = 0; i < 10; i++) after.push_back(saved.NextU32());
        CHECK(saved.GetState() != state && state != RandomGenerator(5).GetState(), "drawing moves the state");
        saved.SetState(state);
        for (int i = 0; i < 10; i++) replayed.push_back(saved.NextU32());
        CHECK(replayed == after, "restoring the state replays the same numbers");

        // bounded integers
        auto sequence = [](RandomGenerator g, int n, uint32_t bound) { std::vector<uint32_t> v; for (int i = 0; i < n; i++) v.push_back(g.Below(bound)); return v; };
        CHECK(sequence(RandomGenerator(7), 12, 10) == std::vector<uint32_t>({ 3, 8, 3, 0, 0, 9, 6, 3, 5, 9, 8, 6 }) && sequence(RandomGenerator(9), 10, 7) == std::vector<uint32_t>({ 3, 0, 6, 0, 0, 1, 4, 1, 1, 5 }), "Below gives the numbers the reference algorithm gives");
        RandomGenerator dice(2024);
        int faces[6] = {};
        bool inRange = true;
        for (int i = 0; i < 6000; i++) { uint32_t r = dice.Below(6); inRange = inRange && r < 6; faces[r]++; }
        CHECK(inRange && *std::min_element(faces, faces + 6) > 900 && *std::max_element(faces, faces + 6) < 1100, "Below stays in range and is even (6000 rolls of a die)");
        RandomGenerator edge(1);
        CHECK(edge.Below(1) == 0 && edge.Below(0) == 0 && edge.Below(2) < 2, "a bound of 1 or 0 gives 0");
        bool large = true;
        for (int i = 0; i < 1000; i++) large = large && edge.Below(0x80000001u) < 0x80000001u;
        CHECK(large, "a bound over half the range stays in range (where a plain modulo would be most biased)");

        // integers between
        auto between = [](RandomGenerator g, int n, int lo, int hi) { std::vector<int> v; for (int i = 0; i < n; i++) v.push_back(g.IntBetween(lo, hi)); return v; };
        CHECK(between(RandomGenerator(3), 12, -5, 5) == std::vector<int>({ -3, 4, -4, 0, 5, -2, -5, -2, -2, 4, 0, -1 }), "IntBetween gives the numbers the reference algorithm gives");
        RandomGenerator ends(8);
        bool sawLow = false, sawHigh = false, within = true;
        for (int i = 0; i < 1000; i++) { int v = ends.IntBetween(3, 5); sawLow = sawLow || v == 3; sawHigh = sawHigh || v == 5; within = within && v >= 3 && v <= 5; }
        CHECK(sawLow && sawHigh && within, "IntBetween includes both ends and nothing outside them");
        CHECK(ends.IntBetween(4, 4) == 4 && ends.IntBetween(9, 2) == 9, "one value is that value, and a reversed range gives the minimum");
        bool negative = true, wide = false;
        for (int i = 0; i < 500; i++) { int v = ends.IntBetween(-1000000, -999990); negative = negative && v >= -1000000 && v <= -999990; }
        for (int i = 0; i < 100; i++) wide = wide || ends.IntBetween(INT_MIN, INT_MAX) < 0;
        CHECK(negative && wide, "IntBetween works for negative ranges and for the whole range of int");

        // floats
        RandomGenerator f(1);
        std::vector<int> bits;
        for (int i = 0; i < 4; i++) bits.push_back((int)(f.NextFloat() * 16777216.0f));
        CHECK(bits == std::vector<int>({ 10185693, 799228, 13539320, 8530947 }), "NextFloat is 24 random bits over 2^24, exactly");
        RandomGenerator g(11);
        bool unit = true;
        double sum = 0;
        for (int i = 0; i < 100000; i++) { float v = g.NextFloat(); unit = unit && v >= 0.0f && v < 1.0f; sum += v; }
        CHECK(unit && std::fabs(sum / 100000 - 0.5) < 0.01, "NextFloat is from 0 up to, not including, 1 and centered on one half");
        bool ranged = true;
        for (int i = 0; i < 10000; i++) { float v = g.FloatBetween(-2.5f, 4.0f); ranged = ranged && v >= -2.5f && v < 4.0f; }
        CHECK(ranged && g.FloatBetween(3.0f, 3.0f) == 3.0f && g.FloatBetween(5.0f, 1.0f) == 5.0f, "FloatBetween stays in range, and a range with no width gives its minimum");
        int heads = 0, never = 0, always = 0;
        for (int i = 0; i < 10000; i++) { heads += g.Chance(0.5f); never += g.Chance(0.0f); always += g.Chance(1.0f); }
        CHECK(never == 0 && always == 10000 && heads > 4800 && heads < 5200, "Chance never happens at 0, always at 1 and half the time at one half");
    }

    // random numbers: the scene's generator
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        auto first = [](Scene& scene, int n) { std::vector<uint32_t> v; for (int i = 0; i < n; i++) v.push_back(scene.GetRandom().NextU32()); return v; };
        const std::vector<uint32_t> seed42 = { 0xa15c02b7, 0x7b47f409, 0xba1d3330, 0x83d2f293 };
        const std::vector<uint32_t> seed1 = { 0x9b6bdda9, 0x0c31fc48, 0xce97f8ef, 0x822c03d4 };

        const ComponentInfo* info = registry.Find("Random");
        CHECK(info && info->Fields.size() == 1 && info->Fields[0].Name == "seed" && info->Fields[0].Type == FieldType::Int && info->Fields[0].Default == 1 && !info->Fields[0].Min && !info->Fields[0].Max && !info->Description.empty(), "Random is a registered component with a seed that can be any integer");
        CHECK(Contains(info->Fields[0].Description, "same sequence") && Contains(info->Description, "clock"), "and says what it is for");

        Scene plain("p");
        CHECK(first(plain, 4) == seed1, "a scene without a Random component starts from seed 1");
        plain.GetSceneEntity().Add<Random>().Seed = 42;
        CHECK(first(plain, 2) != std::vector<uint32_t>({ seed42[0], seed42[1] }) , "changing the component does not change a generator that is already running");
        plain.ResetRandom();
        CHECK(first(plain, 4) == seed42, "ResetRandom restarts it from the component's seed");
        plain.ResetRandom();
        CHECK(first(plain, 4) == seed42, "and again from the same place");
        plain.GetSceneEntity().Remove<Random>();
        plain.ResetRandom();
        CHECK(first(plain, 4) == seed1, "without the component it is back to seed 1");

        // loaded from a scene file
        SceneLoadResult load = LoadSceneFromText(R"({"version": 1, "name": "s", "components": {"Random": {"seed": 42}}, "entities": []})", "inline", registry);
        CHECK(load.Ok() && load.Scene->GetSceneEntity().Get<Random>().Seed == 42 && first(*load.Scene, 4) == seed42, "loading a scene starts its generator from the seed in the file");
        SceneLoadResult twice = LoadSceneFromText(R"({"version": 1, "name": "s", "components": {"Random": {"seed": 42}}, "entities": []})", "inline", registry);
        CHECK(first(*twice.Scene, 4) == seed42, "and loading it again gives the same numbers");
        CHECK(first(*LoadSceneFromText(R"({"version": 1, "name": "s", "entities": []})", "inline", registry).Scene, 4) == seed1, "a file without a seed uses seed 1");

        SceneLoadResult negative = LoadSceneFromText(R"({"version": 1, "name": "s", "components": {"Random": {"seed": -1}}, "entities": []})", "inline", registry);
        CHECK(first(*negative.Scene, 3) == std::vector<uint32_t>({ 0x1836f28a, 0x41720992, 0x67039735 }), "a negative seed is its 32-bit value, and a different seed from the positive one");
        CHECK(first(*LoadSceneFromText(R"({"version": 1, "name": "s", "components": {"Random": {"seed": 2147483647}}, "entities": []})", "inline", registry).Scene, 1) != first(*LoadSceneFromText(R"({"version": 1, "name": "s", "components": {"Random": {"seed": -2147483648}}, "entities": []})", "inline", registry).Scene, 1), "the largest and smallest seeds differ");

        // a bad seed
        SceneLoadResult bad = LoadSceneFromText(R"({"version": 1, "name": "s", "components": {"Random": {"seed": 1.5}}, "entities": []})", "inline", registry);
        CHECK(HasProblem(bad, "/components/Random/seed", "expected an integer, got a number with a fraction") && first(*bad.Scene, 4) == seed1, "a seed that is not an integer is a problem, and the scene falls back to seed 1");
        CHECK(HasProblem(LoadSceneFromText(R"({"version": 1, "name": "s", "components": {"Random": {"sed": 1}}, "entities": []})", "inline", registry), "/components/Random/sed", "did you mean 'seed'?"), "a misspelled field gets a suggestion");

        // no shared state
        Scene one("one"), two("two");
        std::vector<uint32_t> fromOne = first(one, 5);
        CHECK(first(two, 5) == fromOne, "two scenes with the same seed give the same numbers");
        first(one, 100);
        RandomGenerator reference(1);
        for (int i = 0; i < 5; i++) reference.NextU32();   // two has drawn five numbers; the next five are numbers 6 to 10
        std::vector<uint32_t> nextFive;
        for (int i = 0; i < 5; i++) nextFive.push_back(reference.NextU32());
        CHECK(first(two, 5) == nextFive, "drawing a hundred numbers from one scene does not move another's sequence");
        Scene three("three");
        first(one, 7);
        CHECK(first(three, 5) == fromOne, "a new scene starts from the start however much another has drawn");

        // writing: the seed is data, the position in the sequence is not
        Scene saved("saved");
        saved.GetSceneEntity().Add<Random>().Seed = 42;
        saved.ResetRandom();
        first(saved, 10);
        SceneSaveResult text = SaveSceneToText(saved, registry);
        CHECK(text.Ok() && Contains(text.Text, "\"components\": {\n    \"Random\": { \"seed\": 42 }\n  },"), "the seed is written as a scene component");
        SceneLoadResult back = LoadSceneFromText(text.Text, "saved", registry);
        CHECK(back.Ok() && first(*back.Scene, 4) == seed42, "a saved scene starts its sequence again from the seed");

        // the schema and the example
        const Json schema = GenerateSceneSchema(registry);
        const Json& seed = schema["definitions"]["component_Random"]["properties"]["seed"];
        CHECK(seed["type"] == "integer" && seed["default"] == 1 && !seed.contains("minimum") && !seed.contains("maximum"), "the schema describes the seed as any integer");
        CHECK(SchemaErrors(schema, ParseWithComments(R"({"version": 1, "name": "x", "components": {"Random": {"seed": 7}}, "entities": []})")).empty() && !SchemaErrors(schema, ParseWithComments(R"({"version": 1, "name": "x", "components": {"Random": {"seed": 0.5}}, "entities": []})")).empty(), "a scene with a seed validates, a fractional seed does not");
        SceneLoadResult example = LoadScene("Assets/Scenes/example.json", registry);
        CHECK(example.Ok() && example.Scene->GetSceneEntity().Get<Random>().Seed == 1 && first(*example.Scene, 4) == seed1, "the example scene carries a seed of its own");
    }

    // field enums: key names
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        registry.Register<Binding>("Binding", "A key binding.").Field(&Binding::Key, "key", "Which key.").OneOf(KeyNames());
        const size_t count = KeyNames().size();
        CHECK(registry.Find("Binding")->Fields[0].OneOf == KeyNames() && count > 100, "a field can be limited to every key name");

        Scene scene;
        bool all = true;
        for (const std::string& name : KeyNames()) {
            Scene one;
            Entity e = one.CreateEntity("e");
            all = all && registry.Read(e, "Binding", Json::parse(R"({"key": )" + Json(name).dump() + "}")).empty() && e.Get<Binding>().Key == name && KeyFromName(e.Get<Binding>().Key) != Key::Unknown;
        }
        CHECK(all, "every key name is accepted and names a key");

        Entity bad = scene.CreateEntity("bad");
        auto errors = registry.Read(bad, "Binding", Json::parse(R"({"key": "Spce"})"));
        CHECK(errors.size() == 1 && HasError(errors, "/key", "expected one of the " + std::to_string(count) + " allowed values (the schema lists them), got 'Spce' (did you mean 'Space'?)"), "a long list is not printed in the error, and the closest name is suggested");
        CHECK(!HasError(errors, "/key", "'A', 'B'"), "and the message stays short");
        CHECK(HasError(registry.Read(bad, "Binding", Json::parse(R"({"key": "w"})")), "/key", "did you mean 'W'?") && HasError(registry.Read(bad, "Binding", Json::parse(R"({"key": "rightbracket"})")), "/key", "did you mean 'RightBracket'?"), "a different case is suggested too");
        CHECK(!registry.Read(bad, "Binding", Json::parse(R"({"key": ""})")).empty() && !registry.Read(bad, "Binding", Json::parse(R"({"key": "RBracket"})")).empty(), "an empty string and the old name of a key are not key names");

        const Json schema = GenerateSceneSchema(registry);
        const Json& key = schema["definitions"]["component_Binding"]["properties"]["key"];
        CHECK(key["type"] == "string" && key["enum"].size() == count && key["enum"][0] == "A" && key["default"] == "Q" && key["description"] == "Which key. One of " + std::to_string(count) + " values, listed in enum.", "the schema has every name in an enum, and the description does not repeat them");
        auto schemaOk = [&](const std::string& name) { return SchemaErrors(schema, Json::parse(R"({"version": 1, "name": "x", "entities": [{"name": "e", "components": {"Binding": {"key": )" + Json(name).dump() + "}}}]}")).empty(); };
        bool agree = true;
        for (const std::string& name : KeyNames()) agree = agree && schemaOk(name);
        for (const char* name : { "w", "Spce", "", "RBracket", "Escape ", "F13" }) {
            Scene probe;
            agree = agree && !schemaOk(name) && !registry.Read(probe.CreateEntity("p"), "Binding", Json::parse(R"({"key": )" + Json(std::string(name)).dump() + "}")).empty();
        }
        CHECK(agree, "the schema and the loader accept exactly the key names");

        // a default that is not a key name is refused when the set is registered
        ComponentRegistry other;
        other.Register<Pad>("Pad", "A pad.").Field(&Pad::Keys, "keys", "k").OneOf(KeyNames());   // the default 'both' is not a key
        CHECK(other.Find("Pad")->Fields[0].OneOf.empty(), "the usual guard applies: a default outside the names refuses the set");
    }

    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
