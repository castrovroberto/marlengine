#pragma once

// The single include for Marle clients: the engine's public surface.
// For use by Marle applications
#include "Marle/Application.h"
#include "Marle/CommandLine.h"
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
#include "Marle/Scene/ComponentReference.h"
#include "Marle/Scene/SceneTool.h"
#include "Marle/Scene/SceneRenderer.h"
#include "Marle/Scene/SceneReloader.h"
#include "Marle/Scene/Simulation.h"
#include "Marle/Scene/InputRecording.h"

// Events
#include "Marle/Events/Event.h"
#include "Marle/Events/KeyEvent.h"
#include "Marle/Events/MouseEvent.h"
#include "Marle/Events/ApplicationEvent.h"

// Input
#include "Marle/Core/KeyCodes.h"
#include "Marle/Core/MouseCodes.h"
#include "Marle/Input.h"
#include "Marle/InputFrame.h"
#include "Marle/Core/KeyNames.h"

// Renderer
#include "Marle/Renderer/OrthographicCamera.h"
#include "Marle/Renderer/SubTexture2D.h"
#include "Marle/Renderer/BitmapFont.h"
#include "Marle/Renderer/Renderer2D.h"
#include "Marle/Platform/OpenGL/OpenGLTexture.h"
#include "Marle/Platform/OpenGL/OpenGLFramebuffer.h"

// Note: the entry point (main) is separate. Include <Marle/EntryPoint.h> in exactly one source file.
