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
#include "Marle/Scene/Simulation.h"
#include "Marle/Scene/InputRecording.h"
#include "Marle/CommandLine.h"
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
// Components with behaviors, for the simulation tests
static std::vector<std::string> g_BehaviorLog;
struct LogA { std::string Label; };
struct LogB { std::string Label; };
struct Counter { int Count = 0; int Every = 1; };
struct Drift { glm::vec2 Velocity = { 0.0f, 0.0f }; };
struct Stamp { float Elapsed = 0.0f; };
struct Dice { int Last = 0; int Sum = 0; };
struct Keyed { int Presses = 0; int FramesDown = 0; float Scroll = 0.0f; };
struct Mischief { int Mode = 0; std::string Target; int Done = 0; };   // 1 destroys Target, 2 creates an entity each step

static ComponentRegistry MakeSimulationRegistry()
{
    ComponentRegistry registry;
    RegisterBuiltinComponents(registry);
    registry.Register<LogA>("LogA", "Logs.").Field(&LogA::Label, "label", "l")
        .Behavior([](LogA& c, BehaviorContext& ctx) { g_BehaviorLog.push_back(ctx.Self.GetName() + ":A:" + c.Label); });
    registry.Register<LogB>("LogB", "Logs.").Field(&LogB::Label, "label", "l")
        .Behavior([](LogB& c, BehaviorContext& ctx) { g_BehaviorLog.push_back(ctx.Self.GetName() + ":B:" + c.Label); });
    registry.Register<Counter>("Counter", "Counts.").Field(&Counter::Count, "count", "c").Field(&Counter::Every, "every", "e")
        .Behavior([](Counter& c, BehaviorContext&) { c.Count += c.Every; });
    registry.Register<Drift>("Drift", "Moves.").Field(&Drift::Velocity, "velocity", "v")
        .Behavior([](Drift& c, BehaviorContext& ctx) { if (Transform* t = ctx.Self.TryGet<Transform>()) t->Position += c.Velocity * (float)ctx.Dt; });
    registry.Register<Stamp>("Stamp", "Time.").Field(&Stamp::Elapsed, "elapsed", "e")
        .Behavior([](Stamp& c, BehaviorContext& ctx) { c.Elapsed += (float)ctx.Dt; });
    registry.Register<Dice>("Dice", "Rolls.").Field(&Dice::Last, "last", "l").Field(&Dice::Sum, "sum", "s")
        .Behavior([](Dice& c, BehaviorContext& ctx) { c.Last = ctx.Random.IntBetween(1, 6); c.Sum += c.Last; });
    registry.Register<Keyed>("Keyed", "Reads input.").Field(&Keyed::Presses, "presses", "p").Field(&Keyed::FramesDown, "framesDown", "f").Field(&Keyed::Scroll, "scroll", "s")
        .Behavior([](Keyed& c, BehaviorContext& ctx) { c.Presses += ctx.Input.WasPressed(Key::W); c.FramesDown += ctx.Input.IsDown(Key::W); c.Scroll += ctx.Input.Scroll; });
    registry.Register<Mischief>("Mischief", "Changes the scene.").Field(&Mischief::Mode, "mode", "m").Field(&Mischief::Target, "target", "t").Field(&Mischief::Done, "done", "d")
        .Behavior([](Mischief& c, BehaviorContext& ctx) {
            if (c.Mode == 1) ctx.Scene.DestroyEntity(ctx.Scene.FindEntity(c.Target));
            if (c.Mode == 2) ctx.Scene.CreateEntity("spawned" + std::to_string(c.Done)).Add<Counter>();
            c.Done++;
        });
    return registry;
}

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

