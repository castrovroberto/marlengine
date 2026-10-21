#include "WorldLayer.h"
#include <cmath>

static constexpr int MapWidth = 32, MapHeight = 24;
static constexpr float TileSize = 64.0f;
static constexpr float PlayerSize = 48.0f;
static constexpr float WalkSpeed = 300.0f;      // world units per second
static constexpr float CameraTurnSpeed = 1.0f;  // radians per second
static constexpr int SheetColumns = 4;

static const glm::vec2 WorldCenter(MapWidth * TileSize * 0.5f, MapHeight * TileSize * 0.5f);

WorldLayer::WorldLayer(Marle::Application& app)
    : Layer("World"), m_App(app)
{
    // Deterministic map: grass and earth variants around a pond, accent tiles along the edge.
    m_Map.resize(MapWidth * MapHeight);
    for (int y = 0; y < MapHeight; y++) {
        for (int x = 0; x < MapWidth; x++) {
            unsigned h = (unsigned)(x * 73856093) ^ (unsigned)(y * 19349663);
            h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15;

            float dx = x - MapWidth * 0.5f + 0.5f, dy = y - MapHeight * 0.5f + 0.5f;
            int row, column = h % SheetColumns;
            if (dx * dx + dy * dy < 20.0f)                                           row = 2;   // pond
            else if (x == 0 || y == 0 || x == MapWidth - 1 || y == MapHeight - 1)    row = 3;   // border
            else                                                                      row = (h >> 8) % 4 == 0 ? 1 : 0;
            m_Map[y * MapWidth + x] = row * SheetColumns + column;
        }
    }
    Reset();
}

void WorldLayer::Reset()
{
    m_Player = m_PlayerPrev = WorldCenter + glm::vec2(0.0f, -250.0f);
    m_CameraRotation = 0.0f;
    m_Zoom = 1.0f;
}

void WorldLayer::OnAttach()
{
    m_Sheet = std::make_unique<Marle::OpenGLTexture2D>("Assets/Textures/test_sheet.tga");
    m_PlayerTexture = std::make_unique<Marle::OpenGLTexture2D>("Assets/Textures/test_sprite.tga");

    for (int row = 0; row < SheetColumns; row++)
        for (int column = 0; column < SheetColumns; column++)
            m_Cells.push_back(Marle::SubTexture2D::CreateFromCoords(m_Sheet.get(), { column, row }, { 32.0f, 32.0f }));
}

void WorldLayer::OnDetach()
{
    // Textures hold GL objects: release them while the context is still alive.
    m_Cells.clear();
    m_Sheet.reset();
    m_PlayerTexture.reset();
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

    const float margin = TileSize + PlayerSize * 0.5f;   // stay inside the border tiles
    m_Player.x = glm::clamp(m_Player.x, margin, MapWidth * TileSize - margin);
    m_Player.y = glm::clamp(m_Player.y, margin, MapHeight * TileSize - margin);

    m_SpinPrev = m_Spin;
    m_Spin += 2.0f * (float)dt;
}

void WorldLayer::OnRender(double alpha)
{
    using Marle::Renderer2D;
    using Marle::Input;

    float a = (float)alpha;
    glm::vec2 player = glm::mix(m_PlayerPrev, m_Player, a);
    float spin = glm::mix(m_SpinPrev, m_Spin, a);

    // The visible area shrinks as we zoom in; the camera sits on the player.
    float halfW = (float)m_App.GetWidth() * 0.5f / m_Zoom;
    float halfH = (float)m_App.GetHeight() * 0.5f / m_Zoom;
    Marle::OrthographicCamera camera(-halfW, halfW, -halfH, halfH);
    camera.SetPosition(player);
    camera.SetRotation(m_CameraRotation);

    Renderer2D::BeginScene(camera);

    for (int y = 0; y < MapHeight; y++)
        for (int x = 0; x < MapWidth; x++)
            Renderer2D::DrawQuad({ (x + 0.5f) * TileSize, (y + 0.5f) * TileSize }, { TileSize, TileSize }, m_Cells[m_Map[y * MapWidth + x]]);

    // Spinning accent tiles around the pond
    for (int i = 0; i < 4; i++) {
        float angle = spin * (i % 2 ? 1.0f : -1.0f) + i * 1.5707963f;
        glm::vec2 at = WorldCenter + 170.0f * glm::vec2(cosf(i * 1.5707963f), sinf(i * 1.5707963f));
        Renderer2D::DrawRotatedQuad(at, { 56.0f, 56.0f }, angle, m_Cells[15]);
    }

    // Player (shift = running = orange tint), with a satellite orbiting it
    glm::vec4 tint = Input::IsKeyPressed(Marle::Key::LeftShift) ? glm::vec4(1.0f, 0.65f, 0.3f, 1.0f) : glm::vec4(1.0f);
    Renderer2D::DrawQuad(player, { PlayerSize, PlayerSize }, m_PlayerTexture.get(), tint);
    Renderer2D::DrawRotatedQuad(player + 44.0f * glm::vec2(cosf(spin), sinf(spin)), { 16.0f, 16.0f }, spin * 3.0f, glm::vec4(1.0f, 0.9f, 0.2f, 1.0f));

    Renderer2D::EndScene();
}

void WorldLayer::OnEvent(Marle::Event& e)
{
    if (e.GetEventType() == Marle::EventType::MouseScrolled) {
        auto& scroll = static_cast<Marle::MouseScrolledEvent&>(e);
        m_Zoom = glm::clamp(m_Zoom * powf(1.1f, scroll.GetYOffset()), 0.25f, 4.0f);
    }
    else if (e.GetEventType() == Marle::EventType::KeyPressed) {
        auto& key = static_cast<Marle::KeyPressedEvent&>(e);
        if (key.GetKeyCode() == Marle::Key::R && key.GetRepeatCount() == 0) {
            Reset();
            e.SetHandled();
        }
    }
}
