#pragma once

#include "Core.h"

#include <memory>
#include <spdlog/spdlog.h>

namespace Marle 
{
    // Two named loggers writing to the console: "MARLE" for the engine and "APP" for clients.
    // Messages use fmt syntax: MRL_CORE_INFO("Loaded {} ({}x{})", path, w, h).
    // Init() is optional and idempotent; the first use of either logger initializes it.
    // Both loggers start at info level; override with SPDLOG_LEVEL, e.g. SPDLOG_LEVEL=MARLE=trace,APP=debug.
    class EXPORT Log
    {
        public:
            static void Init(); 

            static std::shared_ptr<spdlog::logger>& GetCoreLogger();
            static std::shared_ptr<spdlog::logger>& GetClientLogger();

        private:
            static std::shared_ptr<spdlog::logger> s_CoreLogger;
            static std::shared_ptr<spdlog::logger> s_ClientLogger;
    };
}

// Engine log macros
#define MRL_CORE_TRACE(...)   ::Marle::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define MRL_CORE_INFO(...)    ::Marle::Log::GetCoreLogger()->info(__VA_ARGS__)
#define MRL_CORE_WARN(...)    ::Marle::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define MRL_CORE_ERROR(...)   ::Marle::Log::GetCoreLogger()->error(__VA_ARGS__)
#define MRL_CORE_FATAL(...)   ::Marle::Log::GetCoreLogger()->critical(__VA_ARGS__)

// Client log macros
#define MRL_TRACE(...)        ::Marle::Log::GetClientLogger()->trace(__VA_ARGS__)
#define MRL_INFO(...)         ::Marle::Log::GetClientLogger()->info(__VA_ARGS__)
#define MRL_WARN(...)         ::Marle::Log::GetClientLogger()->warn(__VA_ARGS__)
#define MRL_ERROR(...)        ::Marle::Log::GetClientLogger()->error(__VA_ARGS__)
#define MRL_FATAL(...)        ::Marle::Log::GetClientLogger()->critical(__VA_ARGS__)
