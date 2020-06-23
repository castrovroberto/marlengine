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


#include <iostream>
#include <utility>
#include <algorithm>

/*
 * Standard library header <functional>
 * This header is part of the function objects library and provides the standard hash function.
 * https://en.cppreference.com/w/cpp/header/functional
 * 
/**/
#include <functional>

/*
 * Standard library header <string>
 * This header is part of the strings library.
 * https://en.cppreference.com/w/cpp/header/string
 * 
/**/
#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#ifdef MRL_PLATFORM_WINDOWS
    #inclide <Windows.h>
#endif