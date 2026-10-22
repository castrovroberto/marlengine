#pragma once

#include "WorldLayer.h"

// ImGui panel with frame time, Renderer2D stats and camera values. F1 toggles it.
// Needs an ImGuiLayer in the stack to be drawn.
class StatsLayer : public Marle::Layer
{
public:
    StatsLayer(const WorldLayer& world);

    void OnImGuiRender() override;
    void OnEvent(Marle::Event& e) override;

private:
    const WorldLayer& m_World;
    bool m_Visible = true;

    // Renderer2D stats accumulate until someone resets them (HudLayer does, once a second),
    // so per-frame numbers are the change since the previous frame.
    Marle::Renderer2D::Statistics m_PrevStats{};
};
