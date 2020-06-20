#include <Marle.h>

class Sandbox : public Marle::Application
{
    public:
        Sandbox()
        {

        }

        ~Sandbox()
        {

        }
};

Marle::Application* Marle::CreateApplication()
{
    return new Sandbox();
}