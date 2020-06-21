#pragma once

#include <memory>

#include "Core.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"

namespace Marle 
{
    class EXPORT Log
    {
        public:
            static void Init();
            inline static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return s_CoreLogger;}
            inline static std::shared_ptr<spdlog::logger>& GetClientLogger() { return s_ClientLogger;}
        private:
            static std::shared_ptr<spdlog::logger> s_CoreLogger;
            static std::shared_ptr<spdlog::logger> s_ClientLogger;
    };
}

// Core log macros
#define MRL_CORE_TRACE(...)   ::Marle::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define MRL_CORE_INFO(...)    ::Marle::Log::GetCoreLogger()->info(__VA_ARGS__)
#define MRL_CORE_WARN(...)    ::Marle::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define MRL_CORE_ERROR(...)   ::Marle::Log::GetCoreLogger()->error(__VA_ARGS__)
#define MRL_CORE_FATAL(...)   ::Marle::Log::GetCoreLogger()->fatal(__VA_ARGS__)

// Client log macros
#define MRL_TRACE(...)        ::Marle::Log::GetClientLogger()->trace(__VA_ARGS__)
#define MRL_INFO(...)         ::Marle::Log::GetClientLogger()->info(__VA_ARGS__)
#define MRL_WARN(...)         ::Marle::Log::GetClientLogger()->warn(__VA_ARGS__)
#define MRL_ERROR(...)        ::Marle::Log::GetClientLogger()->error(__VA_ARGS__)
#define MRL_FATAL(...)        ::Marle::Log::GetClientLogger()->fatal(__VA_ARGS__)