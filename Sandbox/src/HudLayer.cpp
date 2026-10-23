#include "HudLayer.h"

HudLayer::HudLayer(Marle::Application& app, const WorldLayer& world)
    : Layer("Hud"), m_App(app), m_World(world)
{
}

void HudLayer::OnAttach()
{
    m_Font = std::make_unique<Marle::BitmapFont>("Assets/Fonts/mono.png", 16, 32);
}

void HudLayer::OnDetach()
{
    m_Font.reset();   // holds a GL texture: release it while the context is still alive
}

void HudLayer::OnUpdate(double)
{
    m_UpdateCount++;
}

void HudLayer::OnRender(double)
{
    using Marle::Input;
    using Marle::Renderer2D;
    namespace Key = Marle::Key;

    if (m_Visible) {
        const glm::vec4 off(0.18f, 0.18f, 0.24f, 1.0f), on(0.95f, 0.85f, 0.25f, 1.0f);
        auto lit = [&](bool pressed) { return pressed ? on : off; };

        Renderer2D::BeginBatch((float)m_App.GetWidth(), (float)m_App.GetHeight());

        // WASD cluster (arrows light the same keys)
        const float k = 30.0f, gap = 6.0f, x0 = 40.0f, y0 = 70.0f;

        // Translucent backing panels go first: quads blend in submission order
        const glm::vec4 panel(0.0f, 0.0f, 0.0f, 0.45f);
        Renderer2D::DrawQuad({ 125.0f, 72.0f }, { 220.0f, 120.0f }, panel);
        Renderer2D::DrawQuad({ 140.0f, (float)m_App.GetHeight() - 30.0f }, { 240.0f, 36.0f }, panel);
        Renderer2D::DrawQuad({ x0 + k + gap, y0 + k + gap }, { k, k }, lit(Input::IsKeyPressed(Key::W) || Input::IsKeyPressed(Key::Up)));
        Renderer2D::DrawQuad({ x0,           y0 },           { k, k }, lit(Input::IsKeyPressed(Key::A) || Input::IsKeyPressed(Key::Left)));
        Renderer2D::DrawQuad({ x0 + k + gap, y0 },           { k, k }, lit(Input::IsKeyPressed(Key::S) || Input::IsKeyPressed(Key::Down)));
        Renderer2D::DrawQuad({ x0 + 2 * (k + gap), y0 },     { k, k }, lit(Input::IsKeyPressed(Key::D) || Input::IsKeyPressed(Key::Right)));
        Renderer2D::DrawQuad({ x0 + k + gap, y0 - k - gap - 4.0f }, { 3 * k + 2 * gap, 14.0f }, lit(Input::IsKeyPressed(Key::LeftShift)));

        // Camera keys
        Renderer2D::DrawQuad({ x0 + 3 * (k + gap) + 20.0f, y0 }, { k, k }, lit(Input::IsKeyPressed(Key::Q)));
        Renderer2D::DrawQuad({ x0 + 4 * (k + gap) + 20.0f, y0 }, { k, k }, lit(Input::IsKeyPressed(Key::E)));

        // Zoom bar along the top: 25% .. 400%, marker at the current zoom
        const float barW = 200.0f, top = (float)m_App.GetHeight() - 30.0f;
        float t = (glm::log(m_World.GetZoom()) - glm::log(0.25f)) / (glm::log(4.0f) - glm::log(0.25f));
        Renderer2D::DrawQuad({ 40.0f + barW * 0.5f, top }, { barW, 6.0f }, off);
        Renderer2D::DrawQuad({ 40.0f + barW * t, top }, { 8.0f, 20.0f }, on);

        // Labels above the key cluster
        char zoom[32];
        snprintf(zoom, sizeof(zoom), "ZOOM %.2fx", m_World.GetZoom());
        Renderer2D::DrawString(*m_Font, zoom, { 20.0f, 164.0f }, 22.0f, on);
        Renderer2D::DrawString(*m_Font, "H hides the HUD", { 20.0f, 142.0f }, 16.0f, glm::vec4(0.8f, 0.8f, 0.85f, 1.0f));

        // Mouse reticle at the polled position; turns red while the left button is held
        glm::vec2 mouse = Input::GetMousePosition();
        glm::vec4 reticle = Input::IsMouseButtonPressed(Marle::Mouse::Left) ? glm::vec4(1.0f, 0.2f, 0.2f, 1.0f) : glm::vec4(1.0f);
        Renderer2D::DrawQuad(mouse, { 22.0f, 2.0f }, reticle);
        Renderer2D::DrawQuad(mouse, { 2.0f, 22.0f }, reticle);

        Renderer2D::EndBatch();
    }

    // Once a second, report how many draw calls the frame took
    if (m_UpdateCount >= 60) {
        m_UpdateCount = 0;
        auto stats = Renderer2D::GetStats();
        MRL_INFO("Renderer2D: {} quads in {} draw calls (last second)", stats.QuadCount, stats.DrawCalls);
        Renderer2D::ResetStats();
    }
}

void HudLayer::OnEvent(Marle::Event& e)
{
    if (e.GetEventType() != Marle::EventType::KeyPressed) return;

    auto& key = static_cast<Marle::KeyPressedEvent&>(e);
    if (key.GetKeyCode() == Marle::Key::H && key.GetRepeatCount() == 0) {
        m_Visible = !m_Visible;
        e.SetHandled();
    }
}
