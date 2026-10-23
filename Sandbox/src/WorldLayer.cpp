#include "WorldLayer.h"
#include <cmath>
#include <imgui.h>

static constexpr const char* WorldFile = "Assets/Scenes/world.json";
static constexpr float WalkSpeed = 300.0f;      // world units per second
static constexpr float CameraTurnSpeed = 1.0f;  // radians per second
static constexpr float SatelliteDistance = 44.0f;
static constexpr float HalfPi = 1.5707963f;

WorldLayer::WorldLayer(Marle::Application& app)
    : Layer("World"), m_App(app)
{
    Marle::RegisterBuiltinComponents(m_Registry);
}

void WorldLayer::LoadWorld()
{
    Marle::SceneLoadResult result = Marle::LoadScene(WorldFile, m_Registry);
    for (const Marle::SceneProblem& problem : result.Problems)
        MRL_ERROR("{}", problem.ToString());
    m_Scene = result.Scene ? std::move(result.Scene) : std::make_unique<Marle::Scene>("Empty");

    m_CameraEntity = m_Scene->FindEntity("camera");
    m_WorldEntity = m_Scene->FindEntity("world");
    m_PlayerEntity = m_Scene->FindEntity("player");
    m_SatelliteEntity = m_Scene->FindEntity("satellite");
    m_Spinners.clear();
    for (int i = 0; i < 4; i++)
        m_Spinners.push_back(m_Scene->FindEntity("spinner_" + std::to_string(i)));

    if (!m_PlayerEntity) MRL_ERROR("{} has no entity named 'player'", WorldFile);

    m_PlayerStart = m_PlayerEntity.Has<Marle::Transform>() ? m_PlayerEntity.Get<Marle::Transform>().Position : glm::vec2(0.0f);
    m_CameraRotationStart = m_CameraEntity.Has<Marle::Transform>() ? m_CameraEntity.Get<Marle::Transform>().Rotation : 0.0f;
    m_ZoomStart = m_CameraEntity.Has<Marle::Camera>() ? m_CameraEntity.Get<Marle::Camera>().Zoom : 1.0f;
    Reset();
}

float WorldLayer::GetZoom() const
{
    const Marle::Camera* camera = m_CameraEntity.TryGet<Marle::Camera>();
    return camera ? camera->Zoom : 1.0f;
}

void WorldLayer::Reset()
{
    m_Player = m_PlayerPrev = m_PlayerStart;
    m_CameraRotation = m_CameraRotationStart;
    if (Marle::Camera* camera = m_CameraEntity.TryGet<Marle::Camera>())
        camera->Zoom = m_ZoomStart;
}

void WorldLayer::OnAttach()
{
    m_Renderer = std::make_unique<Marle::SceneRenderer>();
    LoadWorld();
}

void WorldLayer::OnDetach()
{
    // The renderer holds the scene's textures, and the framebuffer a GL object: release them while
    // the context is still alive.
    m_Viewport.reset();
    m_Renderer.reset();
}

void WorldLayer::OnUpdate(double dt)
{
    using Marle::Input;
    namespace Key = Marle::Key;

    glm::vec2 move(0.0f);
    if (Input::IsKeyPressed(Key::W) || Input::IsKeyPressed(Key::Up))    move.y += 1.0f;
    if (Input::IsKeyPressed(Key::S) || Input::IsKeyPressed(Key::Down))  move.y -= 1.0f;
    if (Input::IsKeyPressed(Key::D) || Input::IsKeyPressed(Key::Right)) move.x += 1.0f;
    if (Input::IsKeyPressed(Key::A) || Input::IsKeyPressed(Key::Left))  move.x -= 1.0f;

    if (Input::IsKeyPressed(Key::Q)) m_CameraRotation += CameraTurnSpeed * (float)dt;
    if (Input::IsKeyPressed(Key::E)) m_CameraRotation -= CameraTurnSpeed * (float)dt;

    m_PlayerPrev = m_Player;
    if (move != glm::vec2(0.0f)) {
        // Movement is relative to the camera: W is always "up the screen".
        float c = cosf(m_CameraRotation), s = sinf(m_CameraRotation);
        glm::vec2 dir = glm::normalize(move);
        dir = { dir.x * c - dir.y * s, dir.x * s + dir.y * c };

        float speed = WalkSpeed * (Input::IsKeyPressed(Key::LeftShift) ? 2.0f : 1.0f);
        m_Player += dir * speed * (float)dt;
    }

    // Stay inside the map's border tiles
    const Marle::Tilemap* map = m_WorldEntity.TryGet<Marle::Tilemap>();
    const Marle::Transform* mapTransform = m_WorldEntity.TryGet<Marle::Transform>();
    if (map && !map->Rows.empty()) {
        const glm::vec2 origin = mapTransform ? mapTransform->Position : glm::vec2(0.0f);
        const glm::vec2 tile = map->TileSize * (mapTransform ? mapTransform->Scale : glm::vec2(1.0f));
        const Marle::Transform* player = m_PlayerEntity.TryGet<Marle::Transform>();
        const float margin = tile.x + (player ? player->Scale.x : 0.0f) * 0.5f;
        const glm::vec2 size((float)map->Rows[0].size() * tile.x, (float)map->Rows.size() * tile.y);
        m_Player.x = glm::clamp(m_Player.x, origin.x + margin, origin.x + size.x - margin);
        m_Player.y = glm::clamp(m_Player.y, origin.y + margin, origin.y + size.y - margin);
    }

    m_SpinPrev = m_Spin;
    m_Spin += 2.0f * (float)dt;
}

