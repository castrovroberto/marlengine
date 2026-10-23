#pragma once

#include <Marle.h>
#include <memory>
#include <vector>

// A scrolling tile world with a player, driven by polled input and viewed through a camera that
// follows the player (Q/E rotate, mouse wheel zooms, R resets). The world itself is data: entities in
// Assets/Scenes/world.json, drawn by a SceneRenderer. The layer finds the ones it moves by name
// (camera, world, player, satellite, spinner_0 to spinner_3) and writes their Transforms each frame.
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

    float GetZoom() const;
    float GetCameraRotation() const { return m_CameraRotation; }   // radians
    glm::vec2 GetPlayerPosition() const { return m_Player; }

private:
    // Reads the scene file and finds the entities this layer drives; problems are logged, and a file
    // that cannot be used at all leaves an empty scene (everything is then skipped safely)
    void LoadWorld();
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

    Marle::ComponentRegistry m_Registry;
    std::unique_ptr<Marle::Scene> m_Scene;
    std::unique_ptr<Marle::SceneRenderer> m_Renderer;
    Marle::Entity m_CameraEntity, m_WorldEntity, m_PlayerEntity, m_SatelliteEntity;
    std::vector<Marle::Entity> m_Spinners;

    // Where the scene file put things, for Reset()
    glm::vec2 m_PlayerStart = { 0.0f, 0.0f };
    float m_CameraRotationStart = 0.0f, m_ZoomStart = 1.0f;

    // Simulation state is kept for the previous and current fixed step so rendering
    // can interpolate between them; the scene's Transforms are written from it each frame.
    glm::vec2 m_PlayerPrev, m_Player;
    float m_SpinPrev = 0.0f, m_Spin = 0.0f;

    float m_CameraRotation = 0.0f;
};
