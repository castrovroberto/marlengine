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
#include "Marle/Layer.h"
#include "Marle/LayerStack.h"
#include "Marle/ImGui/ImGuiLayer.h"

// Scene
#include "Marle/Scene/Scene.h"
#include "Marle/Scene/Components.h"
#include "Marle/Scene/ComponentRegistry.h"
#include "Marle/Scene/SceneLoader.h"
#include "Marle/Scene/SceneWriter.h"
#include "Marle/Scene/SceneSchema.h"
#include "Marle/Scene/SceneTool.h"

// Events
#include "Marle/Events/Event.h"
#include "Marle/Events/KeyEvent.h"
#include "Marle/Events/MouseEvent.h"
#include "Marle/Events/ApplicationEvent.h"

// Input
#include "Marle/Core/KeyCodes.h"
#include "Marle/Core/MouseCodes.h"
#include "Marle/Input.h"

// Renderer
#include "Marle/Renderer/OrthographicCamera.h"
#include "Marle/Renderer/SubTexture2D.h"
#include "Marle/Renderer/BitmapFont.h"
#include "Marle/Renderer/Renderer2D.h"
#include "Marle/Platform/OpenGL/OpenGLTexture.h"
#include "Marle/Platform/OpenGL/OpenGLFramebuffer.h"

// Note: the entry point (main) is separate. Include <Marle/EntryPoint.h> in exactly one source file.
