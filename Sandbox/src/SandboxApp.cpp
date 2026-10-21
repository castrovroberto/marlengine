#include <Marle.h>
#include <Marle/EntryPoint.h>   // defines main(); include in exactly one source file
#include "WorldLayer.h"
#include "HudLayer.h"

class Sandbox : public Marle::Application
{
public:
    Sandbox() : Marle::Application({"Glass - The Sunken Orangerie - Sandbox", 1280, 720})
    {
        auto* world = static_cast<WorldLayer*>(PushLayer(std::make_unique<WorldLayer>(*this)));
        PushOverlay(std::make_unique<HudLayer>(*this, *world));

        printf("Controls: WASD/arrows move (Shift runs), Q/E rotate camera, mouse wheel zooms,\n"
               "          R resets, H toggles the HUD, Esc quits\n");
    }
};

Marle::Application* Marle::CreateApplication()
{
    return new Sandbox();
}
