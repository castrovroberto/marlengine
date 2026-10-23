// Application drives its layers. Needs a display and GL 3.3; run from the repo root.
// Exits non-zero if any check fails.
#include "Marle/Application.h"
#include "Marle/Input.h"
#include "Marle/InputFrame.h"
#include "Marle/Core/KeyCodes.h"
#include "Marle/Events/KeyEvent.h"
#include "Marle/Events/MouseEvent.h"
#include <glad/gl.h>
#include <cstdio>
#include <string>
#include <vector>

using namespace Marle;

static int fails = 0;
#define CHECK(c, name) do { if (!(c)) { fails++; printf("FAIL: %s\n", name); } else { printf("ok:   %s\n", name); } } while (0)

static std::string s_Calls;
static std::string s_DetachOrder;
static bool s_ContextAliveOnDetach = true;

struct RecordingLayer : Layer {
    RecordingLayer(const std::string& name) : Layer(name) {}
    void OnAttach() override { s_Calls += "attach" + m_DebugName + " "; }
    void OnDetach() override {
        s_DetachOrder += m_DebugName;
        // the GL context must still be current while layers detach
        if (!glGetString(GL_VERSION)) s_ContextAliveOnDetach = false;
    }
    void OnUpdate(double) override { s_Calls += "u" + m_DebugName + " "; }
    void OnRender(double) override { s_Calls += "r" + m_DebugName + " "; }
};

class TestApp : public Application {
    int updates = 0;
public:
    TestApp() : Application({"apptest", 400, 300}) {
        PushLayer(std::make_unique<RecordingLayer>("A"));
        PushOverlay(std::make_unique<RecordingLayer>("O"));
        PushLayer(std::make_unique<RecordingLayer>("B"));
    }
protected:
    void OnUpdate(double) override { s_Calls += "uApp "; updates++; }
    void OnRender(double) override {
        s_Calls += "rApp ";
        if (updates >= 2) Close();   // ran at least two fixed updates
    }
};

// Records the input frame of every fixed step, and injects events the way the window would
class LiveInputApp : public Application {
    int frame = 0;
public:
    std::vector<InputFrame> Frames;
    LiveInputApp() : Application({"inputtest", 400, 300}) {}
protected:
    void OnUpdate(double) override { Frames.push_back(GetInputFrame()); }
    void OnRender(double) override {
        frame++;
        if (frame == 3) {
            Input::OnEvent(KeyPressedEvent(Key::W, 0));
            Input::OnEvent(MouseScrolledEvent(0.0f, 2.0f));
            Input::OnEvent(MouseScrolledEvent(0.0f, 1.0f));
        }
        if (frame == 6) Input::OnEvent(KeyReleasedEvent(Key::W));
        if (frame >= 9 && Frames.size() >= 10) Close();
    }
};

// A layer that takes key presses of one key, the way ImGui takes keys while a text field has focus
struct TakesW : Layer {
    TakesW() : Layer("TakesW") {}
    void OnEvent(Event& e) override
    {
        if (e.GetEventType() == EventType::KeyPressed && static_cast<KeyPressedEvent&>(e).GetKeyCode() == Key::W) e.SetHandled();
        if (e.GetEventType() == EventType::MouseScrolled) e.SetHandled();
    }
};

// Sends events through HandleEvent, as the window does, and records the input frames
class WindowEventApp : public Application {
    int frame = 0;
public:
    std::vector<InputFrame> Frames;
    std::vector<std::string> AppKeyEvents;   // the key events that reached the application's own OnEvent
    WindowEventApp() : Application({"eventtest", 400, 300}) { PushOverlay(std::make_unique<TakesW>()); }
    void OnEvent(Event& e) override
    {
        if (e.GetEventType() == EventType::KeyPressed) AppKeyEvents.push_back("press " + std::to_string(static_cast<KeyPressedEvent&>(e).GetKeyCode()));
        if (e.GetEventType() == EventType::KeyReleased) AppKeyEvents.push_back("release " + std::to_string(static_cast<KeyReleasedEvent&>(e).GetKeyCode()));
    }
protected:
    void OnUpdate(double) override { Frames.push_back(GetInputFrame()); }
    void OnRender(double) override {
        frame++;
        if (frame == 3) {
            KeyPressedEvent w(Key::W, 0), a(Key::A, 0);
            MouseScrolledEvent wheel(0.0f, 2.0f);
            HandleEvent(w); HandleEvent(a); HandleEvent(wheel);   // the layer takes W and the wheel; A is left
        }
        if (frame == 6) { KeyReleasedEvent a(Key::A); HandleEvent(a); }
        if (frame >= 9 && Frames.size() >= 10) Close();
    }
};

// Feeds scripted frames instead of the keyboard, through the SampleInput hook
class ScriptedApp : public Application {
    size_t next = 0;
public:
    std::vector<InputFrame> Script, Seen, SeenByLayer;

    struct Probe : Layer {
        ScriptedApp& App;
        Probe(ScriptedApp& app) : Layer("Probe"), App(app) {}
        void OnUpdate(double) override { App.SeenByLayer.push_back(App.GetInputFrame()); }
    };

