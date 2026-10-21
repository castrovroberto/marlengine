#pragma once

#include "Core.h"
#include "Events/Event.h"

#include <functional>
#include <memory>

namespace Marle {
    
    struct WindowProps {
        const char* Title;
        unsigned int Width;
        unsigned int Height;

        WindowProps(const char* title = "Marle Engine",
                    unsigned int width = 1280,
                    unsigned int height = 720)
            : Title(title), Width(width), Height(height) {}
    };

    // Platform-independent window + OpenGL context.
    // Creating a window leaves its GL context current on the calling thread
    // with the GL function pointers loaded.
    // Mouse positions are reported in window coordinates, origin at the bottom-left.
    class EXPORT Window
    {
        public:
            using EventCallbackFn = std::function<void(Event&)>;

            virtual ~Window() = default;

            // Pump platform events (dispatching them to the callback), then swap buffers.
            virtual void OnUpdate() = 0;

            virtual unsigned int GetWidth() const = 0;
            virtual unsigned int GetHeight() const = 0;

            virtual void SetEventCallback(const EventCallbackFn& callback) = 0;

            // Implemented by each platform backend.
            static std::unique_ptr<Window> Create(const WindowProps& props = WindowProps());
    };

}