// Keys, running, turning and the wheel, chosen by a seeded generator so a script is always the same:
// mostly up and to the right, so the player reaches the walls
static InputFrame ScriptedFrame(RandomGenerator& script)
{
    InputFrame in;
    if (script.Chance(0.7f)) in.Hold(Key::W);
    if (script.Chance(0.1f)) in.Hold(Key::A);
    if (script.Chance(0.1f)) in.Hold(Key::S);
    if (script.Chance(0.7f)) in.Hold(Key::D);
    if (script.Chance(0.05f)) in.Hold(Key::Up);
    if (script.Chance(0.05f)) in.Hold(Key::Down);
    if (script.Chance(0.05f)) in.Hold(Key::Left);
    if (script.Chance(0.05f)) in.Hold(Key::Right);
    if (script.Chance(0.5f)) in.Hold(Key::LeftShift);
    float turnRoll = script.NextFloat();
    if (turnRoll < 0.12f) in.Hold(Key::Q); else if (turnRoll < 0.24f) in.Hold(Key::E);   // never both: the old code left a rounding residue then
    float wheel = script.NextFloat();
    in.Scroll = wheel < 0.05f ? 1.0f : wheel < 0.1f ? -1.0f : 0.0f;
    return in;
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
        CHECK(names == std::vector<std::string>({ "Transform", "SpriteRenderer", "Camera", "Tilemap", "Random", "KeyboardMove", "KeyboardRotate", "WheelZoom", "Follow", "Spin", "Orbit", "Wander", "RestartOnKey", "ClampPosition" }), "the built-in components register in order, the behaviors last, ClampPosition after all of them");

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

        // every complete scene in a jsonc block of docs/scene-format.md is valid (the first one must exist)
        std::ifstream doc("docs/scene-format.md");
        std::stringstream docText;
        docText << doc.rdbuf();
        const std::string all = docText.str();
        int scenesInSpec = 0;
        bool specScenesOk = true;
        for (size_t at = all.find("```jsonc\n"); at != std::string::npos; at = all.find("```jsonc\n", at + 1)) {
            size_t end = all.find("\n```", at);
            std::string block = all.substr(at + 9, end - at - 9);
            if (block.find("\"version\"") == std::string::npos) continue;   // a fragment of a component, not a scene
            scenesInSpec++;
            SceneLoadResult snippet = LoadSceneFromText(block, "docs/scene-format.md", registry);
            for (const SceneProblem& problem : snippet.Problems) printf("      %s\n", problem.ToString().c_str());
            specScenesOk = specScenesOk && snippet.Ok();
        }
        CHECK(doc.is_open() && scenesInSpec >= 2 && specScenesOk, "every complete scene in the spec's examples is a valid scene");
        SceneLoadResult first;
        size_t firstAt = all.find("```jsonc\n");
        first = LoadSceneFromText(all.substr(firstAt + 9, all.find("\n```", firstAt) - firstAt - 9), "docs/scene-format.md", registry);
        CHECK(first.Ok() && first.Scene->FindEntity("player").Get<Transform>().Position == glm::vec2(10.0f, 20.0f), "the first one is the short example near the top");
        SceneLoadResult walk;
        size_t walkAt = all.find("\"name\": \"Walk\"");
        walk = LoadSceneFromText(all.substr(all.rfind("```jsonc\n", walkAt) + 9, all.find("\n```", walkAt) - all.rfind("```jsonc\n", walkAt) - 9), "docs/scene-format.md", registry);
        CHECK(walk.Ok() && walk.Scene->FindEntity("player").Has<KeyboardMove>() && walk.Scene->FindEntity("camera").Has<WheelZoom>(), "and the walking example has its behaviors");

        // the spec's component table lists every built-in component and field, in order
        bool tableComplete = true;
        for (const ComponentInfo* info : registry.GetComponents()) {
            for (size_t i = 0; i < info->Fields.size(); i++) {
                const std::string row = i == 0 ? "| `" + info->Name + "` | `" + info->Fields[0].Name + "` |" : "| | `" + info->Fields[i].Name + "` |";
                if (all.find(row) == std::string::npos) { printf("      the spec's table has no row '%s'\n", row.c_str()); tableComplete = false; }
            }
        }
        CHECK(tableComplete, "the spec's component table has a row for every field of every built-in component");

        // the key names the spec lists are the ones the engine has
        size_t namesAt = all.find("```keynames\n");
        std::set<std::string> listed;
        if (namesAt != std::string::npos) {
            std::stringstream block(all.substr(namesAt + 12, all.find("\n```", namesAt) - namesAt - 12));
            for (std::string word; block >> word;) listed.insert(word);
        }
        CHECK(!listed.empty() && listed == std::set<std::string>(KeyNames().begin(), KeyNames().end()), "the key names in the spec are exactly the ones the engine accepts");

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
        CHECK(order == "entity component_Transform component_SpriteRenderer component_Camera component_Tilemap component_Random component_KeyboardMove component_KeyboardRotate component_WheelZoom component_Follow component_Spin component_Orbit component_Wander component_RestartOnKey component_ClampPosition ", "components are described in registration order");
        CHECK(schema["definitions"]["entity"]["properties"]["components"]["properties"]["Camera"]["$ref"] == "#/definitions/component_Camera", "an entity's components point at their definitions");

        bool everyField = true, described = !schema["description"].get<std::string>().empty();
        for (const ComponentInfo* info : registry.GetComponents()) {
            const Json& definition = schema["definitions"]["component_" + info->Name];
            described = described && definition["description"].get<std::string>().find(info->Description) == 0 && definition["additionalProperties"] == false;
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
        CHECK(scenes.Code == 0 && scenes.Out == ok.Out + "OK    Assets/Scenes/world.json: 8 entities, 27 components\n2 files, 0 with problems\n", "a directory means its scene files, and the schema file is not one of them");
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
        CHECK(names == "world spinner_0 spinner_1 spinner_2 spinner_3 player satellite camera ", "its entities are in draw order, with the camera after the player and the satellite it follows");

        Entity camera = world.Scene->FindEntity("camera"), player = world.Scene->FindEntity("player"), map = world.Scene->FindEntity("world");
        CHECK(camera.Get<Camera>().Primary && camera.Get<Camera>().Zoom == 1.0f && camera.Get<Transform>().Position == player.Get<Transform>().Position && player.Get<Transform>().Position == glm::vec2(1024.0f, 518.0f), "the camera is primary and starts on the player");

        // what makes it move is in the file
        const KeyboardMove& move = player.Get<KeyboardMove>();
        CHECK(move.Speed == 300.0f && move.RunMultiplier == 2.0f && move.RunKey == "LeftShift" && move.Keys == "both" && move.RelativeTo == "camera" && move.TintWhenRunning && move.IdleTint == glm::vec4(1.0f) && move.RunTint == glm::vec4(1.0f, 0.65f, 0.3f, 1.0f) && player.Get<ClampPosition>().Min == glm::vec2(88.0f) && player.Get<ClampPosition>().Max == glm::vec2(1960.0f, 1448.0f), "the player walks at 300, runs at double, relative to the camera, turns orange running, and stays inside the border tiles");
        CHECK(camera.Get<KeyboardRotate>().Speed == 1.0f && camera.Get<KeyboardRotate>().CounterClockwiseKey == "Q" && camera.Get<KeyboardRotate>().ClockwiseKey == "E" && camera.Get<WheelZoom>().Factor == 1.1f && camera.Get<WheelZoom>().MinZoom == 0.25f && camera.Get<WheelZoom>().MaxZoom == 4.0f && camera.Get<Follow>().Target == "player", "the camera follows the player, turns with Q and E and zooms with the wheel between 0.25 and 4");
        Entity satelliteEntity = world.Scene->FindEntity("satellite");
        CHECK(satelliteEntity.Get<Orbit>().Target == "player" && satelliteEntity.Get<Orbit>().Radius == 44.0f && satelliteEntity.Get<Orbit>().Speed == 2.0f && satelliteEntity.Get<Orbit>().RotationSpeed == 6.0f, "the satellite orbits the player at 44 units, 2 radians a second, turning three times as fast");
        bool spinnersOk = true;
        for (int i = 0; i < 4; i++) {
            Entity spinner = world.Scene->FindEntity("spinner_" + std::to_string(i));
            spinnersOk = spinnersOk && spinner.Get<Spin>().Speed == (i % 2 ? 2.0f : -2.0f) && spinner.Get<Transform>().Rotation == i * 1.5707963f;
        }
        CHECK(spinnersOk && world.Scene->GetSceneEntity().Get<RestartOnKey>().Key == "R", "the spinners turn two radians a second, alternately each way, from a quarter turn apart, and R restarts the scene");
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

    // the Sandbox world, played by its behaviors, against the movement code it replaced
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        SceneLoadResult now = LoadScene("Assets/Scenes/world.json", registry);
        CHECK(now.Ok(), "the world loads");

        // The Sandbox's old movement code, line for line, with one change: the order of the file. The player
        // is before the camera, so it moves by the rotation the camera had at the end of the previous step,
        // where the old code had turned the camera first.
        struct OldSandbox {
            glm::vec2 player = { 1024.0f, 518.0f }, satellite = { 1068.0f, 518.0f }, camera = { 1024.0f, 518.0f };
            float cameraRotation = 0.0f, zoom = 1.0f, orbit = 0.0f, satelliteRotation = 0.0f, spinners[4] = { 0.0f * 1.5707963f, 1.0f * 1.5707963f, 2.0f * 1.5707963f, 3.0f * 1.5707963f };   // a quarter turn apart, as in the file
            glm::vec4 tint = { 1.0f, 1.0f, 1.0f, 1.0f };
            void Step(const InputFrame& in, double dtSeconds)
            {
                const float dt = (float)dtSeconds;
                glm::vec2 move(0.0f);
                if (in.IsDown(Key::W) || in.IsDown(Key::Up))    move.y += 1.0f;
                if (in.IsDown(Key::S) || in.IsDown(Key::Down))  move.y -= 1.0f;
                if (in.IsDown(Key::D) || in.IsDown(Key::Right)) move.x += 1.0f;
                if (in.IsDown(Key::A) || in.IsDown(Key::Left))  move.x -= 1.0f;
                if (move != glm::vec2(0.0f)) {
                    float c = cosf(cameraRotation), s = sinf(cameraRotation);
                    glm::vec2 dir = glm::normalize(move);
                    dir = { dir.x * c - dir.y * s, dir.x * s + dir.y * c };
                    float speed = 300.0f * (in.IsDown(Key::LeftShift) ? 2.0f : 1.0f);
                    player += dir * speed * dt;
                }
                const float margin = 64.0f + 48.0f * 0.5f;
                player.x = glm::clamp(player.x, margin, 32 * 64.0f - margin);
                player.y = glm::clamp(player.y, margin, 24 * 64.0f - margin);
                tint = in.IsDown(Key::LeftShift) ? glm::vec4(1.0f, 0.65f, 0.3f, 1.0f) : glm::vec4(1.0f);

                orbit += 2.0f * dt;
                satellite = player + 44.0f * glm::vec2(cosf(orbit), sinf(orbit));
                satelliteRotation += 6.0f * dt;
                for (int i = 0; i < 4; i++) spinners[i] += (i % 2 ? 2.0f : -2.0f) * dt;

                float turn = 0.0f;
                if (in.IsDown(Key::Q)) turn += 1.0f * dt;
                if (in.IsDown(Key::E)) turn -= 1.0f * dt;
                cameraRotation += turn;
                if (in.Scroll != 0.0f) zoom = glm::clamp(zoom * powf(1.1f, in.Scroll), 0.25f, 4.0f);
                camera = player;
            }
        };

        OldSandbox then;
        RandomGenerator script(11);
        bool identical = true, spinnersClose = true, satelliteClose = true, sawWall = false, sawRestart = false;
        float worstSpinner = 0.0f;
        float spin = 0.0f;   // the old code's spin, for comparing the spinners with its own formula
        for (int i = 0; i < 900 && identical; i++) {
            InputFrame in = ScriptedFrame(script);
            then.Step(in, 1.0 / 60.0);
            spin += 2.0f * (float)(1.0 / 60.0);
            Simulation::Step(registry, *now.Scene, in, 1.0 / 60.0);

            Entity player = now.Scene->FindEntity("player"), camera = now.Scene->FindEntity("camera"), satellite = now.Scene->FindEntity("satellite");
            identical = player.Get<Transform>().Position == then.player && player.Get<SpriteRenderer>().Tint == then.tint
                && camera.Get<Transform>().Position == then.camera && camera.Get<Transform>().Rotation == then.cameraRotation && camera.Get<Camera>().Zoom == then.zoom
                && satellite.Get<Transform>().Position == then.satellite && satellite.Get<Transform>().Rotation == then.satelliteRotation && satellite.Get<Orbit>().Angle == then.orbit;
            for (int k = 0; k < 4; k++) {
                float rotation = now.Scene->FindEntity("spinner_" + std::to_string(k)).Get<Transform>().Rotation;
                identical = identical && rotation == then.spinners[k];
                float oldFormula = spin * (k % 2 ? 1.0f : -1.0f) + (float)k * 1.5707963f;     // what the old code drew
                worstSpinner = std::max(worstSpinner, std::fabs(rotation - oldFormula));
            }
            satelliteClose = satelliteClose && std::fabs(satellite.Get<Transform>().Rotation - spin * 3.0f) < 1e-3f;
            sawWall = sawWall || then.player.x == 88.0f || then.player.y == 88.0f || then.player.x == 1960.0f || then.player.y == 1448.0f;
            if (!identical) printf("      differs at step %d: player %.6f,%.6f vs %.6f,%.6f\n", i, player.Get<Transform>().Position.x, player.Get<Transform>().Position.y, then.player.x, then.player.y);
        }
        spinnersClose = worstSpinner < 1e-3f;
        CHECK(identical, "900 scripted steps through the real world file give exactly the old code's player, tint, camera, zoom, satellite and spinners");
        CHECK(spinnersClose && satelliteClose, "and the spinners and the satellite's turn are the old formulas to well under a thousandth of a radian");
        CHECK(sawWall, "the script drives the player into the walls");
        CHECK(Simulation::Checksum(registry, *now.Scene) != Simulation::Checksum(registry, *LoadScene("Assets/Scenes/world.json", registry).Scene), "and the world has moved on from the file");

        // R restarts: the scene asks, and loading the file again gives back the start
        SceneLoadResult restart = LoadScene("Assets/Scenes/world.json", registry);
        RandomGenerator script2(3);
        for (int i = 0; i < 120; i++) Simulation::Step(registry, *restart.Scene, ScriptedFrame(script2), 1.0 / 60.0);
        CHECK(!restart.Scene->IsRestartRequested(), "the world does not ask for a restart by itself");
        Simulation::Step(registry, *restart.Scene, InputFrame().Press(Key::R), 1.0 / 60.0);
        CHECK(restart.Scene->IsRestartRequested(), "pressing R makes it ask");
        sawRestart = Simulation::Checksum(registry, *LoadScene("Assets/Scenes/world.json", registry).Scene) == Simulation::Checksum(registry, *LoadScene("Assets/Scenes/world.json", registry).Scene) && Simulation::Checksum(registry, *restart.Scene) != Simulation::Checksum(registry, *LoadScene("Assets/Scenes/world.json", registry).Scene);
        CHECK(sawRestart, "and loading the file again is the start, different from the world as it has become");

        // the same input played twice ends in the same state
        SceneLoadResult one = LoadScene("Assets/Scenes/world.json", registry), two = LoadScene("Assets/Scenes/world.json", registry);
        RandomGenerator a(21), b(21);
        for (int i = 0; i < 600; i++) { Simulation::Step(registry, *one.Scene, ScriptedFrame(a), 1.0 / 60.0); Simulation::Step(registry, *two.Scene, ScriptedFrame(b), 1.0 / 60.0); }
        CHECK(Simulation::Checksum(registry, *one.Scene) == Simulation::Checksum(registry, *two.Scene), "the same input played twice ends in the same state");

        // an edit to the file while the world runs: it carries on, and what the author changed is the file's
        const std::string text = ReadFile("Assets/Scenes/world.json");
        SceneLoadResult baseline = LoadSceneFromText(text, "baseline", registry);
        SceneLoadResult edited = LoadSceneFromText(text.substr(0, text.find("\"speed\": 300")) + "\"speed\": 450" + text.substr(text.find("\"speed\": 300") + 12), "edited", registry);
        CHECK(edited.Ok() && edited.Scene->FindEntity("player").Get<KeyboardMove>().Speed == 450.0f, "an edited copy of the world has the player faster");
        glm::vec2 where = one.Scene->FindEntity("player").Get<Transform>().Position;
        float turned = one.Scene->FindEntity("camera").Get<Transform>().Rotation, zoomed = one.Scene->FindEntity("camera").Get<Camera>().Zoom, around = one.Scene->FindEntity("satellite").Get<Orbit>().Angle;
        CarryOverState(registry, *baseline.Scene, *one.Scene, *edited.Scene);
        CHECK(edited.Scene->FindEntity("player").Get<KeyboardMove>().Speed == 450.0f && edited.Scene->FindEntity("player").Get<Transform>().Position == where && edited.Scene->FindEntity("camera").Get<Transform>().Rotation == turned && edited.Scene->FindEntity("camera").Get<Camera>().Zoom == zoomed && edited.Scene->FindEntity("satellite").Get<Orbit>().Angle == around, "after a reload the player is where it was, the camera turned and zoomed as it was, the satellite where it was round, and the new speed is in force");
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

    // behaviors and the simulation step
    {
        ComponentRegistry registry = MakeSimulationRegistry();
        auto load = [&](const std::string& text) { return LoadSceneFromText(text, "inline", registry); };
        auto step = [&](Scene& scene, int n, const InputFrame& input = InputFrame(), double dt = 1.0 / 60.0) { for (int i = 0; i < n; i++) Simulation::Step(registry, scene, input, dt); };

        // a behavior is a component with an update, and nothing else about it changes
        CHECK(registry.Find("Counter")->IsBehavior && registry.Find("LogA")->IsBehavior && !registry.Find("Transform")->IsBehavior && !registry.Find("Tilemap")->IsBehavior, "a component with an update is a behavior, the others are not");
        ComponentRegistry none;
        none.Register<Counter>("Counter", "c").Field(&Counter::Count, "count", "c").Behavior(nullptr);
        CHECK(!none.Find("Counter")->IsBehavior, "an empty update does not make a behavior");
        SceneLoadResult counted = load(R"({"version": 1, "name": "c", "entities": [{"name": "a", "components": {"Counter": {"every": 3}}}]})");
        CHECK(counted.Ok() && counted.Scene->FindEntity("a").Get<Counter>().Every == 3 && Contains(SaveSceneToText(*counted.Scene, registry).Text, R"("Counter": { "count": 0, "every": 3 })"), "a behavior loads and writes like any component");
        const Json schema = GenerateSceneSchema(registry);
        CHECK(Contains(schema["definitions"]["component_Counter"]["description"].get<std::string>(), "A behavior: it runs every fixed step, in file order.") && !Contains(schema["definitions"]["component_Transform"]["description"].get<std::string>(), "behavior"), "the schema says which components are behaviors");

        // the order: the scene's own components, then entities in file order, in an entity by registration order
        g_BehaviorLog.clear();
        SceneLoadResult ordered = load(R"({"version": 1, "name": "o",
            "components": {"LogB": {"label": "scene"}, "LogA": {"label": "scene"}},
            "entities": [
              {"name": "e1", "components": {"LogB": {"label": "b"}, "LogA": {"label": "a"}}},
              {"name": "e2", "components": {"LogA": {"label": "a"}}},
              {"name": "e3"},
              {"name": "e4", "components": {"LogB": {"label": "b"}}}]})");
        step(*ordered.Scene, 2);
        const std::vector<std::string> once = { "(scene):A:scene", "(scene):B:scene", "e1:A:a", "e1:B:b", "e2:A:a", "e4:B:b" };
        std::vector<std::string> twice = once;
        twice.insert(twice.end(), once.begin(), once.end());
        CHECK(g_BehaviorLog == twice, "behaviors run for the scene first, then each entity in file order, and in an entity in registration order, every step");

        // counting, and components that are not behaviors
        SceneLoadResult counts = load(R"({"version": 1, "name": "c", "components": {"Counter": {"every": 5}}, "entities": [{"name": "a", "components": {"Counter": {}}}, {"name": "b", "components": {"Counter": {"every": 3}, "Tilemap": {"rows": ["a"]}}}]})");
        step(*counts.Scene, 100);
        CHECK(counts.Scene->FindEntity("a").Get<Counter>().Count == 100 && counts.Scene->FindEntity("b").Get<Counter>().Count == 300 && counts.Scene->GetSceneEntity().Get<Counter>().Count == 500, "every behavior runs once per step, on entities and on the scene");
        CHECK(counts.Scene->FindEntity("b").Get<Tilemap>().Rows == std::vector<std::string>({ "a" }), "a component without an update is left alone");

        // time is the step given, and nothing else
        SceneLoadResult timed = load(R"({"version": 1, "name": "t", "entities": [{"name": "a", "components": {"Stamp": {}}}]})");
        step(*timed.Scene, 4, InputFrame(), 0.25);
        CHECK(timed.Scene->FindEntity("a").Get<Stamp>().Elapsed == 1.0f, "dt is the step the caller gives");
        SceneLoadResult sixty = load(R"({"version": 1, "name": "t", "entities": [{"name": "a", "components": {"Stamp": {}}}]})");
        float expected = 0.0f;
        for (int i = 0; i < 60; i++) expected += (float)(1.0 / 60.0);
        step(*sixty.Scene, 60);
        CHECK(sixty.Scene->FindEntity("a").Get<Stamp>().Elapsed == expected, "sixty steps of 1/60 add up the way the same arithmetic does");
        step(*sixty.Scene, 10, InputFrame(), 0.0);
        CHECK(sixty.Scene->FindEntity("a").Get<Stamp>().Elapsed == expected, "a step of no time changes nothing");

        // moving the transform of the entity
        SceneLoadResult drifting = load(R"({"version": 1, "name": "d", "entities": [{"name": "a", "components": {"Transform": {"position": [10, 20]}, "Drift": {"velocity": [60, -30]}}}, {"name": "no_transform", "components": {"Drift": {"velocity": [1, 1]}}}]})");
        glm::vec2 position(10.0f, 20.0f);
        for (int i = 0; i < 90; i++) position += glm::vec2(60.0f, -30.0f) * (float)(1.0 / 60.0);
        step(*drifting.Scene, 90);
        CHECK(drifting.Scene->FindEntity("a").Get<Transform>().Position == position && !drifting.Scene->FindEntity("no_transform").Has<Transform>(), "a behavior moves its entity's Transform, and does nothing on one without");

        // input is the frame of the step
        SceneLoadResult keyed = load(R"({"version": 1, "name": "k", "entities": [{"name": "a", "components": {"Keyed": {}}}]})");
        Keyed& k = keyed.Scene->FindEntity("a").Get<Keyed>();
        step(*keyed.Scene, 1, InputFrame().Press(Key::W));
        CHECK(k.Presses == 1 && k.FramesDown == 1 && k.Scroll == 0.0f, "a step sees the press and the key held");
        InputFrame held; held.Hold(Key::W); held.Scroll = 2.5f;
        step(*keyed.Scene, 3, held);
        CHECK(k.Presses == 1 && k.FramesDown == 4 && k.Scroll == 7.5f, "held keys are not new presses, and the wheel adds up");
        step(*keyed.Scene, 2);
        InputFrame tap; tap.Press(Key::W).Release(Key::W);
        step(*keyed.Scene, 1, tap);
        CHECK(k.Presses == 2 && k.FramesDown == 4, "a tap counts as a press and not as held");

        // randomness is the scene's generator, in order
        SceneLoadResult dice = load(R"({"version": 1, "name": "r", "components": {"Random": {"seed": 42}}, "entities": [{"name": "d1", "components": {"Dice": {}}}, {"name": "d2", "components": {"Dice": {}}}]})");
        RandomGenerator reference(42);
        bool rolls = true;
        for (int i = 0; i < 20; i++) {
            step(*dice.Scene, 1);
            rolls = rolls && dice.Scene->FindEntity("d1").Get<Dice>().Last == reference.IntBetween(1, 6) && dice.Scene->FindEntity("d2").Get<Dice>().Last == reference.IntBetween(1, 6);
        }
        CHECK(rolls && dice.Scene->GetRandom().GetState() == reference.GetState(), "behaviors draw from the scene's generator in the order they run");

        // the previous transform, for drawing between steps
        SceneLoadResult moving = load(R"({"version": 1, "name": "m", "entities": [{"name": "a", "components": {"Transform": {"position": [0, 0], "scale": [2, 2]}, "Drift": {"velocity": [60, 0]}}}, {"name": "plain"}]})");
        Entity a = moving.Scene->FindEntity("a");
        CHECK(!a.Has<PreviousTransform>(), "before any step there is no previous transform");
        step(*moving.Scene, 1);
        CHECK(a.Has<PreviousTransform>() && a.Get<PreviousTransform>().Position == glm::vec2(0.0f) && a.Get<PreviousTransform>().Scale == glm::vec2(2.0f) && a.Get<Transform>().Position == glm::vec2(1.0f, 0.0f), "a step records where the transform was before it ran the behaviors");
        step(*moving.Scene, 1);
        CHECK(a.Get<PreviousTransform>().Position == glm::vec2(1.0f, 0.0f) && a.Get<Transform>().Position == glm::vec2(2.0f, 0.0f), "and the next step records the one after");
        CHECK(!moving.Scene->FindEntity("plain").Has<PreviousTransform>(), "an entity with no transform has none");
        moving.Scene->FindEntity("plain").Add<Transform>().Position = { 5.0f, 5.0f };
        step(*moving.Scene, 1);
        CHECK(moving.Scene->FindEntity("plain").Get<PreviousTransform>().Position == glm::vec2(5.0f), "one that gets a transform later is recorded from the next step");
        CHECK(registry.GetComponentNames(a) == std::vector<std::string>({ "Transform", "Drift" }) && !Contains(SaveSceneToText(*moving.Scene, registry).Text, "Previous") && SchemaErrors(schema, Json::parse(SaveSceneToText(*moving.Scene, registry).Text)).empty(), "it is not a registered component: not listed, not written, not in the file");
        SceneLoadResult still = load(R"({"version": 1, "name": "s", "entities": [{"name": "a", "components": {"Transform": {"position": [3, 4]}}}]})");
        uint64_t beforeStep = Simulation::Checksum(registry, *still.Scene);
        step(*still.Scene, 5);
        CHECK(still.Scene->FindEntity("a").Has<PreviousTransform>() && Simulation::Checksum(registry, *still.Scene) == beforeStep, "recording previous transforms does not change the checksum");

        // changes made during a step
        SceneLoadResult mischief = load(R"({"version": 1, "name": "x", "entities": [{"name": "killer", "components": {"Mischief": {"mode": 1, "target": "victim"}}}, {"name": "victim", "components": {"Counter": {}}}, {"name": "last", "components": {"Counter": {}}}]})");
        step(*mischief.Scene, 1);
        CHECK(!mischief.Scene->FindEntity("victim") && mischief.Scene->GetEntityCount() == 2 && mischief.Scene->FindEntity("last").Get<Counter>().Count == 1, "an entity destroyed during a step is skipped, and the others still run");
        step(*mischief.Scene, 3);
        CHECK(mischief.Scene->GetEntityCount() == 2 && mischief.Scene->FindEntity("last").Get<Counter>().Count == 4, "and destroying one that is not there is harmless");
        SceneLoadResult spawner = load(R"({"version": 1, "name": "x", "entities": [{"name": "maker", "components": {"Mischief": {"mode": 2}}}]})");
        step(*spawner.Scene, 1);
        CHECK(spawner.Scene->FindEntity("spawned0") && spawner.Scene->FindEntity("spawned0").Get<Counter>().Count == 0, "an entity created during a step does not run in it");
        step(*spawner.Scene, 1);
        CHECK(spawner.Scene->FindEntity("spawned0").Get<Counter>().Count == 1 && spawner.Scene->FindEntity("spawned1").Get<Counter>().Count == 0 && spawner.Scene->GetEntities().back().GetName() == "spawned1", "it runs from the next step, and goes last in the order");
        BehaviorContext context{ *spawner.Scene, Entity(), InputFrame(), spawner.Scene->GetRandom(), 0.0 };
        const size_t entityCount = spawner.Scene->GetEntityCount();
        registry.RunBehaviors(Entity(), context);
        CHECK(spawner.Scene->GetEntityCount() == entityCount, "running behaviors for an invalid entity does nothing");
        registry.RunBehaviors(spawner.Scene->FindEntity("maker"), context);
        CHECK(spawner.Scene->GetEntityCount() == entityCount + 1 && spawner.Scene->FindEntity("spawned2"), "and for a valid one runs just its behaviors");
    }

    // the checksum of a scene's state, and runs that must match
    {
        ComponentRegistry registry = MakeSimulationRegistry();
        auto load = [&](const std::string& text) { return LoadSceneFromText(text, "inline", registry); };
        auto step = [&](Scene& scene, int n, const InputFrame& input = InputFrame()) { for (int i = 0; i < n; i++) Simulation::Step(registry, scene, input, 1.0 / 60.0); };

        // FNV-1a 64: the published test vectors
        CHECK(Fnv1a64("", 0) == 0xcbf29ce484222325ull && Fnv1a64("a", 1) == 0xaf63dc4c8601ec8cull && Fnv1a64("foobar", 6) == 0x85944171f73967e8ull, "the hash is FNV-1a 64 bit, matching the published vectors");
        CHECK(Simulation::ChecksumText(0xbc939d54723477b5ull) == "bc939d54723477b5" && Simulation::ChecksumText(0) == "0000000000000000" && Simulation::ChecksumText(0xFFull) == "00000000000000ff", "a checksum is written as 16 hex digits");

        // a golden value, worked out apart from the engine (an independent script mixing the same pieces
        // with FNV-1a): ten steps of two counters, and the generator still at the start of seed 1
        const std::string counters = R"({"version": 1, "name": "g", "entities": [{"name": "a", "components": {"Counter": {}}}, {"name": "b", "components": {"Counter": {"every": 2}}}]})";
        SceneLoadResult golden = load(counters);
        step(*golden.Scene, 10);
        CHECK(Simulation::Checksum(registry, *golden.Scene) == 0xbc939d54723477b5ull, "the checksum of a known state is the value worked out independently");

        // equal runs, equal checksums
        SceneLoadResult one = load(counters), two = load(counters);
        step(*one.Scene, 25); step(*two.Scene, 25);
        CHECK(Simulation::Checksum(registry, *one.Scene) == Simulation::Checksum(registry, *two.Scene), "two runs of the same scene end with the same checksum");
        uint64_t atStart = Simulation::Checksum(registry, *load(counters).Scene);
        CHECK(atStart == Simulation::Checksum(registry, *load(counters).Scene) && atStart != Simulation::Checksum(registry, *one.Scene), "the checksum of a scene that has not run is the same every time, and running changes it");

        // anything that is state changes it
        uint64_t base = Simulation::Checksum(registry, *one.Scene);
        one.Scene->FindEntity("a").Get<Counter>().Count++;
        CHECK(Simulation::Checksum(registry, *one.Scene) != base, "a change to one value changes the checksum");
        one.Scene->FindEntity("a").Get<Counter>().Count--;
        CHECK(Simulation::Checksum(registry, *one.Scene) == base, "and changing it back restores it");
        one.Scene->GetSceneEntity().Add<Counter>();
        CHECK(Simulation::Checksum(registry, *one.Scene) != base, "so does a component on the scene itself");
        one.Scene->GetSceneEntity().Remove<Counter>();
        one.Scene->GetRandom().NextU32();
        CHECK(Simulation::Checksum(registry, *one.Scene) != base, "and so does the position in the random sequence");
        one.Scene->GetRandom().Seed(1);
        CHECK(Simulation::Checksum(registry, *one.Scene) == base, "which is restored by starting it again");

        SceneLoadResult swapped = load(R"({"version": 1, "name": "g", "entities": [{"name": "b", "components": {"Counter": {"every": 2}}}, {"name": "a", "components": {"Counter": {}}}]})");
        step(*swapped.Scene, 10);
        CHECK(Simulation::Checksum(registry, *swapped.Scene) != 0xbc939d54723477b5ull, "the order of entities is part of the state");
        SceneLoadResult renamed = load(R"({"version": 1, "name": "g", "entities": [{"name": "a", "components": {"Counter": {}}}, {"name": "c", "components": {"Counter": {"every": 2}}}]})");
        step(*renamed.Scene, 10);
        CHECK(Simulation::Checksum(registry, *renamed.Scene) != 0xbc939d54723477b5ull, "and so are the names");
        SceneLoadResult renamedScene = load(R"({"version": 1, "name": "another name", "entities": [{"name": "a", "components": {"Counter": {}}}, {"name": "b", "components": {"Counter": {"every": 2}}}]})");
        step(*renamedScene.Scene, 10);
        CHECK(Simulation::Checksum(registry, *renamedScene.Scene) == 0xbc939d54723477b5ull, "but what the scene is called is not");

        // seeds
        const std::string rolls = R"({"version": 1, "name": "r", "components": {"Random": {"seed": SEED}}, "entities": [{"name": "d", "components": {"Dice": {}}}]})";
        auto withSeed = [&](int seed) { std::string text = rolls; text.replace(text.find("SEED"), 4, std::to_string(seed)); return load(text); };
        SceneLoadResult s1 = withSeed(7), s2 = withSeed(7), s3 = withSeed(8);
        step(*s1.Scene, 30); step(*s2.Scene, 30); step(*s3.Scene, 30);
        CHECK(Simulation::Checksum(registry, *s1.Scene) == Simulation::Checksum(registry, *s2.Scene) && Simulation::Checksum(registry, *s1.Scene) != Simulation::Checksum(registry, *s3.Scene), "the same seed gives the same run and another seed a different one");

        // no hidden state: a scene saved halfway and loaded carries on exactly as the one that did not stop
        const std::string moving = R"({"version": 1, "name": "m", "entities": [
            {"name": "a", "components": {"Transform": {"position": [1, 2], "rotation": 0.5}, "Drift": {"velocity": [13.5, -7.25]}, "Counter": {"every": 3}, "Stamp": {}}},
            {"name": "b", "components": {"Counter": {}, "Keyed": {}}}]})";
        InputFrame keys; keys.Press(Key::W); keys.Scroll = 0.5f;
        SceneLoadResult straight = load(moving);
        step(*straight.Scene, 100, keys);

        SceneLoadResult first = load(moving);
        step(*first.Scene, 50, keys);
        SceneSaveResult saved = SaveSceneToText(*first.Scene, registry);
        SceneLoadResult resumed = load(saved.Text);
        step(*resumed.Scene, 50, keys);
        CHECK(saved.Ok() && resumed.Ok() && Simulation::Checksum(registry, *resumed.Scene) == Simulation::Checksum(registry, *straight.Scene), "a scene saved halfway, loaded and run on ends where an unbroken run does (all its state is in the scene)");

        // the same with randomness, once the position in the sequence is carried over too
        SceneLoadResult diceStraight = withSeed(99);
        step(*diceStraight.Scene, 100);
        SceneLoadResult diceFirst = withSeed(99);
        step(*diceFirst.Scene, 50);
        SceneLoadResult diceResumed = load(SaveSceneToText(*diceFirst.Scene, registry).Text);
        diceResumed.Scene->GetRandom().SetState(diceFirst.Scene->GetRandom().GetState());
        step(*diceResumed.Scene, 50);
        CHECK(Simulation::Checksum(registry, *diceResumed.Scene) == Simulation::Checksum(registry, *diceStraight.Scene), "and with random numbers when the generator's state is carried over");
        SceneLoadResult diceRestarted = load(SaveSceneToText(*diceFirst.Scene, registry).Text);
        step(*diceRestarted.Scene, 50);
        CHECK(Simulation::Checksum(registry, *diceRestarted.Scene) != Simulation::Checksum(registry, *diceStraight.Scene), "while a saved scene alone restarts the sequence from its seed, as documented");

        // stepping several scenes in turn does not mix them up
        SceneLoadResult x = load(moving), y = load(moving), reference = load(moving);
        for (int i = 0; i < 40; i++) { step(*x.Scene, 1, keys); step(*y.Scene, 1); }
        step(*reference.Scene, 40, keys);
        CHECK(Simulation::Checksum(registry, *x.Scene) == Simulation::Checksum(registry, *reference.Scene) && Simulation::Checksum(registry, *y.Scene) != Simulation::Checksum(registry, *x.Scene), "stepping two scenes alternately gives each the same result as stepping it alone");
    }

    // movement behaviors: KeyboardMove
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        auto load = [&](const std::string& entities) { return LoadSceneFromText(R"({"version": 1, "name": "t", "entities": [)" + entities + "]}", "inline", registry); };
        auto stepWith = [&](Scene& scene, const InputFrame& input, int n = 1) { for (int i = 0; i < n; i++) Simulation::Step(registry, scene, input, 1.0 / 60.0); };
        const float dt = (float)(1.0 / 60.0);
        const std::string mover = R"({"name": "p", "components": {"Transform": {"position": [100, 100]}, "KeyboardMove": {"speed": 60}}})";
        auto positionAfter = [&](const std::string& entity, const InputFrame& input, int steps = 1) {
            SceneLoadResult r = load(entity);
            stepWith(*r.Scene, input, steps);
            return r.Scene->FindEntity("p").Get<Transform>().Position;
        };

        // what it is
        const ComponentInfo* info = registry.Find("KeyboardMove");
        std::string fieldNames;
        for (const FieldInfo& f : info->Fields) fieldNames += f.Name + " ";
        CHECK(info->IsBehavior && fieldNames == "speed runMultiplier runKey keys relativeTo tintWhenRunning idleTint runTint ", "KeyboardMove is a behavior with these fields");
        CHECK(info->Fields[0].Default == 100.0 && info->Fields[1].Default == 2.0 && info->Fields[2].Default == "LeftShift" && info->Fields[3].Default == "both" && info->Fields[4].Default == "" && info->Fields[5].Default == false, "with these defaults");
        CHECK(info->Fields[0].Min == 0.0 && info->Fields[1].Min == 0.0 && info->Fields[3].OneOf == std::vector<std::string>({ "wasd", "arrows", "both" }) && info->Fields[2].OneOf.size() == KeyNames().size() + 1 && info->Fields[2].OneOf[0] == "" && info->Fields[4].EntityReference, "ranges, key names (or none) and the camera reference are described");
        for (const FieldInfo& f : info->Fields) CHECK(!f.Description.empty(), ("the field '" + f.Name + "' has a description").c_str());

        // each key moves by speed times the step, and nothing else does
        struct Case { int key; glm::vec2 direction; };
        const Case cases[] = { { Key::W, { 0, 1 } }, { Key::Up, { 0, 1 } }, { Key::S, { 0, -1 } }, { Key::Down, { 0, -1 } }, { Key::D, { 1, 0 } }, { Key::Right, { 1, 0 } }, { Key::A, { -1, 0 } }, { Key::Left, { -1, 0 } } };
        for (const Case& c : cases) {
            glm::vec2 expected = glm::vec2(100.0f, 100.0f);
            expected += c.direction * 60.0f * dt;
            CHECK(positionAfter(mover, InputFrame().Hold(c.key)) == expected, (std::string("the key ") + KeyName(c.key) + " moves one step that way").c_str());
        }
        CHECK(positionAfter(mover, InputFrame()) == glm::vec2(100.0f, 100.0f) && positionAfter(mover, InputFrame().Hold(Key::Q).Hold(Key::Space)) == glm::vec2(100.0f, 100.0f), "no key, or other keys, do not move it");

        // which keys
        const std::string arrowsOnly = R"({"name": "p", "components": {"Transform": {"position": [100, 100]}, "KeyboardMove": {"speed": 60, "keys": "arrows"}}})";
        const std::string wasdOnly = R"({"name": "p", "components": {"Transform": {"position": [100, 100]}, "KeyboardMove": {"speed": 60, "keys": "wasd"}}})";
        CHECK(positionAfter(arrowsOnly, InputFrame().Hold(Key::W).Hold(Key::A)) == glm::vec2(100.0f) && positionAfter(arrowsOnly, InputFrame().Hold(Key::Up)).y > 100.0f, "arrows only ignores W A S D");
        CHECK(positionAfter(wasdOnly, InputFrame().Hold(Key::Up).Hold(Key::Left)) == glm::vec2(100.0f) && positionAfter(wasdOnly, InputFrame().Hold(Key::W)).y > 100.0f, "wasd only ignores the arrows");

        // opposite keys, the same direction twice, and diagonals
        CHECK(positionAfter(mover, InputFrame().Hold(Key::W).Hold(Key::S)) == glm::vec2(100.0f) && positionAfter(mover, InputFrame().Hold(Key::A).Hold(Key::D)) == glm::vec2(100.0f), "opposite keys cancel");
        CHECK(positionAfter(mover, InputFrame().Hold(Key::W).Hold(Key::Up)) == positionAfter(mover, InputFrame().Hold(Key::W)), "W and Up together are one direction, not double speed");
        // (positions near 100 are only resolved to about 8e-6 in float, hence the tolerance)
        glm::vec2 diagonal = positionAfter(mover, InputFrame().Hold(Key::W).Hold(Key::D)) - glm::vec2(100.0f);
        CHECK(std::fabs(glm::length(diagonal) - 60.0f * dt) < 3e-5f && diagonal.x == diagonal.y, "a diagonal is as fast as a straight line");

        // running
        glm::vec2 walked = positionAfter(mover, InputFrame().Hold(Key::W)) - glm::vec2(100.0f), ran = positionAfter(mover, InputFrame().Hold(Key::W).Hold(Key::LeftShift)) - glm::vec2(100.0f);
        CHECK(ran.y == 60.0f * 2.0f * dt && std::fabs(ran.y / walked.y - 2.0f) < 1e-5f, "holding the run key doubles the speed by default");
        const std::string fast = R"({"name": "p", "components": {"Transform": {"position": [100, 100]}, "KeyboardMove": {"speed": 60, "runMultiplier": 3, "runKey": "Space"}}})";
        CHECK(positionAfter(fast, InputFrame().Hold(Key::W).Hold(Key::Space)).y == 100.0f + 60.0f * 3.0f * dt && positionAfter(fast, InputFrame().Hold(Key::W).Hold(Key::LeftShift)).y == 100.0f + 60.0f * dt, "the multiplier and the run key can be set");
        const std::string noRun = R"({"name": "p", "components": {"Transform": {"position": [100, 100]}, "KeyboardMove": {"speed": 60, "runKey": ""}}})";
        CHECK(positionAfter(noRun, InputFrame().Hold(Key::W).Hold(Key::LeftShift)).y == 100.0f + 60.0f * dt, "an empty run key means no running");
        CHECK(positionAfter(mover, InputFrame().Hold(Key::LeftShift)) == glm::vec2(100.0f), "running without a direction goes nowhere");
        CHECK(positionAfter(R"({"name": "p", "components": {"Transform": {"position": [100, 100]}, "KeyboardMove": {"speed": 0}}})", InputFrame().Hold(Key::W)) == glm::vec2(100.0f), "a speed of 0 does not move");
        glm::vec2 sixty = positionAfter(mover, InputFrame().Hold(Key::D), 60);
        glm::vec2 loop(100.0f, 100.0f);
        for (int i = 0; i < 60; i++) loop += glm::vec2(1.0f, 0.0f) * 60.0f * dt;
        CHECK(sixty == loop && std::fabs(sixty.x - 160.0f) < 1e-3f, "sixty steps cover sixty units at 60 units a second");

        // relative to a camera
        auto relative = [&](float rotation, const InputFrame& input) {
            SceneLoadResult r = load(R"({"name": "camera", "components": {"Transform": {"rotation": )" + std::to_string(rotation) + R"(}}}, {"name": "p", "components": {"Transform": {"position": [100, 100]}, "KeyboardMove": {"speed": 60, "relativeTo": "camera"}}})");
            stepWith(*r.Scene, input);
            return r.Scene->FindEntity("p").Get<Transform>().Position;
        };
        const float quarter = 1.5707963f;
        glm::vec2 up = glm::vec2(0.0f, 1.0f), turned = { up.x * cosf(quarter) - up.y * sinf(quarter), up.x * sinf(quarter) + up.y * cosf(quarter) };
        glm::vec2 expectedUp = glm::vec2(100.0f); expectedUp += turned * 60.0f * dt;
        CHECK(relative(quarter, InputFrame().Hold(Key::W)) == expectedUp && relative(quarter, InputFrame().Hold(Key::W)).x < 100.0f, "relative to a camera turned a quarter, W goes up the camera's screen (to the left of the world)");
        CHECK(relative(0.0f, InputFrame().Hold(Key::W)) == glm::vec2(100.0f, 100.0f + 60.0f * dt), "relative to a camera that is not turned it is the world's axes");
        glm::vec2 slant = relative(0.7f, InputFrame().Hold(Key::W).Hold(Key::D)) - glm::vec2(100.0f);
        glm::vec2 d = glm::normalize(glm::vec2(1.0f, 1.0f)), rotated = { d.x * cosf(0.7f) - d.y * sinf(0.7f), d.x * sinf(0.7f) + d.y * cosf(0.7f) };
        CHECK(std::fabs(slant.x - rotated.x * 60.0f * dt) < 1e-5f && std::fabs(slant.y - rotated.y * 60.0f * dt) < 1e-5f, "a diagonal is turned too, by any angle");
        SceneLoadResult missing = load(R"({"name": "p", "components": {"Transform": {"position": [100, 100]}, "KeyboardMove": {"speed": 60, "relativeTo": "nobody"}}})");
        stepWith(*missing.Scene, InputFrame().Hold(Key::W));
        CHECK(missing.Scene->FindEntity("p").Get<Transform>().Position == glm::vec2(100.0f, 100.0f + 60.0f * dt), "a camera that does not exist means the world's axes (the loader reports it)");
        SceneLoadResult noTransform = load(R"({"name": "camera"}, {"name": "p", "components": {"Transform": {"position": [100, 100]}, "KeyboardMove": {"speed": 60, "relativeTo": "camera"}}})");
        stepWith(*noTransform.Scene, InputFrame().Hold(Key::W));
        CHECK(noTransform.Scene->FindEntity("p").Get<Transform>().Position == glm::vec2(100.0f, 100.0f + 60.0f * dt), "so does one with no transform");

        // a camera later in the file is read as it was after the previous step
        SceneLoadResult later = load(R"({"name": "p", "components": {"Transform": {"position": [100, 100]}, "KeyboardMove": {"speed": 60, "relativeTo": "camera"}}}, {"name": "camera", "components": {"Transform": {}, "KeyboardRotate": {"speed": 1}}})");
        stepWith(*later.Scene, InputFrame().Hold(Key::W).Hold(Key::Q));
        glm::vec2 first = later.Scene->FindEntity("p").Get<Transform>().Position;
        stepWith(*later.Scene, InputFrame().Hold(Key::W).Hold(Key::Q));
        glm::vec2 second = later.Scene->FindEntity("p").Get<Transform>().Position - first;
        CHECK(first == glm::vec2(100.0f, 100.0f + 60.0f * dt) && second.x < 0.0f && std::fabs(second.x + 60.0f * dt * sinf(1.0f * dt)) < 3e-5f, "a camera later in the file is one step behind: that step uses the rotation it had before turning");

        // tinting while running
        const std::string tinted = R"({"name": "p", "components": {"Transform": {}, "SpriteRenderer": {"tint": [0.2, 0.4, 0.6, 1]}, "KeyboardMove": {"tintWhenRunning": true, "idleTint": [0.2, 0.4, 0.6, 1], "runTint": [1, 0, 0, 1]}}})";
        SceneLoadResult tint = load(tinted);
        SpriteRenderer& sprite = tint.Scene->FindEntity("p").Get<SpriteRenderer>();
        stepWith(*tint.Scene, InputFrame().Hold(Key::LeftShift));
        CHECK(sprite.Tint == glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), "the run tint is set while running, even standing still");
        stepWith(*tint.Scene, InputFrame().Hold(Key::W));
        CHECK(sprite.Tint == glm::vec4(0.2f, 0.4f, 0.6f, 1.0f), "and the idle tint when not");
        SceneLoadResult plain = load(R"({"name": "p", "components": {"Transform": {}, "SpriteRenderer": {"tint": [0.2, 0.4, 0.6, 1]}, "KeyboardMove": {}}})");
        stepWith(*plain.Scene, InputFrame().Hold(Key::LeftShift));
        CHECK(plain.Scene->FindEntity("p").Get<SpriteRenderer>().Tint == glm::vec4(0.2f, 0.4f, 0.6f, 1.0f), "without tintWhenRunning the sprite's tint is left alone");
        SceneLoadResult bare = load(R"({"name": "p", "components": {"KeyboardMove": {"tintWhenRunning": true}}})");
        stepWith(*bare.Scene, InputFrame().Hold(Key::W).Hold(Key::LeftShift), 3);
        CHECK(!bare.Scene->FindEntity("p").Has<Transform>() && !bare.Scene->FindEntity("p").Has<SpriteRenderer>(), "an entity without a Transform or a sprite is left as it is");

        // what the loader says
        auto problems = [&](const std::string& fields) { return LoadSceneFromText(R"({"version": 1, "name": "t", "entities": [{"name": "p", "components": {"KeyboardMove": {)" + fields + "}}}]}", "inline", registry); };
        const std::string at = "/entities/0/components/KeyboardMove";
        CHECK(HasProblem(problems(R"("speed": -1)"), at + "/speed", "must be at least 0") && HasProblem(problems(R"("speed": 1000000)"), at + "/speed", "must be at most 100000") && HasProblem(problems(R"("runMultiplier": -2)"), at + "/runMultiplier", "must be at least 0"), "negative or huge speeds are refused");
        CHECK(HasProblem(problems(R"("runKey": "LeftShft")"), at + "/runKey", "did you mean 'LeftShift'?") && HasProblem(problems(R"("runKey": "LeftShft")"), at + "/runKey", "the " + std::to_string(KeyNames().size() + 1) + " allowed values") && problems(R"("runKey": "")").Ok(), "a mistyped run key is suggested, and empty is allowed");
        CHECK(HasProblem(problems(R"("keys": "wasdd")"), at + "/keys", "expected one of 'wasd', 'arrows', 'both', got 'wasdd' (did you mean 'wasd'?)") && HasProblem(problems(R"("tintWhenRunning": "yes")"), at + "/tintWhenRunning", "expected true or false"), "the key set and flags are checked");
        CHECK(HasProblem(problems(R"("idleTint": [2, 0, 0, 1])"), at + "/idleTint/0", "must be at most 1") && HasProblem(problems(R"("relativeTo": "cam")"), at + "/relativeTo", "no entity named 'cam' exists"), "tints are in 0 to 1, and the camera must exist");
        const std::string golden = R"("KeyboardMove": { "speed": 100, "runMultiplier": 2, "runKey": "LeftShift", "keys": "both", "relativeTo": "", "tintWhenRunning": false, "idleTint": [1, 1, 1, 1], "runTint": [1, 0.65, 0.3, 1] })";
        SceneLoadResult written = problems("");
        CHECK(written.Ok() && Contains(SaveSceneToText(*written.Scene, registry).Text, golden), "it is written with every field, in this order");
    }

    // movement behaviors: KeyboardRotate, WheelZoom, ClampPosition
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        auto load = [&](const std::string& entities) { return LoadSceneFromText(R"({"version": 1, "name": "t", "entities": [)" + entities + "]}", "inline", registry); };
        auto stepWith = [&](Scene& scene, const InputFrame& input, int n = 1) { for (int i = 0; i < n; i++) Simulation::Step(registry, scene, input, 1.0 / 60.0); };
        const float dt = (float)(1.0 / 60.0);
        auto problems = [&](const std::string& component, const std::string& fields) { return LoadSceneFromText(R"({"version": 1, "name": "t", "entities": [{"name": "p", "components": {")" + component + R"(": {)" + fields + "}}}]}", "inline", registry); };

        // KeyboardRotate
        const ComponentInfo* rotateInfo = registry.Find("KeyboardRotate");
        CHECK(rotateInfo->IsBehavior && rotateInfo->Fields.size() == 3 && rotateInfo->Fields[0].Default == 1.0 && rotateInfo->Fields[1].Default == "Q" && rotateInfo->Fields[2].Default == "E" && rotateInfo->Fields[1].OneOf == KeyNames() && rotateInfo->Fields[0].Min == 0.0, "KeyboardRotate has a speed and two keys, Q and E by default");
        const std::string turner = R"({"name": "p", "components": {"Transform": {}, "KeyboardRotate": {"speed": 2}}})";
        SceneLoadResult turning = load(turner);
        Transform& turned = turning.Scene->FindEntity("p").Get<Transform>();
        float expected = 0.0f;
        for (int i = 0; i < 90; i++) expected += 2.0f * dt;
        stepWith(*turning.Scene, InputFrame().Hold(Key::Q), 90);
        CHECK(turned.Rotation == expected && expected > 2.9f, "Q turns counter-clockwise by the speed times the step, every step");
        stepWith(*turning.Scene, InputFrame().Hold(Key::E), 90);
        CHECK(std::fabs(turned.Rotation) < 1e-5f, "E turns it back the other way");
        float before = turned.Rotation;
        stepWith(*turning.Scene, InputFrame().Hold(Key::Q).Hold(Key::E), 10);
        CHECK(turned.Rotation == before, "both keys together are exactly no turn");
        stepWith(*turning.Scene, InputFrame().Hold(Key::A).Hold(Key::W), 10);
        CHECK(turned.Rotation == before, "other keys do not turn it");
        SceneLoadResult keys = load(R"({"name": "p", "components": {"Transform": {}, "KeyboardRotate": {"speed": 1, "counterClockwiseKey": "Left", "clockwiseKey": "Right"}}})");
        stepWith(*keys.Scene, InputFrame().Hold(Key::Left));
        CHECK(keys.Scene->FindEntity("p").Get<Transform>().Rotation == 1.0f * dt, "the keys can be chosen");
        stepWith(*keys.Scene, InputFrame().Hold(Key::Q));
        CHECK(keys.Scene->FindEntity("p").Get<Transform>().Rotation == 1.0f * dt, "and then the default ones do nothing");
        SceneLoadResult still = load(R"({"name": "p", "components": {"Transform": {}, "KeyboardRotate": {"speed": 0}}})");
        stepWith(*still.Scene, InputFrame().Hold(Key::Q), 5);
        CHECK(still.Scene->FindEntity("p").Get<Transform>().Rotation == 0.0f, "a speed of 0 does not turn");
        SceneLoadResult noTransform = load(R"({"name": "p", "components": {"KeyboardRotate": {}}})");
        stepWith(*noTransform.Scene, InputFrame().Hold(Key::Q), 3);
        CHECK(!noTransform.Scene->FindEntity("p").Has<Transform>(), "without a Transform there is nothing to turn");
        CHECK(HasProblem(problems("KeyboardRotate", R"("counterClockwiseKey": "Q ")"), "/entities/0/components/KeyboardRotate/counterClockwiseKey", "did you mean 'Q'?") && HasProblem(problems("KeyboardRotate", R"("clockwiseKey": "")"), "/entities/0/components/KeyboardRotate/clockwiseKey", "got ''") && HasProblem(problems("KeyboardRotate", R"("speed": -1)"), "/entities/0/components/KeyboardRotate/speed", "must be at least 0"), "mistyped keys and negative speeds are refused");

        // WheelZoom
        const ComponentInfo* zoomInfo = registry.Find("WheelZoom");
        CHECK(zoomInfo->IsBehavior && zoomInfo->Fields.size() == 3 && zoomInfo->Fields[0].Default == 1.1 && zoomInfo->Fields[1].Default == 0.25 && zoomInfo->Fields[2].Default == 4.0 && zoomInfo->Fields[0].Min == 0.01 && zoomInfo->Fields[0].Max == 10.0, "WheelZoom has a factor and a range of zoom");
        const std::string zoomer = R"({"name": "p", "components": {"Camera": {}, "WheelZoom": {}}})";
        SceneLoadResult zooming = load(zoomer);
        Camera& camera = zooming.Scene->FindEntity("p").Get<Camera>();
        InputFrame notch; notch.Scroll = 1.0f;
        stepWith(*zooming.Scene, notch);
        CHECK(camera.Zoom == powf(1.1f, 1.0f), "one notch up multiplies the zoom by the factor");
        InputFrame down; down.Scroll = -1.0f;
        stepWith(*zooming.Scene, down);
        CHECK(std::fabs(camera.Zoom - 1.0f) < 1e-6f, "one notch down undoes it");
        float held = camera.Zoom;
        stepWith(*zooming.Scene, InputFrame(), 10);
        CHECK(camera.Zoom == held, "a step with no wheel movement leaves the zoom alone");
        InputFrame fraction; fraction.Scroll = 2.5f;
        SceneLoadResult fractional = load(zoomer);
        stepWith(*fractional.Scene, fraction);
        CHECK(fractional.Scene->FindEntity("p").Get<Camera>().Zoom == powf(1.1f, 2.5f), "several notches in a step, or part of one, are worked out together");
        SceneLoadResult clamped = load(zoomer);
        InputFrame many; many.Scroll = 50.0f;
        stepWith(*clamped.Scene, many);
        CHECK(clamped.Scene->FindEntity("p").Get<Camera>().Zoom == 4.0f, "zooming in stops at the largest zoom");
        many.Scroll = -100.0f;
        stepWith(*clamped.Scene, many);
        CHECK(clamped.Scene->FindEntity("p").Get<Camera>().Zoom == 0.25f, "and zooming out at the smallest");
        SceneLoadResult custom = load(R"({"name": "p", "components": {"Camera": {"zoom": 1}, "WheelZoom": {"factor": 2, "minZoom": 0.5, "maxZoom": 3}}})");
        stepWith(*custom.Scene, notch);
        CHECK(custom.Scene->FindEntity("p").Get<Camera>().Zoom == 2.0f, "the factor can be set");
        stepWith(*custom.Scene, notch);
        CHECK(custom.Scene->FindEntity("p").Get<Camera>().Zoom == 3.0f, "and the largest zoom held to");
        SceneLoadResult outside = load(R"({"name": "p", "components": {"Camera": {"zoom": 10}, "WheelZoom": {}}})");
        stepWith(*outside.Scene, InputFrame(), 3);
        CHECK(outside.Scene->FindEntity("p").Get<Camera>().Zoom == 10.0f, "a zoom outside the range stays until the wheel moves");
        stepWith(*outside.Scene, down);
        CHECK(outside.Scene->FindEntity("p").Get<Camera>().Zoom == 4.0f, "and is then held to it");
        SceneLoadResult otherCamera = load(R"({"name": "p", "components": {"WheelZoom": {}}}, {"name": "q", "components": {"Camera": {}}})");
        stepWith(*otherCamera.Scene, notch);
        CHECK(otherCamera.Scene->FindEntity("q").Get<Camera>().Zoom == 1.0f, "it zooms its own entity's camera and no other");
        CHECK(HasProblem(problems("WheelZoom", R"("minZoom": 2, "maxZoom": 1)"), "/entities/0/components/WheelZoom/maxZoom", "maxZoom (1.0) must not be smaller than minZoom (2.0)") && problems("WheelZoom", R"("minZoom": 2, "maxZoom": 2)").Ok(), "a largest zoom below the smallest is refused, an equal one is fine");
        CHECK(HasProblem(problems("WheelZoom", R"("factor": 0)"), "/entities/0/components/WheelZoom/factor", "must be at least 0.01") && HasProblem(problems("WheelZoom", R"("factor": 11)"), "/entities/0/components/WheelZoom/factor", "must be at most 10") && HasProblem(problems("WheelZoom", R"("minZoom": 0)"), "/entities/0/components/WheelZoom/minZoom", "must be at least 0.01") && HasProblem(problems("WheelZoom", R"("maxZoom": 101)"), "/entities/0/components/WheelZoom/maxZoom", "must be at most 100"), "the factor and the range are held to sensible values");

        // ClampPosition
        const ComponentInfo* clampInfo = registry.Find("ClampPosition");
        CHECK(clampInfo->IsBehavior && clampInfo->Fields.size() == 2 && clampInfo->Fields[0].Default == Json::array({ 0.0, 0.0 }) && clampInfo->Fields[1].Default == Json::array({ 100.0, 100.0 }), "ClampPosition has a rectangle, 0 to 100 by default");
        auto clampedTo = [&](const std::string& position, const std::string& box) {
            SceneLoadResult r = load(R"({"name": "p", "components": {"Transform": {"position": )" + position + R"(}, "ClampPosition": )" + box + "}}");
            stepWith(*r.Scene, InputFrame());
            return r.Scene->FindEntity("p").Get<Transform>().Position;
        };
        const std::string box = R"({"min": [10, 20], "max": [50, 60]})";
        CHECK(clampedTo("[30, 40]", box) == glm::vec2(30.0f, 40.0f) && clampedTo("[10, 60]", box) == glm::vec2(10.0f, 60.0f), "a position inside the rectangle, or on its edge, is left alone");
        CHECK(clampedTo("[0, 40]", box) == glm::vec2(10.0f, 40.0f) && clampedTo("[99, 40]", box) == glm::vec2(50.0f, 40.0f) && clampedTo("[30, -5]", box) == glm::vec2(30.0f, 20.0f) && clampedTo("[30, 500]", box) == glm::vec2(30.0f, 60.0f), "one that is outside is brought to the nearest edge, on each axis");
        CHECK(clampedTo("[-100, 1000]", box) == glm::vec2(10.0f, 60.0f), "and to the corner when it is outside on both");
        CHECK(clampedTo("[7, 7]", R"({"min": [5, 5], "max": [5, 5]})") == glm::vec2(5.0f, 5.0f) && clampedTo("[-50, -50]", R"({"min": [-100, -100], "max": [-10, -10]})") == glm::vec2(-50.0f, -50.0f) && clampedTo("[-5, -5]", R"({"min": [-100, -100], "max": [-10, -10]})") == glm::vec2(-10.0f, -10.0f), "an empty rectangle pins it, and negative ones work");
        CHECK(clampedTo("[200, 200]", "{}") == glm::vec2(100.0f, 100.0f), "the default rectangle is 0 to 100");
        SceneLoadResult noPosition = load(R"({"name": "p", "components": {"ClampPosition": {}}})");
        stepWith(*noPosition.Scene, InputFrame());
        CHECK(!noPosition.Scene->FindEntity("p").Has<Transform>(), "without a Transform there is nothing to clamp");
        CHECK(HasProblem(problems("ClampPosition", R"("min": [5, 0], "max": [1, 100])"), "/entities/0/components/ClampPosition/max/0", "max x (1.0) must not be smaller than min x (5.0)") && HasProblem(problems("ClampPosition", R"("min": [0, 9], "max": [100, 2])"), "/entities/0/components/ClampPosition/max/1", "max y (2.0) must not be smaller than min y (9.0)") && problems("ClampPosition", R"("min": [3, 3], "max": [3, 3])").Ok(), "a corner the wrong way round is refused, on each axis");
        SceneLoadResult both = problems("ClampPosition", R"("min": [5, 9], "max": [1, 2])");
        CHECK(HasProblem(both, "/entities/0/components/ClampPosition/max/0", "max x") && HasProblem(both, "/entities/0/components/ClampPosition/max/1", "max y") && !both.Scene->FindEntity("p").Has<ClampPosition>(), "both axes are reported, and the component is skipped");

        // moving and clamping on one entity: the clamp comes after the move (registration order)
        SceneLoadResult fenced = load(R"({"name": "p", "components": {"Transform": {"position": [99, 50]}, "ClampPosition": {"min": [0, 0], "max": [100, 100]}, "KeyboardMove": {"speed": 600}}})");
        stepWith(*fenced.Scene, InputFrame().Hold(Key::D), 5);
        CHECK(fenced.Scene->FindEntity("p").Get<Transform>().Position == glm::vec2(100.0f, 50.0f), "an entity pushed against its fence stops at it, whatever order the two are written in");

        // the same as the movement code in the Sandbox, step for step
        struct OldSandbox {
            glm::vec2 player = { 1024.0f, 518.0f };
            float cameraRotation = 0.0f, zoom = 1.0f;
            glm::vec4 tint = { 1.0f, 1.0f, 1.0f, 1.0f };
            void Step(const InputFrame& in, double dtSeconds)
            {
                glm::vec2 move(0.0f);
                if (in.IsDown(Key::W) || in.IsDown(Key::Up))    move.y += 1.0f;
                if (in.IsDown(Key::S) || in.IsDown(Key::Down))  move.y -= 1.0f;
                if (in.IsDown(Key::D) || in.IsDown(Key::Right)) move.x += 1.0f;
                if (in.IsDown(Key::A) || in.IsDown(Key::Left))  move.x -= 1.0f;
                if (in.IsDown(Key::Q)) cameraRotation += 1.0f * (float)dtSeconds;
                if (in.IsDown(Key::E)) cameraRotation -= 1.0f * (float)dtSeconds;
                if (move != glm::vec2(0.0f)) {
                    float c = cosf(cameraRotation), s = sinf(cameraRotation);
                    glm::vec2 dir = glm::normalize(move);
                    dir = { dir.x * c - dir.y * s, dir.x * s + dir.y * c };
                    float speed = 300.0f * (in.IsDown(Key::LeftShift) ? 2.0f : 1.0f);
                    player += dir * speed * (float)dtSeconds;
                }
                const float margin = 64.0f + 48.0f * 0.5f;
                player.x = glm::clamp(player.x, margin, 32 * 64.0f - margin);
                player.y = glm::clamp(player.y, margin, 24 * 64.0f - margin);
                if (in.Scroll != 0.0f) zoom = glm::clamp(zoom * powf(1.1f, in.Scroll), 0.25f, 4.0f);
                tint = in.IsDown(Key::LeftShift) ? glm::vec4(1.0f, 0.65f, 0.3f, 1.0f) : glm::vec4(1.0f);
            }
        };
        const std::string world = R"({"version": 1, "name": "w", "entities": [
            {"name": "camera", "components": {"Transform": {}, "Camera": {"primary": true}, "KeyboardRotate": {"speed": 1}, "WheelZoom": {}}},
            {"name": "player", "components": {"Transform": {"position": [1024, 518], "scale": [48, 48]}, "SpriteRenderer": {},
                "KeyboardMove": {"speed": 300, "runMultiplier": 2, "relativeTo": "camera", "tintWhenRunning": true},
                "ClampPosition": {"min": [88, 88], "max": [1960, 1448]}}}]})";
        SceneLoadResult now = LoadSceneFromText(world, "world", registry);
        CHECK(now.Ok(), "the scene that replaces the Sandbox's movement code loads with no problems");
        // keys, running, turning and the wheel, chosen by a seeded generator so the script is always the same
        auto scriptedFrame = [](RandomGenerator& script) { return ScriptedFrame(script); };

        OldSandbox then;
        RandomGenerator script(5);
        bool identical = true, sawMove = false, sawWall = false, sawZoom = false, sawTurn = false, sawRun = false;
        glm::vec2 lastPlayer = then.player;
        for (int i = 0; i < 600 && identical; i++) {
            InputFrame in = scriptedFrame(script);
            then.Step(in, 1.0 / 60.0);
            Simulation::Step(registry, *now.Scene, in, 1.0 / 60.0);

            Entity player = now.Scene->FindEntity("player"), cam = now.Scene->FindEntity("camera");
            identical = player.Get<Transform>().Position == then.player && cam.Get<Transform>().Rotation == then.cameraRotation && cam.Get<Camera>().Zoom == then.zoom && player.Get<SpriteRenderer>().Tint == then.tint;
            sawMove = sawMove || then.player != lastPlayer;
            lastPlayer = then.player;
            sawWall = sawWall || then.player.x == 88.0f || then.player.y == 88.0f || then.player.x == 1960.0f || then.player.y == 1448.0f;
            sawZoom = sawZoom || then.zoom != 1.0f;
            sawTurn = sawTurn || then.cameraRotation != 0.0f;
            sawRun = sawRun || then.tint != glm::vec4(1.0f);
            if (!identical) printf("      differs at step %d: player %.6f,%.6f vs %.6f,%.6f\n", i, player.Get<Transform>().Position.x, player.Get<Transform>().Position.y, then.player.x, then.player.y);
        }
        CHECK(identical, "600 scripted steps of keys, running, turning and wheel give exactly the same player, camera, zoom and tint as the Sandbox's old code");
        CHECK(sawMove && sawTurn && sawZoom && sawRun && sawWall, "and the script really moves, turns, zooms, runs and drives the player into the walls");

        // the same script played again, on a fresh scene, ends in the same state
        SceneLoadResult second = LoadSceneFromText(world, "world", registry);
        RandomGenerator script2(5);
        for (int i = 0; i < 600; i++) Simulation::Step(registry, *second.Scene, scriptedFrame(script2), 1.0 / 60.0);
        CHECK(Simulation::Checksum(registry, *second.Scene) == Simulation::Checksum(registry, *now.Scene), "the same script played again ends in the same state (same checksum)");
    }

    // motion behaviors: Follow, Spin, Orbit, Wander
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        auto load = [&](const std::string& entities, const std::string& scene = "") { return LoadSceneFromText(R"({"version": 1, "name": "t", )" + scene + R"("entities": [)" + entities + "]}", "inline", registry); };
        auto stepWith = [&](Scene& scene, const InputFrame& input, int n = 1) { for (int i = 0; i < n; i++) Simulation::Step(registry, scene, input, 1.0 / 60.0); };
        const float dt = (float)(1.0 / 60.0);
        auto problems = [&](const std::string& component, const std::string& fields) { return LoadSceneFromText(R"({"version": 1, "name": "t", "entities": [{"name": "p", "components": {")" + component + R"(": {)" + fields + "}}}, {\"name\": \"q\"}]}", "inline", registry); };
        auto positionOf = [](const SceneLoadResult& r, const std::string& name) { return r.Scene->FindEntity(name).Get<Transform>().Position; };

        // ---- Follow
        const ComponentInfo* followInfo = registry.Find("Follow");
        CHECK(followInfo->IsBehavior && followInfo->Fields.size() == 2 && followInfo->Fields[0].Name == "target" && followInfo->Fields[0].EntityReference && followInfo->Fields[1].Default == Json::array({ 0.0, 0.0 }), "Follow is a behavior with a target entity and an offset");
        const std::string walker = R"({"name": "player", "components": {"Transform": {"position": [10, 20]}, "KeyboardMove": {"speed": 60}}})";
        SceneLoadResult following = load(walker + R"(, {"name": "shadow", "components": {"Transform": {}, "Follow": {"target": "player", "offset": [3, -4]}}})");
        stepWith(*following.Scene, InputFrame().Hold(Key::D), 5);
        glm::vec2 expectedShadow = positionOf(following, "player") + glm::vec2(3.0f, -4.0f);
        CHECK(positionOf(following, "shadow") == expectedShadow && positionOf(following, "player").x > 10.0f, "an entity after its target is at the target's position plus the offset every step");
        SceneLoadResult behind = load(R"({"name": "shadow", "components": {"Transform": {}, "Follow": {"target": "player", "offset": [3, -4]}}}, )" + walker);
        stepWith(*behind.Scene, InputFrame().Hold(Key::D), 1);
        glm::vec2 firstStep = positionOf(behind, "player");
        CHECK(positionOf(behind, "shadow") == glm::vec2(10.0f, 20.0f) + glm::vec2(3.0f, -4.0f), "one that comes before its target is a step behind, at where the target was");
        stepWith(*behind.Scene, InputFrame().Hold(Key::D), 1);
        CHECK(positionOf(behind, "shadow") == firstStep + glm::vec2(3.0f, -4.0f), "and then at where it was a step ago");
        SceneLoadResult chain = load(walker + R"(, {"name": "a", "components": {"Transform": {}, "Follow": {"target": "player", "offset": [1, 0]}}}, {"name": "b", "components": {"Transform": {}, "Follow": {"target": "a", "offset": [0, 2]}}})");
        stepWith(*chain.Scene, InputFrame().Hold(Key::W), 3);
        CHECK(positionOf(chain, "b") == positionOf(chain, "a") + glm::vec2(0.0f, 2.0f) && positionOf(chain, "a") == positionOf(chain, "player") + glm::vec2(1.0f, 0.0f), "a chain of followers, each after the one it follows, is all in step");

        SceneLoadResult camera = load(walker + R"(, {"name": "camera", "components": {"Transform": {}, "Camera": {"primary": true}, "Follow": {"target": "player"}}})");
        stepWith(*camera.Scene, InputFrame().Hold(Key::D).Hold(Key::W), 4);
        Entity cam = camera.Scene->FindEntity("camera"), player = camera.Scene->FindEntity("player");
        CHECK(cam.Get<Transform>().Position == player.Get<Transform>().Position && cam.Get<PreviousTransform>().Position == player.Get<PreviousTransform>().Position, "a camera that follows the player, after it, is where the player is and was, so it is drawn between steps the same way");

        const std::string startAt = R"("Transform": {"position": [7, 8]})";
        SceneLoadResult stays = load(R"({"name": "none", "components": {)" + startAt + R"(, "Follow": {"target": "ghost", "offset": [5, 5]}}}, {"name": "empty", "components": {)" + startAt + R"(, "Follow": {}}}, {"name": "self", "components": {)" + startAt + R"(, "Follow": {"target": "self", "offset": [5, 5]}}}, {"name": "bare"}, {"name": "no_position", "components": {)" + startAt + R"(, "Follow": {"target": "bare"}}}, {"name": "no_transform", "components": {"Follow": {"target": "none"}}})");
        stepWith(*stays.Scene, InputFrame(), 3);
        CHECK(positionOf(stays, "none") == glm::vec2(7.0f, 8.0f) && positionOf(stays, "empty") == glm::vec2(7.0f, 8.0f) && positionOf(stays, "self") == glm::vec2(7.0f, 8.0f) && positionOf(stays, "no_position") == glm::vec2(7.0f, 8.0f) && !stays.Scene->FindEntity("no_transform").Has<Transform>(), "with no target, a missing one, itself, or one without a Transform, an entity stays where it is");
        CHECK(HasProblem(problems("Follow", R"("target": "nobody")"), "/entities/0/components/Follow/target", "no entity named 'nobody' exists") && HasProblem(problems("Follow", R"("offset": [1])"), "/entities/0/components/Follow/offset", "expected an array of 2 numbers") && problems("Follow", R"("target": "q", "offset": [1, 2])").Ok(), "a missing target and a bad offset are reported");
        CHECK(Contains(SaveSceneToText(*problems("Follow", R"("target": "q", "offset": [3, -4])").Scene, registry).Text, R"("Follow": { "target": "q", "offset": [3, -4] })"), "it is written with its target and offset");

        // ---- Spin
        const ComponentInfo* spinInfo = registry.Find("Spin");
        CHECK(spinInfo->IsBehavior && spinInfo->Fields.size() == 1 && spinInfo->Fields[0].Default == 1.0 && spinInfo->Fields[0].Min == -1000.0 && spinInfo->Fields[0].Max == 1000.0, "Spin has a speed in radians per second");
        SceneLoadResult spinning = load(R"({"name": "a", "components": {"Transform": {"rotation": 0.5}, "Spin": {"speed": 2}}}, {"name": "b", "components": {"Transform": {}, "Spin": {"speed": -3}}}, {"name": "c", "components": {"Transform": {"rotation": 1}, "Spin": {"speed": 0}}}, {"name": "d", "components": {"Spin": {}}})");
        float a = 0.5f, b = 0.0f;
        for (int i = 0; i < 90; i++) { a += 2.0f * dt; b += -3.0f * dt; }
        stepWith(*spinning.Scene, InputFrame(), 90);
        CHECK(spinning.Scene->FindEntity("a").Get<Transform>().Rotation == a && spinning.Scene->FindEntity("b").Get<Transform>().Rotation == b && spinning.Scene->FindEntity("c").Get<Transform>().Rotation == 1.0f, "an entity turns by speed times the step, from the rotation it has, either way, and not at all at speed 0");
        CHECK(!spinning.Scene->FindEntity("d").Has<Transform>(), "one without a Transform has nothing to turn");
        CHECK(HasProblem(problems("Spin", R"("speed": 1001)"), "/entities/0/components/Spin/speed", "must be at most 1000") && HasProblem(problems("Spin", R"("speed": -1001)"), "/entities/0/components/Spin/speed", "must be at least -1000"), "absurd speeds are refused");

        // ---- Orbit
        const ComponentInfo* orbitInfo = registry.Find("Orbit");
        std::string orbitFields;
        for (const FieldInfo& f : orbitInfo->Fields) orbitFields += f.Name + " ";
        CHECK(orbitInfo->IsBehavior && orbitFields == "target radius speed angle rotationSpeed " && orbitInfo->Fields[0].EntityReference && orbitInfo->Fields[1].Default == 100.0 && orbitInfo->Fields[2].Default == 1.0 && orbitInfo->Fields[3].Default == 0.0 && orbitInfo->Fields[4].Default == 0.0 && orbitInfo->Fields[1].Min == 0.0, "Orbit has a target, a radius, a speed, an angle and a turn");

        // the satellite of the old Sandbox: radius 44, 2 radians a second, turning three times as fast
        SceneLoadResult satellite = load(walker + R"(, {"name": "satellite", "components": {"Transform": {}, "Orbit": {"target": "player", "radius": 44, "speed": 2, "rotationSpeed": 6}}})");
        float spin = 0.0f, rotation = 0.0f;
        bool sameOrbit = true, sameAngle = true, nearOldRotation = true;
        for (int i = 0; i < 120; i++) {
            stepWith(*satellite.Scene, InputFrame().Hold(Key::D).Hold(Key::W));
            spin += 2.0f * dt;              // exactly what the Sandbox did
            rotation += 6.0f * dt;
            glm::vec2 old = positionOf(satellite, "player") + 44.0f * glm::vec2(cosf(spin), sinf(spin));
            sameOrbit = sameOrbit && positionOf(satellite, "satellite") == old;
            sameAngle = sameAngle && satellite.Scene->FindEntity("satellite").Get<Orbit>().Angle == spin;
            nearOldRotation = nearOldRotation && satellite.Scene->FindEntity("satellite").Get<Transform>().Rotation == rotation && std::fabs(rotation - spin * 3.0f) < 2e-5f;
        }
        CHECK(sameOrbit && sameAngle, "an orbit is the old satellite's position and angle, exactly, around a target that is moving");
        CHECK(nearOldRotation, "and it turns as it goes round (the old satellite's rotation, to rounding)");

        SceneLoadResult ring = load(R"({"name": "post", "components": {"Transform": {"position": [100, 100]}}}, {"name": "light", "components": {"Transform": {"rotation": 1}, "Orbit": {"target": "post", "radius": 30, "speed": 1.5, "angle": 1.5707963}}})");
        stepWith(*ring.Scene, InputFrame());
        float startAngle = 1.5707963f + 1.5f * dt;
        CHECK(positionOf(ring, "light") == glm::vec2(100.0f, 100.0f) + 30.0f * glm::vec2(cosf(startAngle), sinf(startAngle)) && ring.Scene->FindEntity("light").Get<Transform>().Rotation == 1.0f, "the angle field says where to start, and a rotationSpeed of 0 keeps the rotation");
        bool onCircle = true;
        for (int i = 0; i < 200; i++) { stepWith(*ring.Scene, InputFrame()); onCircle = onCircle && std::fabs(glm::length(positionOf(ring, "light") - positionOf(ring, "post")) - 30.0f) < 1e-3f; }
        CHECK(onCircle, "it stays at the radius from the target");
        SceneLoadResult backwards = load(R"({"name": "post", "components": {"Transform": {}}}, {"name": "light", "components": {"Transform": {}, "Orbit": {"target": "post", "radius": 10, "speed": -2}}})");
        stepWith(*backwards.Scene, InputFrame(), 30);
        CHECK(backwards.Scene->FindEntity("light").Get<Orbit>().Angle < -0.9f && positionOf(backwards, "light").y < 0.0f, "a negative speed goes the other way");
        SceneLoadResult centered = load(R"({"name": "post", "components": {"Transform": {"position": [5, 6]}}}, {"name": "light", "components": {"Transform": {"position": [50, 50]}, "Orbit": {"target": "post", "radius": 0}}})");
        stepWith(*centered.Scene, InputFrame());
        CHECK(positionOf(centered, "light") == glm::vec2(5.0f, 6.0f), "a radius of 0 is at the target");

        SceneLoadResult before = load(R"({"name": "light", "components": {"Transform": {}, "Orbit": {"target": "post", "radius": 10, "speed": 0}}}, {"name": "post", "components": {"Transform": {"position": [100, 0]}, "KeyboardMove": {"speed": 60}}})");
        stepWith(*before.Scene, InputFrame().Hold(Key::D));
        CHECK(positionOf(before, "post").x > 100.5f && positionOf(before, "light") == glm::vec2(110.0f, 0.0f), "one before its target goes round where the target was at the start of the step");
        glm::vec2 postAfterFirst = positionOf(before, "post");
        stepWith(*before.Scene, InputFrame().Hold(Key::D));
        CHECK(positionOf(before, "light") == postAfterFirst + glm::vec2(10.0f, 0.0f), "and the next step, where it was after the first: always a step behind");
        SceneLoadResult idle = load(R"({"name": "a", "components": {"Transform": {"position": [1, 1]}, "Orbit": {"target": "ghost"}}}, {"name": "b", "components": {"Transform": {"position": [1, 1]}, "Orbit": {"target": "b"}}}, {"name": "c", "components": {"Transform": {"position": [1, 1]}, "Orbit": {}}}, {"name": "bare"}, {"name": "d", "components": {"Transform": {"position": [1, 1]}, "Orbit": {"target": "bare"}}})");
        stepWith(*idle.Scene, InputFrame(), 10);
        bool notMoved = true;
        for (const char* name : { "a", "b", "c", "d" }) notMoved = notMoved && positionOf(idle, name) == glm::vec2(1.0f) && idle.Scene->FindEntity(name).Get<Orbit>().Angle == 0.0f;
        CHECK(notMoved, "with no target, a missing one, itself or one without a Transform it does not move, and its angle does not advance");
        CHECK(HasProblem(problems("Orbit", R"("target": "nobody")"), "/entities/0/components/Orbit/target", "no entity named 'nobody' exists") && HasProblem(problems("Orbit", R"("radius": -1)"), "/entities/0/components/Orbit/radius", "must be at least 0") && HasProblem(problems("Orbit", R"("speed": 2000)"), "/entities/0/components/Orbit/speed", "must be at most 1000") && HasProblem(problems("Orbit", R"("rotationSpeed": -2000)"), "/entities/0/components/Orbit/rotationSpeed", "must be at least -1000"), "a missing target and absurd numbers are reported");

        // the angle is state, so it is saved, and a saved scene carries on where it was
        const std::string orbiting = R"({"name": "post", "components": {"Transform": {"position": [50, 50]}}}, {"name": "light", "components": {"Transform": {}, "Orbit": {"target": "post", "radius": 20, "speed": 3, "rotationSpeed": 1}}})";
        SceneLoadResult orbitStraight = load(orbiting);
        stepWith(*orbitStraight.Scene, InputFrame(), 100);
        SceneLoadResult orbitFirst = load(orbiting);
        stepWith(*orbitFirst.Scene, InputFrame(), 50);
        SceneSaveResult orbitSaved = SaveSceneToText(*orbitFirst.Scene, registry);
        SceneLoadResult orbitResumed = LoadSceneFromText(orbitSaved.Text, "saved", registry);
        stepWith(*orbitResumed.Scene, InputFrame(), 50);
        CHECK(orbitSaved.Ok() && orbitFirst.Scene->FindEntity("light").Get<Orbit>().Angle > 2.0f && Simulation::Checksum(registry, *orbitResumed.Scene) == Simulation::Checksum(registry, *orbitStraight.Scene), "an orbit saved halfway and loaded carries on exactly as one that did not stop");

        // ---- Wander
        const ComponentInfo* wanderInfo = registry.Find("Wander");
        std::string wanderFields;
        for (const FieldInfo& f : wanderInfo->Fields) wanderFields += f.Name + " ";
        CHECK(wanderInfo->IsBehavior && wanderFields == "speed changeEvery heading timer " && wanderInfo->Fields[0].Default == 50.0 && wanderInfo->Fields[1].Default == 1.0 && wanderInfo->Fields[2].Default == 0.0 && wanderInfo->Fields[3].Default == 0.0 && wanderInfo->Fields[1].Min == 0.01 && Contains(wanderInfo->Description, "random numbers"), "Wander has a speed, a time between turns and its state");

        // the directions come from the scene's seeded numbers; the expected values were worked out apart from the engine
        // (the reference PCG32 with the same float arithmetic in an independent script)
        const std::string wanderer = R"({"name": "w", "components": {"Transform": {}, "Wander": {"speed": 60, "changeEvery": 0.5}}})";
        SceneLoadResult walking = load(wanderer, R"("components": {"Random": {"seed": 42}}, )");
        Entity w = walking.Scene->FindEntity("w");
        std::vector<float> headings;
        glm::vec2 expectedPosition(0.0f);
        float heading = 0.0f;
        bool positionsMatch = true, inRange = true;
        for (int i = 0; i < 130; i++) {
            float before = w.Get<Wander>().Heading;
            stepWith(*walking.Scene, InputFrame());
            heading = w.Get<Wander>().Heading;
            if (i == 0 || heading != before) headings.push_back(heading);
            inRange = inRange && heading >= 0.0f && heading < 6.2831855f;
            expectedPosition += glm::vec2(cosf(heading), sinf(heading)) * 60.0f * dt;
            positionsMatch = positionsMatch && w.Get<Transform>().Position == expectedPosition;
        }
        CHECK(headings.size() >= 4 && headings[0] == 3.96035576f && headings[1] == 3.02577281f && headings[2] == 4.56792641f && headings[3] == 3.23544788f, "with seed 42 the first four directions it picks are the independently worked out ones");
        CHECK(inRange && positionsMatch, "headings are from 0 up to a full turn, and it walks that way at its speed every step");
        RandomGenerator reference(42);
        for (size_t i = 0; i < headings.size(); i++) reference.NextU32();
        CHECK(walking.Scene->GetRandom().GetState() == reference.GetState(), "each new direction costs exactly one random number");

        SceneLoadResult other = load(wanderer, R"("components": {"Random": {"seed": 7}}, )");
        stepWith(*other.Scene, InputFrame());
        CHECK(other.Scene->FindEntity("w").Get<Wander>().Heading == 4.03328848f, "another seed starts with another direction");

        SceneLoadResult timing = load(wanderer, R"("components": {"Random": {"seed": 42}}, )");
        Entity t = timing.Scene->FindEntity("w");
        stepWith(*timing.Scene, InputFrame());
        CHECK(t.Get<Wander>().Heading == 3.96035576f && t.Get<Wander>().Timer == 0.5f, "a timer of 0 picks a direction on the first step and starts the timer");
        stepWith(*timing.Scene, InputFrame());
        CHECK(t.Get<Wander>().Heading == 3.96035576f && t.Get<Wander>().Timer == 0.5f - dt, "and it counts down while the direction is kept");

        SceneLoadResult later = load(R"({"name": "w", "components": {"Transform": {}, "Wander": {"speed": 60, "changeEvery": 0.5, "heading": 0, "timer": 0.1}}})", R"("components": {"Random": {"seed": 42}}, )");
        stepWith(*later.Scene, InputFrame(), 3);
        CHECK(later.Scene->FindEntity("w").Get<Wander>().Heading == 0.0f && later.Scene->FindEntity("w").Get<Transform>().Position.x > 2.9f && later.Scene->FindEntity("w").Get<Transform>().Position.y == 0.0f, "with a timer set it walks the heading it has until the timer runs out");
        stepWith(*later.Scene, InputFrame(), 4);
        CHECK(later.Scene->FindEntity("w").Get<Wander>().Heading == 3.96035576f, "and then picks");

        SceneLoadResult twoWanderers = load(wanderer + R"(, {"name": "w2", "components": {"Transform": {}, "Wander": {"speed": 60, "changeEvery": 0.5}}})", R"("components": {"Random": {"seed": 42}}, )");
        stepWith(*twoWanderers.Scene, InputFrame());
        CHECK(twoWanderers.Scene->FindEntity("w").Get<Wander>().Heading == 3.96035576f && twoWanderers.Scene->FindEntity("w2").Get<Wander>().Heading == 3.02577281f, "two wanderers draw in file order, the first from the first number");

        SceneLoadResult still = load(R"({"name": "w", "components": {"Transform": {"position": [3, 4]}, "Wander": {"speed": 0, "changeEvery": 0.5}}})", R"("components": {"Random": {"seed": 42}}, )");
        stepWith(*still.Scene, InputFrame(), 100);
        CHECK(positionOf(still, "w") == glm::vec2(3.0f, 4.0f) && still.Scene->FindEntity("w").Get<Wander>().Heading != 0.0f, "at speed 0 it still picks directions but goes nowhere");
        SceneLoadResult noTransform = load(R"({"name": "w", "components": {"Wander": {}}})", R"("components": {"Random": {"seed": 42}}, )");
        stepWith(*noTransform.Scene, InputFrame(), 50);
        CHECK(noTransform.Scene->GetRandom().GetState() == RandomGenerator(42).GetState() && !noTransform.Scene->FindEntity("w").Has<Transform>(), "without a Transform it does nothing and uses no random numbers");

        // the same seed is the same walk, however it is run
        SceneLoadResult one = load(wanderer, R"("components": {"Random": {"seed": 2024}}, )"), two = load(wanderer, R"("components": {"Random": {"seed": 2024}}, )"), three = load(wanderer, R"("components": {"Random": {"seed": 2025}}, )");
        stepWith(*one.Scene, InputFrame(), 600); stepWith(*two.Scene, InputFrame(), 600); stepWith(*three.Scene, InputFrame(), 600);
        CHECK(Simulation::Checksum(registry, *one.Scene) == Simulation::Checksum(registry, *two.Scene) && positionOf(one, "w") == positionOf(two, "w") && positionOf(one, "w") != positionOf(three, "w"), "the same seed gives the same walk and another seed a different one");
        SceneLoadResult half = load(wanderer, R"("components": {"Random": {"seed": 2024}}, )");
        stepWith(*half.Scene, InputFrame(), 300);
        SceneLoadResult halfResumed = LoadSceneFromText(SaveSceneToText(*half.Scene, registry).Text, "saved", registry);
        halfResumed.Scene->GetRandom().SetState(half.Scene->GetRandom().GetState());
        stepWith(*halfResumed.Scene, InputFrame(), 300);
        CHECK(Simulation::Checksum(registry, *halfResumed.Scene) == Simulation::Checksum(registry, *one.Scene), "a walk saved halfway carries on exactly when the generator's position is carried over too");

        // a fence keeps it in, because the clamp always runs last
        SceneLoadResult fenced = load(R"({"name": "w", "components": {"Transform": {"position": [120, 120]}, "ClampPosition": {"min": [100, 100], "max": [140, 140]}, "Wander": {"speed": 200, "changeEvery": 0.3}}})", R"("components": {"Random": {"seed": 5}}, )");
        bool inside = true, touchedEdge = false;
        for (int i = 0; i < 1200; i++) {
            stepWith(*fenced.Scene, InputFrame());
            glm::vec2 at = positionOf(fenced, "w");
            inside = inside && at.x >= 100.0f && at.x <= 140.0f && at.y >= 100.0f && at.y <= 140.0f;
            touchedEdge = touchedEdge || at.x == 100.0f || at.x == 140.0f || at.y == 100.0f || at.y == 140.0f;
        }
        CHECK(inside && touchedEdge, "a wanderer with a ClampPosition never leaves the fence, though it is written before the wander (and it does reach the fence)");

        CHECK(HasProblem(problems("Wander", R"("changeEvery": 0)"), "/entities/0/components/Wander/changeEvery", "must be at least 0.01") && HasProblem(problems("Wander", R"("speed": -5)"), "/entities/0/components/Wander/speed", "must be at least 0") && HasProblem(problems("Wander", R"("timer": -1)"), "/entities/0/components/Wander/timer", "must be at least 0") && HasProblem(problems("Wander", R"("timer": 4000)"), "/entities/0/components/Wander/timer", "must be at most 3600"), "a zero interval, a negative speed and a bad timer are refused");
        CHECK(Contains(SaveSceneToText(*problems("Wander", R"("speed": 60, "changeEvery": 0.75, "heading": 1.5, "timer": 0.25)").Scene, registry).Text, R"("Wander": { "speed": 60, "changeEvery": 0.75, "heading": 1.5, "timer": 0.25 })"), "it is written with its state");
    }

    // restarting: RestartOnKey, and the scene's restart request
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        auto load = [&](const std::string& scene, const std::string& entities = "") { return LoadSceneFromText(R"({"version": 1, "name": "t", )" + scene + R"("entities": [)" + entities + "]}", "inline", registry); };
        auto stepWith = [&](Scene& scene, const InputFrame& input, int n = 1) { for (int i = 0; i < n; i++) Simulation::Step(registry, scene, input, 1.0 / 60.0); };

        const ComponentInfo* info = registry.Find("RestartOnKey");
        CHECK(info->IsBehavior && info->Fields.size() == 1 && info->Fields[0].Default == "R" && info->Fields[0].OneOf == KeyNames() && Contains(info->Description, "restart"), "RestartOnKey is a behavior with a key, R by default");

        Scene plain("p");
        CHECK(!plain.IsRestartRequested(), "a scene is not asking for a restart");
        plain.RequestRestart();
        CHECK(plain.IsRestartRequested(), "it can be asked to");
        plain.ClearRestartRequest();
        CHECK(!plain.IsRestartRequested(), "and cleared");

        SceneLoadResult restart = load(R"("components": {"RestartOnKey": {}}, )", R"({"name": "c", "components": {"Counter": {}}})");
        stepWith(*restart.Scene, InputFrame().Hold(Key::R), 3);
        CHECK(!restart.Scene->IsRestartRequested(), "a key that is only held does not ask for a restart");
        stepWith(*restart.Scene, InputFrame().Press(Key::Q));
        CHECK(!restart.Scene->IsRestartRequested(), "another key does not");
        stepWith(*restart.Scene, InputFrame().Press(Key::R));
        CHECK(restart.Scene->IsRestartRequested(), "pressing the key does");
        stepWith(*restart.Scene, InputFrame(), 5);
        CHECK(restart.Scene->IsRestartRequested(), "and the request stays until the host clears it");
        restart.Scene->ClearRestartRequest();
        stepWith(*restart.Scene, InputFrame());
        CHECK(!restart.Scene->IsRestartRequested(), "once cleared it is not asked again");
        InputFrame tap; tap.Press(Key::R).Release(Key::R);
        stepWith(*restart.Scene, tap);
        CHECK(restart.Scene->IsRestartRequested(), "a tap that was released before the step still counts as a press");

        SceneLoadResult custom = load(R"("components": {"RestartOnKey": {"key": "F5"}}, )");
        stepWith(*custom.Scene, InputFrame().Press(Key::R));
        CHECK(!custom.Scene->IsRestartRequested(), "with another key chosen, R does nothing");
        stepWith(*custom.Scene, InputFrame().Press(Key::F5));
        CHECK(custom.Scene->IsRestartRequested(), "and that key does");
        SceneLoadResult onEntity = load("", R"({"name": "e", "components": {"RestartOnKey": {}}})");
        stepWith(*onEntity.Scene, InputFrame().Press(Key::R));
        CHECK(onEntity.Scene->IsRestartRequested(), "it works on an entity too");

        // the request is runtime state
        SceneLoadResult checked = load("", R"({"name": "c", "components": {"Counter": {}}})");
        uint64_t before = Simulation::Checksum(registry, *checked.Scene);
        checked.Scene->RequestRestart();
        CHECK(Simulation::Checksum(registry, *checked.Scene) == before && !Contains(SaveSceneToText(*checked.Scene, registry).Text, "estart"), "a request is not in the checksum or in a saved scene");
        CHECK(!load(R"("components": {"RestartOnKey": {}}, )").Scene->IsRestartRequested(), "a loaded scene is not asking");

        CHECK(HasProblem(LoadSceneFromText(R"({"version": 1, "name": "t", "components": {"RestartOnKey": {"key": "r"}}, "entities": []})", "inline", registry), "/components/RestartOnKey/key", "did you mean 'R'?") && HasProblem(LoadSceneFromText(R"({"version": 1, "name": "t", "components": {"RestartOnKey": {"key": ""}}, "entities": []})", "inline", registry), "/components/RestartOnKey/key", "got ''"), "a mistyped key is suggested and no key is refused");

        // taking a component off by name
        SceneLoadResult removal = load("", R"({"name": "e", "components": {"Camera": {}, "Spin": {}}})");
        Entity e = removal.Scene->FindEntity("e");
        CHECK(registry.Remove(e, "Spin") && !e.Has<Spin>() && e.Has<Camera>() && !registry.Remove(e, "Spin") && !registry.Remove(e, "Nope") && !registry.Remove(Entity(), "Camera"), "a component can be taken off by name, once, and an unknown name or an invalid entity does nothing");
    }

    // keeping the state of a running scene when its file changes
    {
        ComponentRegistry registry = MakeSimulationRegistry();
        auto load = [&](const std::string& text) { return LoadSceneFromText(text, "inline", registry); };
        auto stepWith = [&](Scene& scene, const InputFrame& input, int n = 1) { for (int i = 0; i < n; i++) Simulation::Step(registry, scene, input, 1.0 / 60.0); };
        auto world = [](const std::string& playerMove = R"("speed": 60)", const std::string& playerPosition = "[100, 100]", const std::string& extra = "", const std::string& seed = "5") {
            return R"({"version": 1, "name": "w", "components": {"Random": {"seed": )" + seed + R"(}}, "entities": [
                {"name": "player", "components": {"Transform": {"position": )" + playerPosition + R"(}, "KeyboardMove": {)" + playerMove + R"(, "relativeTo": "camera"}}},
                {"name": "light", "components": {"Transform": {}, "Orbit": {"target": "player", "radius": 20, "speed": 2}}},
                {"name": "rabbit", "components": {"Transform": {"position": [300, 300]}, "Wander": {"speed": 40, "changeEvery": 0.5}}},
                {"name": "camera", "components": {"Transform": {}, "Camera": {"primary": true}, "Follow": {"target": "player"}, "KeyboardRotate": {}, "WheelZoom": {}}})" + extra + "]}";
        };
        InputFrame keys; keys.Hold(Key::W).Hold(Key::D).Hold(Key::Q); keys.Scroll = 0.0f;
        InputFrame wheel; wheel.Scroll = 2.0f;
        auto runFor = [&](Scene& scene) { stepWith(scene, keys, 80); stepWith(scene, wheel); stepWith(scene, keys, 40); };

        const std::string baselineText = world();
        SceneLoadResult baseline = load(baselineText), running = load(baselineText);
        runFor(*running.Scene);
        CHECK(glm::length(running.Scene->FindEntity("player").Get<Transform>().Position - glm::vec2(100.0f, 100.0f)) > 20.0f && running.Scene->FindEntity("camera").Get<Camera>().Zoom > 1.0f && running.Scene->FindEntity("camera").Get<Transform>().Rotation > 1.0f && running.Scene->FindEntity("light").Get<Orbit>().Angle > 3.0f, "the scene has been run: the player moved, the camera turned and zoomed, the light went round");

        // the same file again: nothing is lost, and the scene carries on exactly as if it had never been reloaded
        SceneLoadResult same = load(baselineText);
        CarryOverState(registry, *baseline.Scene, *running.Scene, *same.Scene);
        CHECK(Simulation::Checksum(registry, *same.Scene) == Simulation::Checksum(registry, *running.Scene), "reloading an unchanged file gives a scene in exactly the state of the running one, the random sequence included");
        CHECK(running.Scene->FindEntity("player").Has<PreviousTransform>() && !same.Scene->FindEntity("player").Has<PreviousTransform>(), "a previous transform is runtime data: the running scene has one, the carried scene gets its own from its next step");
        stepWith(*same.Scene, keys, 60);
        stepWith(*running.Scene, keys, 60);
        CHECK(same.Scene->FindEntity("player").Has<PreviousTransform>(), "which it then has");
        CHECK(Simulation::Checksum(registry, *same.Scene) == Simulation::Checksum(registry, *running.Scene), "and the two stay together when they run on");

        // an edit to one value: the new value, and everything else as it had got to
        SceneLoadResult faster = load(world(R"("speed": 120)"));
        glm::vec2 playerNow = running.Scene->FindEntity("player").Get<Transform>().Position;
        CarryOverState(registry, *baseline.Scene, *running.Scene, *faster.Scene);
        CHECK(faster.Scene->FindEntity("player").Get<KeyboardMove>().Speed == 120.0f && faster.Scene->FindEntity("player").Get<Transform>().Position == playerNow, "a changed setting takes effect and the player stays where it was");
        CHECK(faster.Scene->FindEntity("light").Get<Orbit>().Angle == running.Scene->FindEntity("light").Get<Orbit>().Angle && faster.Scene->FindEntity("camera").Get<Camera>().Zoom == running.Scene->FindEntity("camera").Get<Camera>().Zoom && faster.Scene->FindEntity("rabbit").Get<Transform>().Position == running.Scene->FindEntity("rabbit").Get<Transform>().Position, "and the other entities keep their state");

        // the author moved the player: that wins, and the rest still carries
        SceneLoadResult moved = load(world(R"("speed": 60)", "[500, 500]"));
        CarryOverState(registry, *baseline.Scene, *running.Scene, *moved.Scene);
        CHECK(moved.Scene->FindEntity("player").Get<Transform>().Position == glm::vec2(500.0f, 500.0f) && moved.Scene->FindEntity("camera").Get<Transform>().Rotation == running.Scene->FindEntity("camera").Get<Transform>().Rotation, "a value the file changed is the file's, even though the simulation had moved it");

        // seeds
        SceneLoadResult reseeded = load(world(R"("speed": 60)", "[100, 100]", "", "6"));
        CarryOverState(registry, *baseline.Scene, *running.Scene, *reseeded.Scene);
        CHECK(reseeded.Scene->GetRandom().GetState() == RandomGenerator(6).GetState() && reseeded.Scene->GetRandom().GetState() != running.Scene->GetRandom().GetState(), "a changed seed starts the random sequence again");

        // entities and components added and removed
        SceneLoadResult edited = load(world(R"("speed": 60)", "[100, 100]", R"(, {"name": "newcomer", "components": {"Transform": {"position": [9, 9]}, "Spin": {"speed": 3}}})"));
        edited.Scene->FindEntity("rabbit").Remove<Wander>();
        CarryOverState(registry, *baseline.Scene, *running.Scene, *edited.Scene);
        CHECK(edited.Scene->FindEntity("newcomer").Get<Transform>().Position == glm::vec2(9.0f, 9.0f) && edited.Scene->FindEntity("newcomer").Get<Spin>().Speed == 3.0f && !edited.Scene->FindEntity("rabbit").Has<Wander>() && edited.Scene->FindEntity("rabbit").Get<Transform>().Position == running.Scene->FindEntity("rabbit").Get<Transform>().Position, "an entity added in the file is as the file has it, and a component removed stays removed");
        const std::string withoutLight = R"({"version": 1, "name": "w", "components": {"Random": {"seed": 5}}, "entities": [{"name": "player", "components": {"Transform": {"position": [100, 100]}}}, {"name": "renamed_rabbit", "components": {"Transform": {"position": [300, 300]}}}]})";
        SceneLoadResult trimmed = load(withoutLight);
        CarryOverState(registry, *baseline.Scene, *running.Scene, *trimmed.Scene);
        CHECK(!trimmed.Scene->FindEntity("light") && trimmed.Scene->FindEntity("renamed_rabbit").Get<Transform>().Position == glm::vec2(300.0f, 300.0f) && trimmed.Scene->FindEntity("player").Get<Transform>().Position == playerNow, "an entity removed from the file is gone, and a renamed one starts over as a new entity");

        // a file that changed a component and then the simulation: only that component is the file's
        SceneLoadResult both = load(world(R"("speed": 60)", "[100, 100]", R"(, {"name": "late", "components": {"Counter": {}}})"));
        SceneLoadResult baseWithLate = load(world(R"("speed": 60)", "[100, 100]", R"(, {"name": "late", "components": {"Counter": {}}})"));
        stepWith(*both.Scene, keys, 10);
        SceneLoadResult next = load(world(R"("speed": 60)", "[100, 100]", R"(, {"name": "late", "components": {"Counter": {"every": 5}}})"));
        CarryOverState(registry, *baseWithLate.Scene, *both.Scene, *next.Scene);
        CHECK(next.Scene->FindEntity("late").Get<Counter>().Every == 5 && next.Scene->FindEntity("late").Get<Counter>().Count == 0, "a component the file changed is as the file has it (its running count is not mixed in)");
        CHECK(next.Scene->FindEntity("player").Get<Transform>().Position == both.Scene->FindEntity("player").Get<Transform>().Position, "while the unchanged player carries");

        // reload twice in a row, each time from the file as it then was: still one continuous run
        SceneLoadResult chainA = load(baselineText), chainRun = load(baselineText), chainControl = load(baselineText);
        stepWith(*chainRun.Scene, keys, 30); stepWith(*chainControl.Scene, keys, 30);
        SceneLoadResult second = load(baselineText);
        CarryOverState(registry, *chainA.Scene, *chainRun.Scene, *second.Scene);
        stepWith(*second.Scene, keys, 30); stepWith(*chainControl.Scene, keys, 30);
        SceneLoadResult baselineAgain = load(baselineText), third = load(baselineText);
        CarryOverState(registry, *baselineAgain.Scene, *second.Scene, *third.Scene);
        stepWith(*third.Scene, keys, 30); stepWith(*chainControl.Scene, keys, 30);
        CHECK(Simulation::Checksum(registry, *third.Scene) == Simulation::Checksum(registry, *chainControl.Scene), "two reloads along the way leave the run exactly as an unbroken one");
    }

    // input recordings: the file
    {
        // frames: W and D go down (step 0); D up (3); a wheel movement (4); a tap on Q (5); W up (6); Space down with the wheel (8)
        InputRecording recording;
        recording.SceneName = "Test scene";
        recording.SceneHash = 0x0123456789abcdefull;
        recording.Seed = 7;
        recording.StepsPerSecond = 60;
        recording.HasChecksum = true;
        recording.Checksum = 0xfedcba9876543210ull;
        recording.Frames.resize(10);
        recording.Frames[0].Press(Key::W).Press(Key::D);
        recording.Frames[1].Hold(Key::W).Hold(Key::D);
        recording.Frames[2].Hold(Key::W).Hold(Key::D);
        recording.Frames[3].Hold(Key::W);
        recording.Frames[4].Hold(Key::W); recording.Frames[4].Scroll = 1.5f;
        recording.Frames[5].Hold(Key::W); recording.Frames[5].Pressed.set(Key::Q);
        recording.Frames[8].Press(Key::Space); recording.Frames[8].Scroll = -2.0f;
        recording.Frames[9].Hold(Key::Space);

        const std::string golden = R"({
  "version": 1,
  "scene": "Test scene",
  "sceneHash": "0123456789abcdef",
  "seed": 7,
  "stepsPerSecond": 60,
  "steps": 10,
  "checksum": "fedcba9876543210",
  "changes": [
    { "step": 0, "down": ["D", "W"], "pressed": ["D", "W"] },
    { "step": 3, "up": ["D"] },
    { "step": 4, "scroll": 1.5 },
    { "step": 5, "pressed": ["Q"] },
    { "step": 6, "up": ["W"] },
    { "step": 8, "down": ["Space"], "pressed": ["Space"], "scroll": -2 }
  ]
}
)";
        RecordingWriteResult written = WriteRecording(recording);
        for (const std::string& problem : written.Problems) printf("      %s\n", problem.c_str());
        CHECK(written.Ok() && written.Text == golden, "a recording is written one line per step where the input changed");
        CHECK(recording.GetStepCount() == 10, "and knows how many steps it has");

        RecordingReadResult read = ReadRecording(golden);
        for (const std::string& problem : read.Problems) printf("      %s\n", problem.c_str());
        const InputRecording& back = read.Recording;
        CHECK(read.Ok() && back.SceneName == "Test scene" && back.SceneHash == 0x0123456789abcdefull && back.Seed == 7 && back.StepsPerSecond == 60 && back.HasChecksum && back.Checksum == 0xfedcba9876543210ull && back.GetStepCount() == 10, "it reads back with its header");
        bool sameFrames = back.Frames.size() == recording.Frames.size();
        for (size_t i = 0; sameFrames && i < recording.Frames.size(); i++) sameFrames = back.Frames[i] == recording.Frames[i];
        CHECK(sameFrames, "and with every frame exactly: keys down, taps, newly pressed keys and the wheel");
        CHECK(WriteRecording(back).Text == golden, "writing what was read gives the same text");
        CHECK(!back.Frames[5].IsDown(Key::Q) && back.Frames[5].WasPressed(Key::Q) && back.Frames[3].IsDown(Key::W) && !back.Frames[3].IsDown(Key::D) && back.Frames[7] == InputFrame() && back.Frames[9].IsDown(Key::Space) && !back.Frames[9].WasPressed(Key::Space), "a tap is pressed and not down, a key stays down until it goes up, and unlisted steps repeat the keys that are down");

        InputRecording empty;
        empty.SceneName = "e"; empty.SceneHash = 1;
        CHECK(WriteRecording(empty).Text == "{\n  \"version\": 1,\n  \"scene\": \"e\",\n  \"sceneHash\": \"0000000000000001\",\n  \"seed\": 1,\n  \"stepsPerSecond\": 60,\n  \"steps\": 0,\n  \"changes\": []\n}\n", "a recording with no steps and no checksum is written without either");
        RecordingReadResult nothing = ReadRecording(WriteRecording(empty).Text);
        CHECK(nothing.Ok() && nothing.Recording.GetStepCount() == 0 && !nothing.Recording.HasChecksum, "and reads back");
        InputRecording quiet = empty;
        quiet.Frames.resize(5);
        quiet.Frames[2].Hold(Key::A); quiet.Frames[3].Hold(Key::A); quiet.Frames[4].Hold(Key::A);   // down from step 2 on, never pressed
        RecordingReadResult quietRead = ReadRecording(WriteRecording(quiet).Text);
        CHECK(quietRead.Ok() && quietRead.Recording.Frames.size() == 5 && quietRead.Recording.Frames[2].IsDown(Key::A) && quietRead.Recording.Frames[4].IsDown(Key::A) && quietRead.Recording.Frames[1] == InputFrame(), "a key that is down without ever being pressed (held from before) is kept too");

        // comments and layout do not matter when reading
        RecordingReadResult loose = ReadRecording("// a recording\n{ \"version\": 1, \"scene\": \"Test scene\", \"sceneHash\": \"0123456789abcdef\", \"seed\": 7, \"stepsPerSecond\": 60, \"steps\": 3,\n \"changes\": [ /* the first step */ {\"step\": 1, \"down\": [\"W\"]} ] }");
        CHECK(loose.Ok() && loose.Recording.Frames[1].IsDown(Key::W) && !loose.Recording.Frames[1].WasPressed(Key::W) && loose.Recording.Frames[2].IsDown(Key::W) && !loose.Recording.HasChecksum, "comments and any layout are fine, and the checksum is optional");

        // what the writer refuses
        InputRecording unnamed = empty;
        unnamed.Frames.resize(2);
        unnamed.Frames[1].Hold(150);
        RecordingWriteResult w1 = WriteRecording(unnamed);
        CHECK(!w1.Ok() && Contains(w1.Problems[0], "step 1: a key that has no name"), "a key with no name cannot be written");
        InputRecording infinite = empty;
        infinite.Frames.resize(1); infinite.Frames[0].Scroll = INFINITY;
        CHECK(!WriteRecording(infinite).Ok() && Contains(WriteRecording(infinite).Problems[0], "not finite"), "a wheel movement that is not a number cannot be written");
        InputRecording nameless = empty; nameless.SceneName = "";
        InputRecording badRate = empty; badRate.StepsPerSecond = 0;
        InputRecording badName = empty; badName.SceneName = "bad\xff";
        CHECK(!WriteRecording(nameless).Ok() && !WriteRecording(badRate).Ok() && !WriteRecording(badName).Ok(), "no scene name, a bad step rate and a name that is not UTF-8 are refused");
    }

    // input recordings: the spec's own example is a valid recording
    {
        std::string doc = ReadFile("docs/input-recording.md");
        size_t at = doc.find("```jsonc\n");
        std::string block = at == std::string::npos ? "" : doc.substr(at + 9, doc.find("\n```", at) - at - 9);
        RecordingReadResult read = ReadRecording(block);
        for (const std::string& problem : read.Problems) printf("      %s\n", problem.c_str());
        CHECK(!block.empty() && read.Ok() && read.Recording.GetStepCount() == 12 && read.Recording.Frames[0].WasPressed(Key::W) && read.Recording.Frames[5].WasPressed(Key::Q) && read.Recording.Frames[11].IsDown(Key::Space) && read.Recording.Frames[8].Scroll == -2.0f, "the example in docs/input-recording.md reads with the engine's own reader");
        CHECK(Contains(doc, "`sceneHash`") && Contains(doc, "`stepsPerSecond`") && Contains(doc, "`checksum`") && Contains(doc, "`changes`") && Contains(doc, "`scene`") && Contains(doc, "`seed`") && Contains(doc, "`steps`") && Contains(doc, "`version`"), "and the document describes every key of the file");
    }

    // input recordings: reading problems
    {
        const std::string head = R"({"version": 1, "scene": "s", "sceneHash": "0000000000000001", "seed": 1, "stepsPerSecond": 60, "steps": 10, )";
        auto read = [&](const std::string& rest) { return ReadRecording(head + rest); };
        auto has = [](const RecordingReadResult& r, const std::string& text) {
            for (const std::string& p : r.Problems) if (p.find(text) != std::string::npos) return true;
            return false;
        };

        CHECK(read(R"("changes": []})").Ok(), "the base recording used below is valid");
        RecordingReadResult syntax = ReadRecording(R"({"version": 1, )");
        CHECK(!syntax.Ok() && Contains(syntax.Problems[0], "parse error"), "a syntax error is reported");
        CHECK(Contains(ReadRecording("[]").Problems[0], "the top level must be an object, got an array"), "so is a top level that is not an object");
        CHECK(has(ReadRecording(R"({"scene": "s"})"), "missing required key 'version'") && has(ReadRecording(R"({"version": 2})"), "/version: this engine reads recordings of version 1"), "a missing or newer version is refused");
        RecordingReadResult unknown = read(R"("stepz": 3, "changes": []})");
        CHECK(has(unknown, "/stepz: unknown key 'stepz' (did you mean 'steps'?)"), "an unknown key gets a suggestion");

        CHECK(has(ReadRecording(R"({"version": 1, "sceneHash": "0000000000000001", "seed": 1, "stepsPerSecond": 60, "steps": 1, "changes": []})"), "missing required key 'scene'") && has(ReadRecording(R"({"version": 1, "scene": "", "sceneHash": "0000000000000001", "seed": 1, "stepsPerSecond": 60, "steps": 1, "changes": []})"), "/scene: expected a non-empty string"), "the scene name is required");
        for (const char* bad : { "\"ABCDEF0123456789\"", "\"123\"", "5", "\"000000000000000g\"" })
            CHECK(has(ReadRecording(std::string(R"({"version": 1, "scene": "s", "sceneHash": )") + bad + R"(, "seed": 1, "stepsPerSecond": 60, "steps": 1, "changes": []})"), "/sceneHash: expected 16 lower-case hex digits"), (std::string("the scene hash ") + bad + " is refused").c_str());
        CHECK(has(ReadRecording(R"({"version": 1, "scene": "s", "seed": 1, "stepsPerSecond": 60, "steps": 1, "changes": []})"), "missing required key 'sceneHash'"), "and required");
        auto withField = [&](const std::string& field) { return ReadRecording(R"({"version": 1, "scene": "s", "sceneHash": "0000000000000001", )" + field + R"(, "changes": []})"); };
        CHECK(has(withField(R"("seed": 1.5, "stepsPerSecond": 60, "steps": 1)"), "/seed: expected an integer, got a number") && has(withField(R"("seed": "x", "stepsPerSecond": 60, "steps": 1)"), "/seed: expected an integer, got a string"), "the seed must be an integer");
        CHECK(has(withField(R"("seed": 1, "stepsPerSecond": 0, "steps": 1)"), "/stepsPerSecond: must be 1 to 1000, got 0") && has(withField(R"("seed": 1, "stepsPerSecond": 5000, "steps": 1)"), "/stepsPerSecond: must be 1 to 1000"), "the step rate is held to a sane range");
        CHECK(has(withField(R"("seed": 1, "stepsPerSecond": 60, "steps": -1)"), "/steps: must be 0 to 1000000, got -1") && has(withField(R"("seed": 1, "stepsPerSecond": 60, "steps": 1000001)"), "/steps: must be 0 to 1000000") && has(withField(R"("seed": 1, "stepsPerSecond": 60)"), "missing required key 'steps'"), "so is the number of steps");
        CHECK(has(read(R"("checksum": "xyz", "changes": []})"), "/checksum: expected 16 lower-case hex digits") && read(R"("checksum": "00000000000000ff", "changes": []})").Recording.Checksum == 255, "a checksum is 16 hex digits when it is there");
        CHECK(has(ReadRecording(R"({"version": 1, "scene": "s", "sceneHash": "0000000000000001", "seed": 1, "stepsPerSecond": 60, "steps": 10})"), "missing required key 'changes'") && has(read(R"("changes": {}})"), "/changes: expected an array, got an object"), "changes is required and an array");
        CHECK(has(read(R"("changes": [5]})"), "/changes/0: a change must be an object, got a number") && has(read(R"("changes": [{"step": 1, "dwn": ["W"]}]})"), "/changes/0/dwn: unknown key 'dwn' (did you mean 'down'?)"), "a change must be an object, with known keys");

        CHECK(has(read(R"("changes": [{"down": ["W"]}]})"), "/changes/0/step: expected an integer step") && has(read(R"("changes": [{"step": 10, "down": ["W"]}]})"), "/changes/0/step: step 10 is outside the recording (0 to 9)") && has(read(R"("changes": [{"step": -1, "down": ["W"]}]})"), "outside the recording"), "a change needs a step inside the recording");
        CHECK(has(read(R"("changes": [{"step": 5, "down": ["W"]}, {"step": 3, "up": ["W"]}]})"), "/changes/1/step: step 3 is not after the step before it (5)") && has(read(R"("changes": [{"step": 2, "down": ["W"]}, {"step": 2, "up": ["W"]}]})"), "/changes/1/step: step 2 is not after"), "steps must be in order, once each");
        CHECK(has(read(R"("changes": [{"step": 1, "down": "W"}]})"), "/changes/0/down: expected an array of key names, got a string") && has(read(R"("changes": [{"step": 1, "down": [5]}]})"), "/changes/0/down/0: expected a key name, got a number"), "key lists must be lists of names");
        RecordingReadResult typo = read(R"("changes": [{"step": 1, "down": ["W", "Spce"]}]})");
        CHECK(has(typo, "/changes/0/down/1: 'Spce' is not a key name (did you mean 'Space'?)") && has(typo, "there are " + std::to_string(KeyNames().size()) + " key names"), "a name that is not a key gets a suggestion and the count of names");
        CHECK(has(read(R"("changes": [{"step": 1, "down": ["w"]}]})"), "(did you mean 'W'?)") && has(read(R"("changes": [{"step": 1, "pressed": ["W", "W"]}]})"), "/changes/0/pressed/1: 'W' is listed twice"), "case matters, and a key is listed once");
        CHECK(has(read(R"("changes": [{"step": 1, "down": ["W"]}, {"step": 2, "down": ["W"]}]})"), "/changes/1/down: 'W' goes down but is already down") && has(read(R"("changes": [{"step": 1, "up": ["W"]}]})"), "/changes/0/up: 'W' goes up but is not down"), "a key cannot go down while down or up while not down");
        CHECK(has(read(R"("changes": [{"step": 1, "down": ["W"]}, {"step": 2, "down": ["A"], "up": ["A"]}]})"), "/changes/1: 'A' goes down and up in the same step"), "or both at once");
        CHECK(has(read(R"("changes": [{"step": 1}]})"), "/changes/0: this change does nothing; leave the step out") && has(read(R"("changes": [{"step": 1, "scroll": 0}]})"), "this change does nothing") && has(read(R"("changes": [{"step": 1, "scroll": "up"}]})"), "/changes/0/scroll: expected a number, got a string"), "a change that changes nothing is a mistake, and the wheel is a number");
        RecordingReadResult many = read(R"("changes": [{"step": 1, "down": ["Nope"]}, {"step": 2, "up": ["W"]}, 7]})");
        CHECK(many.Problems.size() == 3, "every problem is reported, not just the first");
        CHECK(read(R"("changes": [{"step": 1, "up": ["W"]}, {"step": 2, "down": ["W"]}]})").Problems.size() == 1, "a key that is not down cannot go up, even if it goes down later");

        // a recording that reads clean has frames for every step; one with problems must not be replayed
        RecordingReadResult clean = read(R"("changes": [{"step": 2, "down": ["W"], "pressed": ["W"]}, {"step": 6, "up": ["W"]}]})");
        CHECK(clean.Ok() && clean.Recording.Frames.size() == 10 && !clean.Recording.Frames[1].IsDown(Key::W) && clean.Recording.Frames[2].WasPressed(Key::W) && clean.Recording.Frames[5].IsDown(Key::W) && !clean.Recording.Frames[6].IsDown(Key::W) && !clean.Recording.Frames[9].IsDown(Key::W), "a valid recording has a frame for each step");
    }

    // input recordings: the scene it is for, the recorder and the replay
    {
        InputRecording recording;
        recording.SceneName = "The scene"; recording.SceneHash = 0xabcull; recording.Seed = 5; recording.StepsPerSecond = 60;
        CHECK(RecordingMismatch(recording, "The scene", 0xabcull, 5, 60).empty(), "a recording fits the scene it was made for");
        std::string otherName = RecordingMismatch(recording, "Another", 0xabcull, 5, 60);
        CHECK(Contains(otherName, "made for the scene 'The scene', not 'Another'"), "a different scene is named in the refusal");
        std::string otherText = RecordingMismatch(recording, "The scene", 0xdefull, 5, 60);
        CHECK(Contains(otherText, "different version of the scene 'The scene'") && Contains(otherText, "0000000000000abc") && Contains(otherText, "0000000000000def") && Contains(otherText, "record again"), "a changed scene text is refused, with both hashes and what to do");
        CHECK(Contains(RecordingMismatch(recording, "The scene", 0xabcull, 6, 60), "seed 5") && Contains(RecordingMismatch(recording, "The scene", 0xabcull, 5, 120), "60 steps a second, and this run is at 120"), "and so are another seed or step rate");

        InputRecorder recorder;
        CHECK(!recorder.IsRecording(), "nothing is being recorded at first");
        recorder.Record(InputFrame().Press(Key::A));
        CHECK(recorder.GetStepCount() == 0, "a frame given when not recording is dropped");
        recorder.Start("Rec", 0x1234ull, 3, 60);
        CHECK(recorder.IsRecording() && recorder.GetSceneHash() == 0x1234ull, "starting a recording keeps the scene's hash");
        recorder.Record(InputFrame().Press(Key::W));
        recorder.Record(InputFrame().Hold(Key::W));
        recorder.Record(InputFrame());
        CHECK(recorder.GetStepCount() == 3, "each step adds a frame");
        InputRecording finished = recorder.Finish(0xfeedull);
        CHECK(!recorder.IsRecording() && finished.SceneName == "Rec" && finished.SceneHash == 0x1234ull && finished.Seed == 3 && finished.HasChecksum && finished.Checksum == 0xfeedull && finished.Frames.size() == 3 && finished.Frames[0].WasPressed(Key::W) && finished.Frames[1].IsDown(Key::W) && finished.Frames[2] == InputFrame(), "finishing gives the recording with the checksum");
        recorder.Start("Again", 1, 1, 60);
        recorder.Record(InputFrame().Press(Key::B));
        recorder.Cancel();
        CHECK(!recorder.IsRecording() && recorder.GetStepCount() == 0, "cancelling drops what was recorded");
        recorder.Start("Fresh", 2, 1, 60);
        CHECK(recorder.GetStepCount() == 0, "and a new recording starts empty");

        InputReplay replay(finished);
        CHECK(!replay.IsFinished() && replay.GetStep() == 0 && replay.GetRecording().SceneName == "Rec", "a replay starts at the first step");
        InputFrame a = replay.Next(), b = replay.Next();
        CHECK(a.WasPressed(Key::W) && b.IsDown(Key::W) && !b.WasPressed(Key::W) && replay.GetStep() == 2 && !replay.IsFinished(), "it gives the frames in order");
        replay.Next();
        CHECK(replay.IsFinished() && replay.GetStep() == 3 && replay.Next() == InputFrame() && replay.GetStep() == 3, "and then empty ones once it is finished");
    }

    // input recordings: a replay is the run it came from
    {
        ComponentRegistry registry = MakeSimulationRegistry();
        auto load = [&](const std::string& text) { return LoadSceneFromText(text, "inline", registry); };
        auto sceneText = [](int seed, const std::string& speed = "200") {
            return R"({"version": 1, "name": "Run", "components": {"Random": {"seed": )" + std::to_string(seed) + R"(}}, "entities": [
                {"name": "player", "components": {"Transform": {"position": [500, 500]}, "KeyboardMove": {"speed": )" + speed + R"(, "relativeTo": "camera"}}},
                {"name": "rabbit", "components": {"Transform": {"position": [300, 300]}, "Wander": {"speed": 90, "changeEvery": 0.4}, "Dice": {}}},
                {"name": "light", "components": {"Transform": {}, "Orbit": {"target": "player", "radius": 30, "speed": 2}}},
                {"name": "camera", "components": {"Transform": {}, "Camera": {"primary": true}, "Follow": {"target": "player"}, "KeyboardRotate": {}, "WheelZoom": {}}}]})";
        };
        const std::string text = sceneText(77);

        // play and record a run, with random numbers in it
        SceneLoadResult live = load(text);
        InputRecorder recorder;
        recorder.Start(live.Scene->GetName(), live.TextHash, live.Scene->GetSeed(), 60);
        RandomGenerator script(99);
        for (int i = 0; i < 600; i++) {
            InputFrame frame = ScriptedFrame(script);
            recorder.Record(frame);
            Simulation::Step(registry, *live.Scene, frame, 1.0 / 60.0);
        }
        const uint64_t liveChecksum = Simulation::Checksum(registry, *live.Scene);
        InputRecording recording = recorder.Finish(liveChecksum);
        CHECK(recording.GetStepCount() == 600 && recording.SceneHash == Fnv1a64(text.data(), text.size()) && recording.Seed == 77 && recording.SceneName == "Run", "a recorded run knows its scene, its text's hash, its seed and its length");

        // through text, as it would be on disk
        RecordingWriteResult written = WriteRecording(recording);
        RecordingReadResult read = ReadRecording(written.Text);
        CHECK(written.Ok() && read.Ok() && read.Recording.Checksum == liveChecksum && read.Recording.GetStepCount() == 600 && WriteRecording(read.Recording).Text == written.Text, "it survives being written and read");

        // the replay
        SceneLoadResult replayed = load(text);
        CHECK(RecordingMismatch(read.Recording, replayed.Scene->GetName(), replayed.TextHash, replayed.Scene->GetSeed(), 60).empty(), "the scene it is replayed on fits it");
        uint64_t replayChecksum = ReplayRecording(registry, *replayed.Scene, read.Recording);
        CHECK(replayChecksum == liveChecksum && replayChecksum == read.Recording.Checksum, "replaying it on a fresh copy of the scene ends in exactly the state of the run it came from");
        CHECK(Simulation::Checksum(registry, *replayed.Scene) == liveChecksum && replayed.Scene->FindEntity("rabbit").Get<Transform>().Position == live.Scene->FindEntity("rabbit").Get<Transform>().Position && replayed.Scene->FindEntity("player").Get<Transform>().Position == live.Scene->FindEntity("player").Get<Transform>().Position, "player and the random-walking rabbit are where they were");

        // and it is not a coincidence: change anything and the replay differs
        InputRecording changed = read.Recording;
        changed.Frames[300].Hold(Key::Q);
        SceneLoadResult one = load(text), two = load(text), three = load(sceneText(78)), four = load(sceneText(77, "201"));
        CHECK(ReplayRecording(registry, *one.Scene, changed) != liveChecksum, "one key held in one step gives a different end state");
        CHECK(RecordingMismatch(read.Recording, "Run", three.TextHash, three.Scene->GetSeed(), 60) != "" && ReplayRecording(registry, *two.Scene, InputRecording(read.Recording)) == liveChecksum, "a second replay of the same recording is again the same");
        CHECK(RecordingMismatch(read.Recording, "Run", three.TextHash, 78, 60).find("different version") != std::string::npos && ReplayRecording(registry, *three.Scene, read.Recording) != liveChecksum, "a scene with another seed is refused, and forcing it gives another run");
        CHECK(RecordingMismatch(read.Recording, "Run", four.TextHash, 77, 60).find("different version") != std::string::npos && ReplayRecording(registry, *four.Scene, read.Recording) != liveChecksum, "so is one with a different player speed: any change to the text is a different version");
        CHECK(Fnv1a64(text.data(), text.size()) != Fnv1a64((text + " ").data(), text.size() + 1) && load(text + " ").TextHash != load(text).TextHash && load(text).TextHash == load(text).TextHash, "the hash of the scene text changes with a single added space");

        // the real world file
        ComponentRegistry builtins;
        RegisterBuiltinComponents(builtins);
        SceneLoadResult world = LoadScene("Assets/Scenes/world.json", builtins);
        InputRecorder worldRecorder;
        worldRecorder.Start(world.Scene->GetName(), world.TextHash, world.Scene->GetSeed(), 60);
        RandomGenerator worldScript(8);
        for (int i = 0; i < 900; i++) {
            InputFrame frame = ScriptedFrame(worldScript);
            worldRecorder.Record(frame);
            Simulation::Step(builtins, *world.Scene, frame, 1.0 / 60.0);
        }
        InputRecording worldRecording = worldRecorder.Finish(Simulation::Checksum(builtins, *world.Scene));
        RecordingReadResult worldRead = ReadRecording(WriteRecording(worldRecording).Text);
        SceneLoadResult worldAgain = LoadScene("Assets/Scenes/world.json", builtins);
        CHECK(worldRead.Ok() && RecordingMismatch(worldRead.Recording, worldAgain.Scene->GetName(), worldAgain.TextHash, worldAgain.Scene->GetSeed(), 60).empty() && ReplayRecording(builtins, *worldAgain.Scene, worldRead.Recording) == worldRecording.Checksum, "the Sandbox world, played for 900 steps and recorded, replays to the same state");

        // a recording is the same thing whether the run was long or short
        InputRecording prefix = worldRead.Recording;
        prefix.Frames.resize(450);
        SceneLoadResult half = LoadScene("Assets/Scenes/world.json", builtins), straight = LoadScene("Assets/Scenes/world.json", builtins);
        uint64_t halfway = ReplayRecording(builtins, *half.Scene, prefix);
        RandomGenerator again(8);
        for (int i = 0; i < 450; i++) Simulation::Step(builtins, *straight.Scene, ScriptedFrame(again), 1.0 / 60.0);
        CHECK(halfway == Simulation::Checksum(builtins, *straight.Scene) && halfway != worldRecording.Checksum, "the first half of a recording is the first half of the run");
    }

    // the scene's hash, the reloader and the command line
    {
        ComponentRegistry registry;
        RegisterBuiltinComponents(registry);
        const std::string text = R"({"version": 1, "name": "h", "entities": []})";
        SceneLoadResult a = LoadSceneFromText(text, "a", registry), b = LoadSceneFromText(text + "\n", "b", registry);
        CHECK(a.TextHash == Fnv1a64(text.data(), text.size()) && a.TextHash != b.TextHash && a.TextHash == LoadSceneFromText(text, "c", registry).TextHash, "a loaded scene carries the hash of its text, the same for the same text");
        CHECK(LoadSceneFromText("{ not json", "d", registry).TextHash == Fnv1a64("{ not json", 10), "even when it could not be loaded");
        CHECK(a.Scene->GetSeed() == 1 && LoadSceneFromText(R"({"version": 1, "name": "h", "components": {"Random": {"seed": -4}}, "entities": []})", "e", registry).Scene->GetSeed() == -4, "a scene reports its seed, 1 when it has none");

        const std::filesystem::path work = std::filesystem::temp_directory_path() / "marle_scene_hash_test";
        std::filesystem::remove_all(work);
        std::filesystem::create_directories(work);
        const std::string file = (work / "scene.json").generic_string();
        const std::string v2 = R"({"version": 1, "name": "h", "entities": [{"name": "a"}]})";
        const std::string broken = R"({"version": 1, "name": "h", "entities": [{"name": "a", "components": {"Camera": {"zom": 2}}}]})";
        WriteFile(file, text);
        SceneReloader reloader(file, registry);
        reloader.Load();
        CHECK(reloader.GetSceneHash() == Fnv1a64(text.data(), text.size()), "the reloader knows the hash of the scene it handed out");
        WriteFile(file, v2);
        std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now() + std::chrono::minutes(1));
        CHECK(reloader.Poll().Scene != nullptr && reloader.GetSceneHash() == Fnv1a64(v2.data(), v2.size()), "and the new hash once a reload is applied");
        WriteFile(file, broken);
        std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now() + std::chrono::minutes(2));
        CHECK(reloader.Poll().Scene == nullptr && reloader.GetSceneHash() == Fnv1a64(v2.data(), v2.size()), "a refused reload leaves the hash of the scene that is still running");
        std::filesystem::remove_all(work);

        // the command line
        char program[] = "Sandbox", replay[] = "--replay", path[] = "run.json", quiet[] = "--quiet", fast[] = "--speed=3", last[] = "--last";
        char* argv[] = { program, replay, path, quiet, fast, last };
        SetCommandLine(6, argv);
        CHECK(GetCommandLine() == std::vector<std::string>({ "--replay", "run.json", "--quiet", "--speed=3", "--last" }), "the command line is what follows the program's name");
        CHECK(HasCommandLineFlag("--quiet") && HasCommandLineFlag("--replay") && HasCommandLineFlag("--speed") && !HasCommandLineFlag("--loud") && !HasCommandLineFlag("--quie"), "a flag is there or not");
        CHECK(GetCommandLineValue("--replay") == std::optional<std::string>("run.json") && GetCommandLineValue("--speed") == std::optional<std::string>("3"), "a value follows a flag, or comes after an equals sign");
        CHECK(!GetCommandLineValue("--last") && !GetCommandLineValue("--nope"), "a flag with nothing after it, or no flag, has no value");
        SetCommandLine(1, argv);
        CHECK(GetCommandLine().empty() && !HasCommandLineFlag("--replay"), "with nothing after the program's name there are no arguments");
    }

    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
