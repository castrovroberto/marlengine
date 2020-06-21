#pragma once

#ifdef MRL_PLATFORM_LINUX

extern Marle::Application* Marle::CreateApplication();

int main(int argc, char** argv) {

    Marle::Log::Init();
    MRL_CORE_WARN("Initialized Log!");

    int a = 5;
    MRL_INFO("Hello! Var={0}", a);

    auto app = Marle::CreateApplication();
    app->Run();
    delete app;
}

#endif