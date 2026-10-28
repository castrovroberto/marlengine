#pragma once

#include "mrlpch.h"

// Visibility macros for the engine as a shared library: EXPORT marks its own API, IMPORT the client's.
#if defined(_WIN32) || defined(_WIN64)
    //  Microsoft Windows
    #define EXPORT __declspec(dllexport)
    #define IMPORT __declspec(dllimport)
    #ifndef MRL_PLATFORM_WINDOWS
        #define MRL_PLATFORM_WINDOWS
    #endif
#elif defined(__APPLE__)
    //  Apple macOS/iOS
    #define EXPORT __attribute__((visibility("default")))
    #define IMPORT
    #ifndef MRL_PLATFORM_MACOS
        #define MRL_PLATFORM_MACOS
    #endif
#elif defined(__linux__)
    //  Linux
    #define EXPORT __attribute__((visibility("default")))
    #define IMPORT
    #ifndef MRL_PLATFORM_LINUX
        #define MRL_PLATFORM_LINUX
    #endif
#else
    //  No known platform: no dynamic-linkage visibility macros.
    #define EXPORT
    #define IMPORT
#endif

#define BIT(x) (1 << x)
