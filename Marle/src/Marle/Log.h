#pragma once

/*
 * <memory>
 * Standard library header <memory>
 * This header is part of the dynamic memory management library.
 * https://en.cppreference.com/w/cpp/header/memory
 * 
 * memory::shared_ptr
 * smart pointer with shared object ownership semantics
 * 
/**/
#include <memory>

/*
 * Core.h
 * Marle Core library header
/**/
#include "Core.h"

/*
 * spdlog headers
 * Vendor logger library spdlog
/**/
#include "spdlog/spdlog.h"
#include "spdlog/fmt/ostr.h"

/*
 * namespace Marle
 * Namespace aliases allow the programmer to define an alternate name for a namespace.
 * They are commonly used as a convenient shortcut for long or deeply-nested namespaces.
 * https://en.cppreference.com/w/cpp/language/namespace_alias
 * 
/**/
namespace Marle 
{
    /*
     * inline specifier
     * The inline specifier, when used in a function's decl-specifier-seq, declares the function to be an inline function.
     * https://en.cppreference.com/w/cpp/language/inline
     * 
     * The original intent of the inline keyword was to serve as an indicator 
     * to the optimizer that inline substitution of a function is preferred over function call, 
     * that is, instead of executing the function call CPU instruction to transfer control 
     * to the function body, a copy of the function body is executed without generating the call. 
     * 
     * This avoids overhead created by the function call (passing the arguments and retrieving the result) 
     * but it may result in a larger executable as the code for the function has to be repeated multiple times.
     * 
    /**/
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

/*
 * Replacing text macros
 * The preprocessor supports text macro replacement. Function-like text macro replacement is also supported.
 * https://en.cppreference.com/w/cpp/preprocessor/replace
 * 
 * Function-like macros replace each occurrence of defined identifier with replacement-list, 
 * additionally taking a number of arguments, which then replace corresponding occurrences 
 * of any of the parameters in the replacement-list.
 * 
/**/
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