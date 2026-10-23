#pragma once

#include <Marle.h>
#include <memory>
#include <vector>

// A scrolling tile world with a player, driven by polled input and viewed through a
// camera that follows the player (Q/E rotate, mouse wheel zooms, R resets).
class WorldLayer : public Marle::Layer
{
public:
    WorldLayer(Marle::Application& app);

    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate(double fixed_dt) override;
    void OnRender(double interpolation_alpha) override;
    void OnEvent(Marle::Event& e) override;
    void OnImGuiRender() override;

    float GetZoom() const { return m_Zoom; }
    float GetCameraRotation() const { return m_CameraRotation; }   // radians
    glm::vec2 GetPlayerPosition() const { return m_Player; }

private:
    void Reset();

    Marle::Application& m_App;

    // Viewport panel: with an ImGuiLayer in the stack the world is rendered into a framebuffer
    // that OnImGuiRender shows in an ImGui window (F2 toggles it); otherwise it goes straight
    // to the window. OnImGuiRender runs after OnRender in the same frame, so the panel reports
    // its visibility and size for the next frame.
    std::unique_ptr<Marle::OpenGLFramebuffer> m_Viewport;
    bool m_ShowPanel = true;        // the user wants the panel
    bool m_PanelVisible = false;    // the panel was on screen last frame
    glm::vec2 m_PanelSize = { 900.0f, 520.0f };

    std::unique_ptr<Marle::OpenGLTexture2D> m_Sheet;
    std::unique_ptr<Marle::OpenGLTexture2D> m_PlayerTexture;
    std::vector<Marle::SubTexture2D> m_Cells;   // the 4x4 sheet, index = row * 4 + column
    std::vector<int> m_Map;                     // tile -> cell index

    // Simulation state is kept for the previous and current fixed step so rendering
    // can interpolate between them.
    glm::vec2 m_PlayerPrev, m_Player;
    float m_SpinPrev = 0.0f, m_Spin = 0.0f;

    float m_CameraRotation = 0.0f;
    float m_Zoom = 1.0f;
};
