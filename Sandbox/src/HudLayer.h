#pragma once

#include "WorldLayer.h"

// Pixel-space overlay: key indicators, a mouse reticle, a zoom bar and its readout, all drawn from
// polled input. Pressing H toggles it; the HUD consumes that key so layers below never see it.
class HudLayer : public Marle::Layer
{
public:
    HudLayer(Marle::Application& app, const WorldLayer& world);

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(double fixed_dt) override;
    void OnRender(double interpolation_alpha) override;
    void OnEvent(Marle::Event& e) override;

private:
    Marle::Application& m_App;
    const WorldLayer& m_World;
    std::unique_ptr<Marle::BitmapFont> m_Font;
    bool m_Visible = true;
    int m_UpdateCount = 0;
};
