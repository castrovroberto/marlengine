extern Marle::Application* Marle::CreateApplication();

int main(int argc, char** argv) {
    auto app = Marle::CreateApplication();
    app->Run();
    delete app;
}