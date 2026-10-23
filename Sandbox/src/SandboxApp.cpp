#include <Marle.h>
#include <Marle/EntryPoint.h>   // defines main(); include in exactly one source file
#include "WorldLayer.h"
#include "HudLayer.h"
#include "StatsLayer.h"

class Sandbox : public Marle::Application
{
public:
    Sandbox() : Marle::Application({"Glass - The Sunken Orangerie - Sandbox", 1280, 720})
    {
        auto* world = static_cast<WorldLayer*>(PushLayer(std::make_unique<WorldLayer>(*this)));
        PushOverlay(std::make_unique<HudLayer>(*this, *world));
        PushOverlay(std::make_unique<StatsLayer>(*world));
        PushOverlay(std::make_unique<Marle::ImGuiLayer>(GetLayerStack()));   // topmost: sees events first, draws last

        MRL_INFO("Controls: WASD/arrows move (Shift runs), Q/E rotate camera, mouse wheel zooms, R resets, H toggles the HUD, F1 toggles the stats panel, F2 toggles the viewport panel, Esc quits");
    }
};

Marle::Application* Marle::CreateApplication()
{
    return new Sandbox();
}
