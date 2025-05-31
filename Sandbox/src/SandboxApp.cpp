#include <Marle.h>

class Sandbox : public Marle::Application
{
private:
    double m_TotalTimeElapsed = 0.0;
    int m_UpdateCount = 0;
    float m_RectPositionX = 0.0f; // Example value to change in OnUpdate
    float m_RectPositionY = 100.0f; // Y position for the sprite
    std::unique_ptr<Marle::OpenGLTexture2D> m_TestTexture;

public:
    Sandbox() : Marle::Application({"Glass - The Sunken Orangerie - Sprite Rendering Test", 1024, 768})
    {
        printf("Sandbox Application created.\n");
        
        // Load test texture
        m_TestTexture = std::make_unique<Marle::OpenGLTexture2D>("Assets/Textures/test_sprite.tga");
    }

    ~Sandbox()
    {
        printf("Sandbox Application destroyed.\n");
    }

    void OnEvent(Marle::Event& event) override
    {
        Marle::EventDispatcher dispatcher(event);
        dispatcher.Dispatch<Marle::KeyPressedEvent>([this](Marle::KeyPressedEvent& e) {
            return OnKeyPressed(e);
        });
        dispatcher.Dispatch<Marle::KeyReleasedEvent>([this](Marle::KeyReleasedEvent& e) {
            return OnKeyReleased(e);
        });
        dispatcher.Dispatch<Marle::KeyTypedEvent>([this](Marle::KeyTypedEvent& e) {
            return OnKeyTyped(e);
        });
    }

    bool OnKeyPressed(Marle::KeyPressedEvent& event)
    {
        printf("Key Pressed: %d (repeat: %d)\n", event.GetKeyCode(), event.GetRepeatCount());
        
        // Example: Move rectangle with arrow keys
        switch (event.GetKeyCode()) {
            case Marle::Key::Left:
                m_RectPositionX -= 10.0f;
                printf("Moving left! Position: %.2f\n", m_RectPositionX);
                break;
            case Marle::Key::Right:
                m_RectPositionX += 10.0f;
                printf("Moving right! Position: %.2f\n", m_RectPositionX);
                break;
            case Marle::Key::Space:
                printf("Space pressed! Resetting position.\n");
                m_RectPositionX = 0.0f;
                break;
            case Marle::Key::Escape:
                printf("Escape pressed! (Note: This might close the application)\n");
                break;
        }
        
        return false; // Don't consume the event
    }

    bool OnKeyReleased(Marle::KeyReleasedEvent& event)
    {
        printf("Key Released: %d\n", event.GetKeyCode());
        return false; // Don't consume the event
    }

    bool OnKeyTyped(Marle::KeyTypedEvent& event)
    {
        printf("Key Typed: %d\n", event.GetKeyCode());
        return false; // Don't consume the event
    }

protected:
    void OnUpdate(double fixed_dt) override {
        // Call base if it does anything important
        Marle::Application::OnUpdate(fixed_dt);

        m_TotalTimeElapsed += fixed_dt;
        m_UpdateCount++;

        // Log roughly every second
        if (m_UpdateCount % 60 == 0) { // Assuming 60 UPS target
            printf("Sandbox::OnUpdate - Total Time: %.2fs, Updates: %d, RectX: %.2f\n", 
                   m_TotalTimeElapsed, m_UpdateCount, m_RectPositionX);
        }
    }

    void OnRender(double interpolation_alpha) override {
        // Call base if it does anything important
        Marle::Application::OnRender(interpolation_alpha);
        
        // Begin scene for 2D rendering
        Marle::Renderer2D::BeginScene();
        
        // Draw the test sprite
        if (m_TestTexture) {
            // Draw sprite at current position with 64x64 size
            Marle::Renderer2D::DrawQuad({m_RectPositionX + 512.0f, m_RectPositionY + 384.0f}, {64.0f, 64.0f}, m_TestTexture.get());
        }
        
        // End scene
        Marle::Renderer2D::EndScene();
    }
};

Marle::Application* Marle::CreateApplication()
{
    return new Sandbox();
}