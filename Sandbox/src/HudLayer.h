#pragma once

#include "WorldLayer.h"

// Pixel-space overlay: key indicators, a mouse reticle and a zoom bar, all drawn from
// polled input. Pressing H toggles it; the HUD consumes that key so layers below never see it.
class HudLayer : public Marle::Layer
{
public:
    HudLayer(Marle::Application& app, const WorldLayer& world);

    void OnUpdate(double fixed_dt) override;
    void OnRender(double interpolation_alpha) override;
    void OnEvent(Marle::Event& e) override;

private:
    Marle::Application& m_App;
    const WorldLayer& m_World;
    bool m_Visible = true;
    int m_UpdateCount = 0;
};
