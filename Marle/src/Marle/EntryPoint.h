#pragma once

#include "Marle/Application.h"
#include "Marle/Log.h"
#include "Marle/CommandLine.h"

#if defined(MRL_PLATFORM_LINUX) || defined(MRL_PLATFORM_MACOS) || defined(MRL_PLATFORM_WINDOWS)

extern Marle::Application* Marle::CreateApplication();

int main(int argc, char** argv) {

    Marle::SetCommandLine(argc, argv);
    Marle::Log::Init();

    auto app = Marle::CreateApplication();
    app->Run();
    delete app;
}

#endif
