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

    float GetZoom() const { return m_Zoom; }

private:
    void Reset();

    Marle::Application& m_App;

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
