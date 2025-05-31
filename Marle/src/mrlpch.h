#pragma once

// Basic C headers that should work
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Try basic C++ headers
#ifdef __cplusplus
extern "C" {
#endif

// Basic functionality without STL for now
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

#ifdef __cplusplus
}
#endif