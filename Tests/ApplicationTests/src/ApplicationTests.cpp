// Application drives its layers. Needs a display and GL 3.3; run from the repo root.
// Exits non-zero if any check fails.
#include "Marle/Application.h"
#include <glad/gl.h>
#include <cstdio>
#include <string>

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

    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
