#pragma once

/*
 * #include
 * Source file inclusion
 * Includes other source file into current source file at the line immediately after the directive.
 * https://en.cppreference.com/w/cpp/preprocessor/include
 * 
 * Many compilers also implement the non-standard pragma: #pragma once with similar effects: 
 * it disables processing of a file if the same file (where file identity is determined in OS-specific way) has already been included.
 *
 * A __has_include result of 1 only means that a header or source file with the specified name exists. 
 * It does not mean that the header or source file, when included, would not cause an error or would contain anything useful.
 */

// For use by Marle applications
#include "Marle/Application.h"
#include "Marle/Log.h"
#include "Marle/Window.h"

// Events
#include "Marle/Events/Event.h"
#include "Marle/Events/KeyEvent.h"
#include "Marle/Events/MouseEvent.h"
#include "Marle/Events/ApplicationEvent.h"

// Input
#include "Marle/Core/KeyCodes.h"

// Entry point ==START==
#include "Marle/EntryPoint.h"
// Entry point == END ==