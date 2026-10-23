#pragma once

#include <Marle.h>
#include <memory>

// A scrolling tile world with a player, viewed through a camera that follows the player (WASD or arrows
// move, Shift runs, Q/E turn the camera, the wheel zooms, R restarts). The world is data, and so is what
// happens in it: Assets/Scenes/world.json holds the entities and their behaviors (KeyboardMove, Follow,
// Orbit, Spin, ...), which Simulation::Step runs each fixed step. This layer loads the scene, steps it,
// draws it (into a viewport panel when ImGui is there), reloads it when the file changes (keeping what the
// simulation had worked out) and restarts it when the scene asks for that.
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

    // Read from the scene, for the HUD and the stats panel
    float GetZoom() const;
    float GetCameraRotation() const;   // radians
    glm::vec2 GetPlayerPosition() const;

private:
    // Reads the scene file; problems are logged, and a file that cannot be used at all leaves an empty
    // scene (everything then does nothing, safely)
    void LoadWorld();

    // Checks whether the scene file changed (or `force`) and swaps in the new scene if it is fully valid.
    // With `carry` the simulation carries on from where it was (CarryOverState); without it the scene
    // starts from the file, which is what a restart is.
    void PollScene(bool force = false, bool carry = true, const char* what = "Reloaded");
    void RestartWorld();
    void DrawScenePanel();

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
    std::unique_ptr<Marle::SceneReloader> m_Reloader;
    int m_ReloadCountdown = 0;      // fixed steps until the file is checked again
    bool m_ShowScenePanel = true;   // F3
    std::unique_ptr<Marle::Scene> m_Scene;
    std::unique_ptr<Marle::Scene> m_Baseline;   // the file as it was last loaded, never run: what a reload compares with
    std::unique_ptr<Marle::SceneRenderer> m_Renderer;
};
