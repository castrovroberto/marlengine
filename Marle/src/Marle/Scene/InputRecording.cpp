#include "mrlpch.h"
#include "Marle/Core/KeyCodes.h"
#include "Marle/Core/KeyNames.h"
#include "InputRecording.h"
#include "Simulation.h"
#include <cmath>

namespace Marle {

    namespace {

        // Every code that has a key name
        const std::bitset<MaxKeys>& NamedKeys()
        {
            static const std::bitset<MaxKeys> named = [] {
                std::bitset<MaxKeys> keys;
                for (const std::string& name : KeyNames()) keys.set(KeyFromName(name));
                return keys;
            }();
            return named;
        }

        // The names of the keys in a set, in the order of KeyNames()
        std::vector<std::string> NamesIn(const std::bitset<MaxKeys>& keys)
        {
            std::vector<std::string> names;
            for (const std::string& name : KeyNames())
                if (keys[KeyFromName(name)]) names.push_back(name);
            return names;
        }

        std::string QuotedList(const std::vector<std::string>& names)
        {
            std::string out = "[";
            for (size_t i = 0; i < names.size(); i++) out += (i ? ", " : "") + Json(names[i]).dump();
            return out + "]";
        }

        const std::vector<std::string> EventKinds = { "trigger_entered", "trigger_left", "solid_stopped" };

        bool IsEventKind(const std::string& kind) { return std::find(EventKinds.begin(), EventKinds.end(), kind) != EventKinds.end(); }

        bool ParseHex16(const std::string& text, uint64_t& value)
        {
            if (text.size() != 16) return false;
            value = 0;
            for (char c : text) {
                int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
                if (digit < 0) return false;
                value = (value << 4) | (uint64_t)digit;
            }
            return true;
        }

        std::string NumberText(float value)
        {
            std::string text = detail::FloatToJson(value).dump();
            if (text.size() > 2 && text.compare(text.size() - 2, 2, ".0") == 0) text.resize(text.size() - 2);
            return text;
        }

    }

    RecordingWriteResult WriteRecording(const InputRecording& recording)
    {
        RecordingWriteResult result;
        auto problem = [&](const std::string& where, const std::string& what) { result.Problems.push_back(where + ": " + what); };

        if (recording.SceneName.empty()) problem("scene", "the recording has no scene name");
        if (recording.StepsPerSecond < 1 || recording.StepsPerSecond > 1000) problem("stepsPerSecond", "must be 1 to 1000");
        if (recording.GetStepCount() > MaxRecordingSteps) problem("steps", "too many steps (the most is " + std::to_string(MaxRecordingSteps) + ")");

        std::string sceneName = "\"\"";
        try { sceneName = Json(recording.SceneName).dump(); }
        catch (const Json::exception&) { problem("scene", "the scene name is not valid UTF-8"); }

        auto textOf = [&](const std::string& value, const std::string& where) {
            try { return Json(value).dump(); }
            catch (const Json::exception&) { problem(where, "the text is not valid UTF-8"); return std::string("\"\""); }
        };

        std::vector<std::string> entries;
        std::bitset<MaxKeys> previous;
        for (unsigned step = 0; step < recording.Frames.size(); step++) {
            const InputFrame& frame = recording.Frames[step];
            const std::string where = "step " + std::to_string(step);

            if ((frame.Down | frame.Pressed).any() && ((frame.Down | frame.Pressed) & ~NamedKeys()).any())
                problem(where, "a key that has no name is down or pressed, so it cannot be written");
            if (!std::isfinite(frame.Scroll)) problem(where, "the wheel movement is not finite");

            const std::bitset<MaxKeys> wentDown = frame.Down & ~previous, wentUp = previous & ~frame.Down;
            previous = frame.Down;
            if (wentDown.none() && wentUp.none() && frame.Pressed.none() && frame.Scroll == 0.0f) continue;

            std::string entry = "    { \"step\": " + std::to_string(step);
            if (wentDown.any()) entry += ", \"down\": " + QuotedList(NamesIn(wentDown));
            if (wentUp.any()) entry += ", \"up\": " + QuotedList(NamesIn(wentUp));
            if (frame.Pressed.any()) entry += ", \"pressed\": " + QuotedList(NamesIn(frame.Pressed));
            if (frame.Scroll != 0.0f) entry += ", \"scroll\": " + NumberText(frame.Scroll);
            entries.push_back(entry + " }");
        }

        std::string text = "{\n  \"version\": 1,\n  \"scene\": " + sceneName + ",\n";
        text += "  \"sceneHash\": \"" + Simulation::ChecksumText(recording.SceneHash) + "\",\n";
        text += "  \"seed\": " + std::to_string(recording.Seed) + ",\n";
        text += "  \"stepsPerSecond\": " + std::to_string(recording.StepsPerSecond) + ",\n";
        text += "  \"steps\": " + std::to_string(recording.GetStepCount()) + ",\n";
        if (recording.HasChecksum) text += "  \"checksum\": \"" + Simulation::ChecksumText(recording.Checksum) + "\",\n";
        if (!recording.SceneFile.empty()) text += "  \"sceneFile\": " + textOf(recording.SceneFile, "sceneFile") + ",\n";
        if (!recording.Description.empty()) text += "  \"description\": " + textOf(recording.Description, "description") + ",\n";
        if (!recording.ExpectedEvents.empty()) {
            text += "  \"expectedEvents\": [";
            for (size_t i = 0; i < recording.ExpectedEvents.size(); i++) {
                const ExpectedEvent& event = recording.ExpectedEvents[i];
                if (event.Step >= recording.GetStepCount()) problem("expectedEvents/" + std::to_string(i), "the step is past the end of the recording");
                if (!IsEventKind(event.Kind)) problem("expectedEvents/" + std::to_string(i), "'" + event.Kind + "' is not an event kind");
                if (event.Entity.empty()) problem("expectedEvents/" + std::to_string(i), "the event has no entity");
                std::string line = "{ \"step\": " + std::to_string(event.Step) + ", \"kind\": " + textOf(event.Kind, "expectedEvents/kind") + ", \"entity\": " + textOf(event.Entity, "expectedEvents/entity");
                if (!event.Other.empty()) line += ", \"other\": " + textOf(event.Other, "expectedEvents/other");
                if (!event.Axis.empty()) line += ", \"axis\": " + textOf(event.Axis, "expectedEvents/axis");
                text += (i ? ",\n    " : "\n    ") + line + " }";
            }
            text += "\n  ],\n";
        }
        text += "  \"changes\": [";
        for (size_t i = 0; i < entries.size(); i++) text += (i ? ",\n" : "\n") + entries[i];
        text += entries.empty() ? "]\n}\n" : "\n  ]\n}\n";

        result.Text = text;
        return result;
    }