    ScriptedApp() : Application({"scripttest", 400, 300}) { PushLayer(std::make_unique<Probe>(*this)); }
protected:
    InputFrame SampleInput() override { return next < Script.size() ? Script[next++] : InputFrame(); }
    void OnUpdate(double) override { Seen.push_back(GetInputFrame()); }
    void OnRender(double) override { if (next >= Script.size() && Seen.size() >= Script.size()) Close(); }
};

int main()
{
    {
        TestApp app;
        CHECK(s_Calls == "attachA attachO attachB ", "layers attach as they are pushed");

        app.Run();
        CHECK(s_Calls.find("uApp uA uB uO ") != std::string::npos, "update runs app, then layers bottom-to-top");
        CHECK(s_Calls.find("rApp rA rB rO ") != std::string::npos, "render runs app, then layers bottom-to-top");
        CHECK(s_DetachOrder.empty(), "layers stay attached after Run() returns");
    }
    CHECK(s_DetachOrder == "OBA", "destroying the app detaches layers, top first");
    CHECK(s_ContextAliveOnDetach, "GL context is still alive when layers detach");

    // the input of each fixed step, from real events
    {
        LiveInputApp app;
        app.Run();
        const std::vector<InputFrame>& frames = app.Frames;

        int pressed = 0, firstDown = -1, lastDown = -1;
        float scroll = 0.0f;
        for (size_t i = 0; i < frames.size(); i++) {
            pressed += frames[i].WasPressed(Key::W);
            scroll += frames[i].Scroll;
            if (frames[i].IsDown(Key::W)) { if (firstDown < 0) firstDown = (int)i; lastDown = (int)i; }
        }
        CHECK(frames.size() >= 10, "every fixed step gets an input frame");
        CHECK(firstDown >= 0 && frames[firstDown].WasPressed(Key::W) && frames[firstDown].Scroll == 3.0f, "the first step after the events sees the press and all the wheel movement");
        CHECK(pressed == 1 && scroll == 3.0f, "the press and the wheel movement are delivered once each, however many steps there are");
        bool heldUntilReleased = true, quietBefore = true, quietAfter = true;
        for (int i = 0; i < firstDown; i++) quietBefore = quietBefore && frames[i].Down.none() && frames[i].Pressed.none() && frames[i].Scroll == 0.0f;
        for (int i = firstDown; i <= lastDown; i++) heldUntilReleased = heldUntilReleased && frames[i].IsDown(Key::W) && (i == firstDown || (!frames[i].WasPressed(Key::W) && frames[i].Scroll == 0.0f));
        for (size_t i = lastDown + 1; i < frames.size(); i++) quietAfter = quietAfter && !frames[i].IsDown(Key::W);
        CHECK(quietBefore && heldUntilReleased && quietAfter && lastDown + 1 < (int)frames.size(), "the key is held from the press until the release, and not before or after");
    }

    // events a layer handled are not input, the rest are
    {
        WindowEventApp app;
        app.Run();
        bool sawA = false, sawW = false, scrolled = false, releasedA = true;
        int pressedA = 0;
        size_t lastA = 0;
        for (size_t i = 0; i < app.Frames.size(); i++) {
            sawA = sawA || app.Frames[i].IsDown(Key::A);
            sawW = sawW || app.Frames[i].IsDown(Key::W) || app.Frames[i].WasPressed(Key::W);
            scrolled = scrolled || app.Frames[i].Scroll != 0.0f;
            pressedA += app.Frames[i].WasPressed(Key::A);
            if (app.Frames[i].IsDown(Key::A)) lastA = i;
        }
        for (size_t i = lastA + 1; i < app.Frames.size(); i++) releasedA = releasedA && !app.Frames[i].IsDown(Key::A);
        CHECK(sawA && pressedA == 1, "a key no layer took is input");
        CHECK(!sawW && !scrolled, "a key and the wheel that a layer took are not");
        CHECK(releasedA && lastA + 1 < app.Frames.size(), "and the key is released when its release arrives");
        CHECK(app.AppKeyEvents == std::vector<std::string>({ "press " + std::to_string(Key::A), "release " + std::to_string(Key::A) }), "the application's own OnEvent gets the key events the layers left (the A press and its release), not the one they took");
    }

    // the input of each fixed step, from a scripted source
    {
        ScriptedApp app;
        app.Script = {
            InputFrame().Press(Key::W),
            [] { InputFrame f; f.Hold(Key::W); f.Scroll = 1.0f; return f; }(),
            InputFrame().Hold(Key::W).Press(Key::Space),
            InputFrame(),
        };
        app.Run();
        bool inOrder = app.Seen.size() >= 4;
        for (size_t i = 0; i < 4 && inOrder; i++) inOrder = app.Seen[i] == app.Script[i];
        CHECK(inOrder, "OnUpdate sees the frames SampleInput gives, in order");
        CHECK(app.SeenByLayer.size() == app.Seen.size() && app.SeenByLayer == app.Seen, "a layer sees the same frame in the same step");
        bool rest = true;
        for (size_t i = 4; i < app.Seen.size(); i++) rest = rest && app.Seen[i] == InputFrame();
        CHECK(rest, "and nothing from the real keyboard reaches it");
    }

    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
