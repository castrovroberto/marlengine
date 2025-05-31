#pragma once

#include "mrlpch.h"

/*
 * defined 
 * Condition evaluation
 * https://en.cppreference.com/w/cpp/preprocessor/conditional
 * The expression may contain unary operators in form defined identifier or defined (identifier). 
 * The result is 1 if the identifier was defined as a macro name or the identifier is 
 * __has_include (since C++17), otherwise the result is ​0​.
 * 
 * Checking for OS (platform)
 *      Linux and Linux-derived             __linux__
 *      Android                             __ANDROID__ (implies __linux__)
 *      Linux (non-Android)                 __linux__ && !__ANDROID__
 *      Darwin (Mac OS X and iOS)           __APPLE__
 *      Akaros (http://akaros.org)          __ros__
 *      Windows                             _WIN32
 *      Windows 64 bit                      _WIN64 (implies _WIN32)
 *      NaCL                                __native_client__
 *      AsmJS                               __asmjs__
 *      Fuschia                             __Fuchsia__
 * 
 * Checking the compiler:
 *      Visual Studio                       _MSC_VER
 *      gcc                                 __GNUC__
 *      clang                               __clang__
 *      emscripten                          __EMSCRIPTEN__ (for asm.js and webassembly)
 *      MinGW 32                            __MINGW32__
 *      MinGW-w64 32bit                     __MINGW32__
 *      MinGW-w64 64bit                     __MINGW64__
 */
#if defined(_WIN32) || defined(_WIN64)
    //  Microsoft Windows
    #define EXPORT __declspec(dllexport)
    #define IMPORT __declspec(dllimport)
    #define MRL_PLATFORM_WINDOWS
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
    #define MRL_PLATFORM_LINUX
#else
    //  do nothing and hope for the best?
    #define EXPORT
    #define IMPORT
    #pragma warning Unknown dynamic link import/export semantics.
#endif

#define BIT(x) (1 << x) 