    RecordingReadResult ReadRecording(const std::string& text)
    {
        RecordingReadResult result;
        auto problem = [&](const std::string& path, const std::string& message) { result.Problems.push_back(path + ": " + message); };
        InputRecording& recording = result.Recording;

        Json root;
        try {
            root = Json::parse(text, nullptr, true, true);
        } catch (const Json::exception& e) {
            std::string message = e.what();
            size_t bracket = message.find("] ");
            problem("", bracket == std::string::npos ? message : message.substr(bracket + 2));
            return result;
        }
        if (!root.is_object()) { problem("", std::string("the top level must be an object, got ") + detail::JsonTypeName(root)); return result; }

        const std::vector<std::string> topKeys = { "version", "scene", "sceneHash", "seed", "stepsPerSecond", "steps", "checksum", "sceneFile", "description", "expectedEvents", "changes" };
        for (auto it = root.begin(); it != root.end(); ++it) {
            if (std::find(topKeys.begin(), topKeys.end(), it.key()) != topKeys.end()) continue;
            std::string hint = detail::SuggestionFor(it.key(), topKeys);
            problem("/" + it.key(), "unknown key '" + it.key() + "'" + (hint.empty() ? "" : " (" + hint + ")") + "; valid keys: " + detail::Join(topKeys));
        }

        auto requireInteger = [&](const char* key, long long minimum, long long maximum, long long& value) {
            if (!root.contains(key)) { problem("", std::string("missing required key '") + key + "'"); return false; }
            if (!root[key].is_number_integer()) { problem(std::string("/") + key, std::string("expected an integer, got ") + detail::JsonTypeName(root[key])); return false; }
            value = root[key].get<long long>();
            if (value < minimum || value > maximum) { problem(std::string("/") + key, "must be " + std::to_string(minimum) + " to " + std::to_string(maximum) + ", got " + std::to_string(value)); return false; }
            return true;
        };

        if (!root.contains("version")) { problem("", "missing required key 'version'"); return result; }
        if (!root["version"].is_number_integer() || root["version"].get<long long>() != 1) {
            problem("/version", "this engine reads recordings of version 1");
            return result;
        }

        if (!root.contains("scene")) problem("", "missing required key 'scene'");
        else if (!root["scene"].is_string() || root["scene"].get<std::string>().empty()) problem("/scene", "expected a non-empty string");
        else recording.SceneName = root["scene"].get<std::string>();

        if (!root.contains("sceneHash")) problem("", "missing required key 'sceneHash'");
        else if (!root["sceneHash"].is_string() || !ParseHex16(root["sceneHash"].get<std::string>(), recording.SceneHash)) problem("/sceneHash", "expected 16 lower-case hex digits");

        long long seed = 0, perSecond = 0, steps = 0;
        if (requireInteger("seed", INT32_MIN, INT32_MAX, seed)) recording.Seed = (int)seed;
        if (requireInteger("stepsPerSecond", 1, 1000, perSecond)) recording.StepsPerSecond = (int)perSecond;
        bool haveSteps = requireInteger("steps", 0, MaxRecordingSteps, steps);

        if (root.contains("checksum")) {
            if (!root["checksum"].is_string() || !ParseHex16(root["checksum"].get<std::string>(), recording.Checksum)) problem("/checksum", "expected 16 lower-case hex digits");
            else recording.HasChecksum = true;
        }

        for (const char* key : { "sceneFile", "description" }) {
            if (!root.contains(key)) continue;
            if (!root[key].is_string() || root[key].get<std::string>().empty()) problem(std::string("/") + key, "expected a non-empty string");
            else (std::string(key) == "sceneFile" ? recording.SceneFile : recording.Description) = root[key].get<std::string>();
        }

        if (root.contains("expectedEvents")) {
            const Json& list = root["expectedEvents"];
            if (!list.is_array()) problem("/expectedEvents", std::string("expected an array, got ") + detail::JsonTypeName(list));
            else {
                const std::vector<std::string> eventKeys = { "step", "kind", "entity", "other", "axis" };
                long long lastStep = 0;
                for (size_t i = 0; i < list.size(); i++) {
                    const std::string at = "/expectedEvents/" + std::to_string(i);
                    const Json& item = list[i];
                    if (!item.is_object()) { problem(at, std::string("an event must be an object, got ") + detail::JsonTypeName(item)); continue; }
                    for (auto it = item.begin(); it != item.end(); ++it)
                        if (std::find(eventKeys.begin(), eventKeys.end(), it.key()) == eventKeys.end()) {
                            std::string hint = detail::SuggestionFor(it.key(), eventKeys);
                            problem(at + "/" + it.key(), "unknown key '" + it.key() + "'" + (hint.empty() ? "" : " (" + hint + ")") + "; valid keys: " + detail::Join(eventKeys));
                        }

                    ExpectedEvent event;
                    bool ok = true;
                    if (!item.contains("step") || !item["step"].is_number_integer()) { problem(at + "/step", "expected an integer step"); ok = false; }
                    else {
                        long long step = item["step"].get<long long>();
                        if (haveSteps && (step < 0 || step >= steps)) { problem(at + "/step", "step " + std::to_string(step) + " is outside the recording (0 to " + std::to_string(steps - 1) + ")"); ok = false; }
                        else if (step < lastStep) { problem(at + "/step", "step " + std::to_string(step) + " is before the event above it; expected events are in step order"); ok = false; }
                        else { event.Step = (unsigned)std::max(step, 0ll); lastStep = step; }
                    }
                    if (!item.contains("kind") || !item["kind"].is_string()) { problem(at + "/kind", "expected an event kind: " + detail::Join(EventKinds)); ok = false; }
                    else if (!IsEventKind(item["kind"].get<std::string>())) {
                        std::string hint = detail::SuggestionFor(item["kind"].get<std::string>(), EventKinds);
                        problem(at + "/kind", "'" + item["kind"].get<std::string>() + "' is not an event kind" + (hint.empty() ? "" : " (" + hint + ")") + "; the kinds are " + detail::Join(EventKinds));
                        ok = false;
                    } else event.Kind = item["kind"].get<std::string>();
                    if (!item.contains("entity") || !item["entity"].is_string() || item["entity"].get<std::string>().empty()) { problem(at + "/entity", "expected the name of an entity"); ok = false; }
                    else event.Entity = item["entity"].get<std::string>();
                    for (const char* key : { "other", "axis" }) {
                        if (!item.contains(key)) continue;
                        if (!item[key].is_string() || item[key].get<std::string>().empty()) { problem(at + "/" + key, "expected a non-empty string"); ok = false; }
                        else (std::string(key) == "other" ? event.Other : event.Axis) = item[key].get<std::string>();
                    }
                    if (ok && event.Kind == "solid_stopped" && (!event.Other.empty() || (event.Axis != "x" && event.Axis != "y" && event.Axis != "xy"))) { problem(at, "a solid_stopped event has an axis (\"x\", \"y\" or \"xy\") and no 'other'"); ok = false; }
                    if (ok && event.Kind != "solid_stopped" && (event.Other.empty() || !event.Axis.empty())) { problem(at, "a " + event.Kind + " event has an 'other' entity and no axis"); ok = false; }
                    if (ok) recording.ExpectedEvents.push_back(std::move(event));
                }
            }
        }

        if (!root.contains("changes")) { problem("", "missing required key 'changes'"); return result; }
        if (!root["changes"].is_array()) { problem("/changes", std::string("expected an array, got ") + detail::JsonTypeName(root["changes"])); return result; }
        if (!haveSteps) return result;

        // Rebuild the frames, checking each change against the keys that are down at that point
        recording.Frames.assign((size_t)steps, InputFrame());
        std::bitset<MaxKeys> down;
        long long nextStep = 0, previousListed = -1;
        size_t index = 0;
        const std::vector<std::string> changeKeys = { "step", "down", "up", "pressed", "scroll" };

        for (const Json& change : root["changes"]) {
            const std::string at = "/changes/" + std::to_string(index++);
            if (!change.is_object()) { problem(at, std::string("a change must be an object, got ") + detail::JsonTypeName(change)); continue; }

            for (auto it = change.begin(); it != change.end(); ++it)
                if (std::find(changeKeys.begin(), changeKeys.end(), it.key()) == changeKeys.end()) {
                    std::string hint = detail::SuggestionFor(it.key(), changeKeys);
                    problem(at + "/" + it.key(), "unknown key '" + it.key() + "'" + (hint.empty() ? "" : " (" + hint + ")") + "; valid keys: " + detail::Join(changeKeys));
                }

            if (!change.contains("step") || !change["step"].is_number_integer()) { problem(at + "/step", "expected an integer step"); continue; }
            long long step = change["step"].get<long long>();
            if (step < 0 || step >= steps) { problem(at + "/step", "step " + std::to_string(step) + " is outside the recording (0 to " + std::to_string(steps - 1) + ")"); continue; }
            if (step <= previousListed) { problem(at + "/step", "step " + std::to_string(step) + " is not after the step before it (" + std::to_string(previousListed) + "); changes must be in order, once each"); continue; }
            previousListed = step;

            std::bitset<MaxKeys> lists[3];   // down, up, pressed
            bool any = false, ok = true;
            const char* names[3] = { "down", "up", "pressed" };
            for (int k = 0; k < 3; k++) {
                if (!change.contains(names[k])) continue;
                const Json& list = change[names[k]];
                const std::string listAt = at + "/" + names[k];
                if (!list.is_array()) { problem(listAt, std::string("expected an array of key names, got ") + detail::JsonTypeName(list)); ok = false; continue; }
                for (size_t i = 0; i < list.size(); i++) {
                    if (!list[i].is_string()) { problem(listAt + "/" + std::to_string(i), std::string("expected a key name, got ") + detail::JsonTypeName(list[i])); ok = false; continue; }
                    const std::string name = list[i].get<std::string>();
                    int code = KeyFromName(name);
                    if (code == Key::Unknown) {
                        std::string hint = detail::SuggestionFor(name, KeyNames());
                        problem(listAt + "/" + std::to_string(i), "'" + name + "' is not a key name" + (hint.empty() ? "" : " (" + hint + ")") + "; there are " + std::to_string(KeyNames().size()) + " key names (see the scene format's list)");
                        ok = false;
                    } else if (lists[k][code]) {
                        problem(listAt + "/" + std::to_string(i), "'" + name + "' is listed twice");
                        ok = false;
                    } else {
                        lists[k].set(code);
                        any = true;
                    }
                }
            }

            float scroll = 0.0f;
            if (change.contains("scroll")) {
                if (!change["scroll"].is_number() || !std::isfinite(change["scroll"].get<double>())) { problem(at + "/scroll", std::string("expected a number, got ") + detail::JsonTypeName(change["scroll"])); ok = false; }
                else { scroll = change["scroll"].get<float>(); any = any || scroll != 0.0f; }
            }
            if (!ok) continue;
            if (!any) { problem(at, "this change does nothing; leave the step out"); continue; }

            // the key lists have to fit the keys that are down
            for (int code = 0; code < MaxKeys; code++) {
                if (lists[0][code] && lists[1][code]) problem(at, std::string("'") + KeyName(code) + "' goes down and up in the same step");
                else if (lists[0][code] && down[code]) problem(at + "/down", std::string("'") + KeyName(code) + "' goes down but is already down");
                else if (lists[1][code] && !down[code]) problem(at + "/up", std::string("'") + KeyName(code) + "' goes up but is not down");
            }

            // frames from the previous listed step up to this one keep the keys that were down
            for (long long s = nextStep; s < step; s++) recording.Frames[(size_t)s].Down = down;
            down = (down & ~lists[1]) | lists[0];
            recording.Frames[(size_t)step].Down = down;
            recording.Frames[(size_t)step].Pressed = lists[2];
            recording.Frames[(size_t)step].Scroll = scroll;
            nextStep = step + 1;
        }
        for (long long s = nextStep; s < steps; s++) recording.Frames[(size_t)s].Down = down;

        return result;
    }

