#include "StatsLayer.h"

#include <imgui.h>

StatsLayer::StatsLayer(const WorldLayer& world)
    : Layer("Stats"), m_World(world)
{
}

void StatsLayer::OnImGuiRender()
{
    using Marle::Renderer2D;

    // A drop in the totals means they were reset since last frame, so the new totals are this frame's.
    Renderer2D::Statistics total = Renderer2D::GetStats();
    uint32_t drawCalls = total.DrawCalls >= m_PrevStats.DrawCalls ? total.DrawCalls - m_PrevStats.DrawCalls : total.DrawCalls;
    uint32_t quads = total.QuadCount >= m_PrevStats.QuadCount ? total.QuadCount - m_PrevStats.QuadCount : total.QuadCount;
    m_PrevStats = total;

    if (!m_Visible) return;

    ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Stats", &m_Visible, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%.1f FPS (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);

        ImGui::SeparatorText("Renderer2D (last frame)");
        ImGui::Text("Draw calls: %u", drawCalls);
        ImGui::Text("Quads: %u", quads);

        ImGui::SeparatorText("Camera");
        ImGui::Text("Zoom: %.2fx", m_World.GetZoom());
        ImGui::Text("Rotation: %.1f deg", glm::degrees(m_World.GetCameraRotation()));
        ImGui::Text("Player: %.0f, %.0f", m_World.GetPlayerPosition().x, m_World.GetPlayerPosition().y);

        ImGui::TextDisabled("F1 toggles this panel");
    }
    ImGui::End();
}

void StatsLayer::OnEvent(Marle::Event& e)
{
    if (e.GetEventType() != Marle::EventType::KeyPressed) return;

    auto& key = static_cast<Marle::KeyPressedEvent&>(e);
    if (key.GetKeyCode() == Marle::Key::F1 && key.GetRepeatCount() == 0) {
        m_Visible = !m_Visible;
        e.SetHandled();
    }
}
