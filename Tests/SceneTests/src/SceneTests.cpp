// Scene and Entity: naming, components, handle safety and iteration. Headless.
// Exits non-zero if any check fails.
#include "Marle/Scene/Scene.h"
#include "Marle/Scene/Components.h"
#include "Marle/Scene/ComponentRegistry.h"
#include <cstdio>
#include <string>
#include <vector>

using namespace Marle;

static int fails = 0;
#define CHECK(c, name) do { if (!(c)) { fails++; printf("FAIL: %s\n", name); } else { printf("ok:   %s\n", name); } } while (0)

struct Position { float x = 0.0f, y = 0.0f; Position() = default; Position(float x, float y) : x(x), y(y) {} };
struct Health { int points = 10; };
struct Door { std::string Target; int Level = 1; bool Locked = false; };   // Target is required

static bool HasError(const std::vector<ValidationError>& errors, const std::string& path, const std::string& text)
{
    for (const ValidationError& e : errors)
        if (e.Path == path && e.Message.find(text) != std::string::npos) return true;
    return false;
}

int main()
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
        CHECK(names.size() == 3 && names[0] == "Transform" && names[1] == "SpriteRenderer" && names[2] == "Camera", "the built-in components register in order");

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

    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
