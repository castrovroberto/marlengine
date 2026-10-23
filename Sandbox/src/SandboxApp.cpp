#include <Marle.h>
#include <Marle/EntryPoint.h>   // defines main(); include in exactly one source file
#include "WorldLayer.h"
#include "HudLayer.h"
#include "StatsLayer.h"

class Sandbox : public Marle::Application
{
    WorldLayer* m_World = nullptr;
public:
    Sandbox() : Marle::Application({"Glass - The Sunken Orangerie - Sandbox", 1280, 720})
    {
        auto* world = m_World = static_cast<WorldLayer*>(PushLayer(std::make_unique<WorldLayer>(*this)));
        PushOverlay(std::make_unique<HudLayer>(*this, *world));
        PushOverlay(std::make_unique<StatsLayer>(*world));
        PushOverlay(std::make_unique<Marle::ImGuiLayer>(GetLayerStack()));   // topmost: sees events first, draws last

        // --replay <file> plays a recording instead of the keyboard (--exit-when-done closes the game when it
        // ends); --record <file> records from the start and saves when the game closes (docs/input-recording.md)
        if (Marle::HasCommandLineFlag("--replay")) {
            if (auto file = Marle::GetCommandLineValue("--replay")) world->StartReplay(*file, Marle::HasCommandLineFlag("--exit-when-done"));
            else MRL_ERROR("--replay needs a recording file");
        }
        if (Marle::HasCommandLineFlag("--record")) {
            if (auto file = Marle::GetCommandLineValue("--record")) world->StartRecording(*file, false);
            else MRL_ERROR("--record needs a file to save to");
        }

        MRL_INFO("Controls: WASD/arrows move (Shift runs), Q/E rotate camera, mouse wheel zooms, R resets, H toggles the HUD, F1 toggles the stats panel, F2 toggles the viewport panel, F3 the scene panel, F5 records, Esc quits");
    }

protected:
    // While a replay plays, its frames stand in for the keyboard (the live input is still drained, so it does
    // not pile up for when the replay ends)
    Marle::InputFrame SampleInput() override
    {
        Marle::InputFrame live = Marle::Application::SampleInput();
        return m_World && m_World->IsReplaying() ? m_World->NextReplayFrame() : live;
    }
};

Marle::Application* Marle::CreateApplication()
{
    return new Sandbox();
}
