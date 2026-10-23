// ImGuiLayer: frame hook, event routing into ImGui IO, and drawing. Needs a display and GL 3.3;
// run from the repo root. Exits non-zero if any check fails.
#include "Marle/Application.h"
#include "Marle/ImGui/ImGuiLayer.h"
#include "Marle/Events/KeyEvent.h"
#include "Marle/Events/MouseEvent.h"
#include "Marle/Core/KeyCodes.h"
#include "Marle/Core/MouseCodes.h"
#include <imgui.h>
#include <glad/gl.h>
#include <cstdio>

using namespace Marle;

static int fails = 0;
#define CHECK(c, name) do { if (!(c)) { fails++; printf("FAIL: %s\n", name); } else { printf("ok:   %s\n", name); } } while (0)

static int Frame = 0;                  // advanced by Driver, which runs just below the ImGuiLayer
static int HookCallsBelow = 0;         // OnImGuiRender calls on Driver
static int HookCallsAbove = 0;         // OnImGuiRender calls on Probe, which sits above the ImGuiLayer
static ImGuiLayer* Imgui = nullptr;

static int ViewportHeight()
{
    GLint v[4];
    glGetIntegerv(GL_VIEWPORT, v);
    return v[3];
}

// Feeds events into the ImGuiLayer just before it begins each frame, then checks what ImGui saw.
struct Driver : Layer {
    Application& App;
    Driver(Application& app) : Layer("Driver"), App(app) {}

    void OnRender(double) override
    {
        // Marle mouse coordinates have their origin at the bottom-left
        MouseMovedEvent outside(350.0f, 20.0f);                                // bottom-right, outside the ImGui window
        MouseMovedEvent inside(100.0f, (float)ViewportHeight() - 100.0f);     // inside the ImGui window at 0..200
        KeyPressedEvent keyDown(Key::A, 0);
        KeyReleasedEvent keyUp(Key::A);

        // Events are fed just before the ImGuiLayer begins the frame, so OnImGuiRender below sees them.
        // Outside-the-window cases run first: clicking an ImGui window focuses it for the rest of the run.
        if (Frame == 1) Imgui->OnEvent(outside);
        if (Frame == 2) Imgui->OnEvent(keyDown);
        if (Frame == 3) { Imgui->OnEvent(keyUp); Imgui->OnEvent(inside); }
        if (Frame >= 8) App.Close();
    }

    void OnImGuiRender() override
    {
        HookCallsBelow++;
        ImGuiIO& io = ImGui::GetIO();

        // ImGui normally applies one button/key change per frame; apply them all at once so the
        // events fed each frame are visible in that same frame.
        io.ConfigInputTrickleEventQueue = false;

        if (Frame == 0) {
            CHECK(io.DisplaySize.x > 0 && io.DisplaySize.y > 0, "display size comes from the GL viewport");
        }
        if (Frame == 1) {
            CHECK(io.MousePos.x == 350.0f && io.MousePos.y == io.DisplaySize.y - 20.0f, "mouse position is flipped to a top-left origin");
            CHECK(!io.WantCaptureMouse, "ImGui leaves the mouse alone away from its windows");
            MouseButtonPressedEvent press(Mouse::Left);
            Imgui->OnEvent(press);
            CHECK(!press.IsHandled(), "a click outside ImGui windows is not handled");
            MouseButtonReleasedEvent release(Mouse::Left);
            Imgui->OnEvent(release);
        }
        if (Frame == 2) {
            CHECK(ImGui::IsKeyDown(ImGuiKey_A), "a Marle key press reaches ImGui");
            CHECK(!io.WantCaptureKeyboard, "ImGui does not capture the keyboard without a focused text field");
            KeyPressedEvent other(Key::B, 0);
            Imgui->OnEvent(other);
            CHECK(!other.IsHandled(), "key events pass through when ImGui does not want the keyboard");
        }
        if (Frame == 3) {
            CHECK(!ImGui::IsKeyDown(ImGuiKey_A), "a Marle key release reaches ImGui");
        }
        if (Frame == 4) {
            CHECK(io.WantCaptureMouse, "ImGui captures the mouse over its window");
            MouseButtonPressedEvent press(Mouse::Left);
            Imgui->OnEvent(press);
            CHECK(press.IsHandled(), "a click on an ImGui window is marked handled");
            MouseButtonReleasedEvent release(Mouse::Left);
            Imgui->OnEvent(release);

            MouseScrolledEvent wheel(0.0f, 1.0f);
            Imgui->OnEvent(wheel);
            CHECK(wheel.IsHandled(), "the wheel over an ImGui window is handled by default");
            // a layer showing a picture of the world asks for the mouse back while it is hovered
            ImGui::SetNextFrameWantCaptureMouse(false);
        }
        if (Frame == 5) {
            CHECK(!io.WantCaptureMouse, "SetNextFrameWantCaptureMouse(false) releases the mouse over a hovered window");
            MouseScrolledEvent wheel(0.0f, 1.0f);
            Imgui->OnEvent(wheel);
            CHECK(!wheel.IsHandled(), "the wheel then reaches the layers below");
        }

        // A window to hover, and a foreground rect to read back (ImGui coordinates, top-left origin)
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(200.0f, 200.0f));
        ImGui::Begin("test window", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        ImGui::End();
        ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(240.0f, 240.0f), ImVec2(290.0f, 290.0f), IM_COL32(255, 0, 255, 255));
    }
};

// Above the ImGuiLayer, so it renders after ImGui has drawn.
struct Probe : Layer {
    unsigned char Inside[4] = {0, 0, 0, 0}, Outside[4] = {0, 0, 0, 0};
    bool Read = false;

    Probe() : Layer("Probe") {}
    void OnImGuiRender() override { HookCallsAbove++; }
    void OnRender(double) override
    {
        if (Frame == 5) {
            int h = ViewportHeight();
            glReadPixels(265, h - 265, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, Inside);   // inside the magenta rect
            glReadPixels(320, h - 265, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, Outside);  // right of it
            Read = true;
        }
        Frame++;
    }
};

class TestApp : public Application {
public:
    Probe* ProbeLayer = nullptr;
    TestApp() : Application({"imgui test", 400, 300}) {
        PushLayer(std::make_unique<Driver>(*this));
        Imgui = static_cast<ImGuiLayer*>(PushOverlay(std::make_unique<ImGuiLayer>(GetLayerStack())));
        ProbeLayer = static_cast<Probe*>(PushOverlay(std::make_unique<Probe>()));
    }
};

int main()
{
    {
        TestApp app;
        CHECK(ImGui::GetCurrentContext() != nullptr, "attaching the ImGuiLayer creates an ImGui context");

        app.Run();
        CHECK(Frame >= 8, "the loop ran every scripted frame");
        CHECK(HookCallsBelow >= 8 && HookCallsAbove >= 8, "OnImGuiRender runs on every layer, above and below the ImGuiLayer");

        const Probe& p = *app.ProbeLayer;
        CHECK(p.Read, "pixels were read back");
        CHECK(p.Inside[0] == 255 && p.Inside[1] == 0 && p.Inside[2] == 255, "ImGui draw data reaches the framebuffer");
        CHECK(!(p.Outside[0] == 255 && p.Outside[1] == 0 && p.Outside[2] == 255), "pixels outside the ImGui rect are untouched");
    }
    CHECK(ImGui::GetCurrentContext() == nullptr, "destroying the app detaches the layer and destroys the context");

    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