void WorldLayer::OnRender(double alpha)
{
    using Marle::Input;

    float a = (float)alpha;
    glm::vec2 player = glm::mix(m_PlayerPrev, m_Player, a);
    float spin = glm::mix(m_SpinPrev, m_Spin, a);

    // Render into the viewport panel's framebuffer when the panel was on screen last frame
    bool toPanel = m_PanelVisible;
    m_PanelVisible = false;   // OnImGuiRender sets it again if the panel is still on screen
    glm::vec2 target((float)m_App.GetWidth(), (float)m_App.GetHeight());
    if (toPanel) {
        target = m_PanelSize;
        uint32_t w = (uint32_t)target.x, h = (uint32_t)target.y;
        if (!m_Viewport)
            m_Viewport = std::make_unique<Marle::OpenGLFramebuffer>(w, h);
        else if (m_Viewport->GetWidth() != w || m_Viewport->GetHeight() != h)
            m_Viewport->Resize(w, h);
        toPanel = m_Viewport->IsComplete();
        if (toPanel) {
            m_Viewport->Bind();
            glClear(GL_COLOR_BUFFER_BIT);
        }
    }

    // The scene's Transforms follow the simulation, interpolated between fixed steps
    if (Marle::Transform* camera = m_CameraEntity.TryGet<Marle::Transform>()) {
        camera->Position = player;
        camera->Rotation = m_CameraRotation;
    }
    if (Marle::Transform* transform = m_PlayerEntity.TryGet<Marle::Transform>())
        transform->Position = player;
    if (Marle::SpriteRenderer* sprite = m_PlayerEntity.TryGet<Marle::SpriteRenderer>())
        sprite->Tint = Input::IsKeyPressed(Marle::Key::LeftShift) ? glm::vec4(1.0f, 0.65f, 0.3f, 1.0f) : glm::vec4(1.0f);   // running = orange
    if (Marle::Transform* satellite = m_SatelliteEntity.TryGet<Marle::Transform>()) {
        satellite->Position = player + SatelliteDistance * glm::vec2(cosf(spin), sinf(spin));
        satellite->Rotation = spin * 3.0f;
    }
    for (size_t i = 0; i < m_Spinners.size(); i++)
        if (Marle::Transform* spinner = m_Spinners[i].TryGet<Marle::Transform>())
            spinner->Rotation = spin * (i % 2 ? 1.0f : -1.0f) + (float)i * HalfPi;

    m_Renderer->Render(*m_Scene, target.x, target.y);

    if (toPanel)
        m_Viewport->Unbind();
}

void WorldLayer::OnImGuiRender()
{
    if (!m_ShowPanel) return;

    ImGui::SetNextWindowPos(ImVec2(340.0f, 40.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(900.0f, 540.0f), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    bool open = ImGui::Begin("Viewport", &m_ShowPanel, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    if (open) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        m_PanelSize = { glm::max(avail.x, 1.0f), glm::max(avail.y, 1.0f) };
        m_PanelVisible = true;

        if (m_Viewport && m_Viewport->IsComplete()) {
            // Framebuffer rows run bottom to top, so flip v
            ImGui::Image((ImTextureID)(intptr_t)m_Viewport->GetColorAttachment(), avail, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
            // Let the world see the mouse wheel (zoom) while the pointer is over the picture
            if (ImGui::IsItemHovered())
                ImGui::SetNextFrameWantCaptureMouse(false);
        }
    }
    ImGui::End();
}

void WorldLayer::OnEvent(Marle::Event& e)
{
    if (e.GetEventType() == Marle::EventType::MouseScrolled) {
        auto& scroll = static_cast<Marle::MouseScrolledEvent&>(e);
        if (Marle::Camera* camera = m_CameraEntity.TryGet<Marle::Camera>())
            camera->Zoom = glm::clamp(camera->Zoom * powf(1.1f, scroll.GetYOffset()), 0.25f, 4.0f);
    }
    else if (e.GetEventType() == Marle::EventType::KeyPressed) {
        auto& key = static_cast<Marle::KeyPressedEvent&>(e);
        if (key.GetKeyCode() == Marle::Key::R && key.GetRepeatCount() == 0) {
            Reset();
            e.SetHandled();
        }
        else if (key.GetKeyCode() == Marle::Key::F2 && key.GetRepeatCount() == 0) {
            m_ShowPanel = !m_ShowPanel;
            e.SetHandled();
        }
    }
}
