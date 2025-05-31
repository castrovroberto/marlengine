#include <Marle.h>

class Sandbox : public Marle::Application
{
    public:
        Sandbox() : Marle::Application({"Glass - The Sunken Orangerie", 1024, 768})
        {
            printf("Sandbox Application created.\n");
        }

        ~Sandbox()
        {
            printf("Sandbox Application destroyed.\n");
        }
};

Marle::Application* Marle::CreateApplication()
{
    return new Sandbox();
}