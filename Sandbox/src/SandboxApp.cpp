#include <Marle.h>

class Sandbox : public Marle::Application
{
private:
    double m_TotalTimeElapsed = 0.0;
    int m_UpdateCount = 0;
    float m_RectPositionX = 0.0f; // Example value to change in OnUpdate

public:
    Sandbox() : Marle::Application({"Glass - The Sunken Orangerie - Game Loop Test", 1024, 768})
    {
        printf("Sandbox Application created.\n");
    }

    ~Sandbox()
    {
        printf("Sandbox Application destroyed.\n");
    }

protected:
    void OnUpdate(double fixed_dt) override {
        // Call base if it does anything important
        Marle::Application::OnUpdate(fixed_dt);

        m_TotalTimeElapsed += fixed_dt;
        m_UpdateCount++;
        m_RectPositionX += (float)(50.0 * fixed_dt); // Move 50 pixels per second
        if (m_RectPositionX > 1024.0f) m_RectPositionX = 0.0f;

        // Log roughly every second
        if (m_UpdateCount % 60 == 0) { // Assuming 60 UPS target
            printf("Sandbox::OnUpdate - Total Time: %.2fs, Updates: %d, RectX: %.2f\n", 
                   m_TotalTimeElapsed, m_UpdateCount, m_RectPositionX);
        }
    }

    void OnRender(double interpolation_alpha) override {
        // Call base if it does anything important
        Marle::Application::OnRender(interpolation_alpha);
        
        // In the future, you'd render your game scene here.
        // For example, rendering a rectangle at m_RectPositionX (interpolated with alpha if implemented)
        // printf("Sandbox::OnRender - Alpha: %.2f\n", interpolation_alpha);
    }
};

Marle::Application* Marle::CreateApplication()
{
    return new Sandbox();
}