    std::string ExpectedEventText(const ExpectedEvent& event)
    {
        return "step " + std::to_string(event.Step) + " " + event.Kind + " " + event.Entity + (event.Other.empty() ? "" : " > " + event.Other) + (event.Axis.empty() ? "" : " on " + event.Axis);
    }

    std::vector<ExpectedEvent> MissingExpectedEvents(const std::vector<ExpectedEvent>& expected, const std::vector<StampedEvent>& run)
    {
        std::vector<ExpectedEvent> missing;
        size_t from = 0;
        for (const ExpectedEvent& want : expected) {
            size_t at = from;
            while (at < run.size()) {
                const StampedEvent& got = run[at];
                if (got.Step == (long long)want.Step && want.Kind == SimulationEventKindName(got.What.What) && want.Entity == got.What.Entity && want.Other == got.What.Other && want.Axis == got.What.Axis) break;
                at++;
            }
            if (at < run.size()) from = at + 1;
            else missing.push_back(want);   // a miss does not move on, so what follows is still looked for after the last match
        }
        return missing;
    }

    std::string RecordingMismatch(const InputRecording& recording, const std::string& sceneName, uint64_t sceneHash, int seed, int stepsPerSecond)
    {
        if (recording.SceneName != sceneName)
            return "this recording was made for the scene '" + recording.SceneName + "', not '" + sceneName + "'";
        if (recording.SceneHash != sceneHash)
            return "this recording was made against a different version of the scene '" + sceneName + "' (its text hashed to " + Simulation::ChecksumText(recording.SceneHash) + " when it was recorded, and the scene now hashes to " + Simulation::ChecksumText(sceneHash) + "); the same input would not give the same run, so record again, or run the scene as it was";
        if (recording.Seed != seed)
            return "this recording was made with the scene's seed " + std::to_string(recording.Seed) + ", and the scene's seed is " + std::to_string(seed);
        if (recording.StepsPerSecond != stepsPerSecond)
            return "this recording was made at " + std::to_string(recording.StepsPerSecond) + " steps a second, and this run is at " + std::to_string(stepsPerSecond);
        return "";
    }

    void InputRecorder::Start(std::string sceneName, uint64_t sceneHash, int seed, int stepsPerSecond)
    {
        m_Result = InputRecording();
        m_Result.SceneName = std::move(sceneName);
        m_Result.SceneHash = sceneHash;
        m_Result.Seed = seed;
        m_Result.StepsPerSecond = stepsPerSecond;
        m_Recording = true;
    }

    void InputRecorder::Record(const InputFrame& frame)
    {
        if (m_Recording && m_Result.Frames.size() < MaxRecordingSteps) m_Result.Frames.push_back(frame);
    }

    InputRecording InputRecorder::Finish(uint64_t checksum)
    {
        m_Recording = false;
        m_Result.HasChecksum = true;
        m_Result.Checksum = checksum;
        return std::move(m_Result);
    }

    void InputRecorder::Cancel()
    {
        m_Recording = false;
        m_Result = InputRecording();
    }

    uint64_t ReplayRecording(const ComponentRegistry& registry, Scene& scene, const InputRecording& recording)
    {
        const double dt = 1.0 / (double)recording.StepsPerSecond;
        for (const InputFrame& frame : recording.Frames)
            Simulation::Step(registry, scene, frame, dt);
        return Simulation::Checksum(registry, scene);
    }

}
