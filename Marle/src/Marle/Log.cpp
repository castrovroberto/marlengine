#include "mrlpch.h"
#include "Log.h"

#include <spdlog/cfg/env.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Marle {

    std::shared_ptr<spdlog::logger> Log::s_CoreLogger;
    std::shared_ptr<spdlog::logger> Log::s_ClientLogger;

    void Log::Init() 
    {
        if (s_CoreLogger) return;

        // [time] [level] NAME: message, colored by level on a terminal
        spdlog::set_pattern("%^[%T] [%l] %n: %v%$");

        s_CoreLogger = spdlog::stdout_color_mt("MARLE");
        s_CoreLogger->set_level(spdlog::level::info);

        s_ClientLogger = spdlog::stdout_color_mt("APP");
        s_ClientLogger->set_level(spdlog::level::info);

        // Per-logger overrides from the environment, e.g. SPDLOG_LEVEL=MARLE=trace,APP=debug
        // (or SPDLOG_LEVEL=trace for both)
        spdlog::cfg::load_env_levels();
    }

    std::shared_ptr<spdlog::logger>& Log::GetCoreLogger()
    {
        if (!s_CoreLogger) Init();
        return s_CoreLogger;
    }

    std::shared_ptr<spdlog::logger>& Log::GetClientLogger()
    {
        if (!s_ClientLogger) Init();
        return s_ClientLogger;
    }

}
