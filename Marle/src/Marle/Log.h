#pragma once

/*
 * Core.h
 * Marle Core library header
 */
#include "Core.h"

/*
 * namespace Marle
 * Namespace aliases allow the programmer to define an alternate name for a namespace.
 * They are commonly used as a convenient shortcut for long or deeply-nested namespaces.
 * https://en.cppreference.com/w/cpp/language/namespace_alias
 * 
 */
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
     */
    class EXPORT Log
    {
        public:
            static void Init(); 
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
 */
// Simple log macros using printf
#define MRL_CORE_TRACE(...)   printf("CORE TRACE: "); printf(__VA_ARGS__); printf("\n")
#define MRL_CORE_INFO(...)    printf("CORE INFO: "); printf(__VA_ARGS__); printf("\n")
#define MRL_CORE_WARN(...)    printf("CORE WARN: "); printf(__VA_ARGS__); printf("\n")
#define MRL_CORE_ERROR(...)   printf("CORE ERROR: "); printf(__VA_ARGS__); printf("\n")
#define MRL_CORE_FATAL(...)   printf("CORE FATAL: "); printf(__VA_ARGS__); printf("\n")

// Client log macros
#define MRL_TRACE(...)        printf("APP TRACE: "); printf(__VA_ARGS__); printf("\n")
#define MRL_INFO(...)         printf("APP INFO: "); printf(__VA_ARGS__); printf("\n")
#define MRL_WARN(...)         printf("APP WARN: "); printf(__VA_ARGS__); printf("\n")
#define MRL_ERROR(...)        printf("APP ERROR: "); printf(__VA_ARGS__); printf("\n")
#define MRL_FATAL(...)        printf("APP FATAL: "); printf(__VA_ARGS__); printf("\n")