#pragma once

#include "Core.h"
#include "Events/Event.h"
#include <string>

namespace Marle {

    // A unit of application behavior (game world, UI, debug overlay...) managed by a LayerStack.
    // Update and render run bottom-to-top; events are offered top-to-bottom until one is handled.
    class EXPORT Layer
    {
        public:
            Layer(const std::string& name = "Layer") : m_DebugName(name) {}
            virtual ~Layer() = default;

            virtual void OnAttach() {}                      // pushed onto a stack; GL context is current
            virtual void OnDetach() {}                      // popped or the stack is being destroyed
            virtual void OnUpdate(double fixed_dt) {}       // fixed timestep; read input through Application::GetInputFrame()
            virtual void OnRender(double interpolation_alpha) {}
            virtual void OnEvent(Event& e) {}               // call e.SetHandled() to stop propagation
            virtual void OnImGuiRender() {}                 // issue ImGui calls; only called while an ImGuiLayer is in the stack

            const std::string& GetName() const { return m_DebugName; }

        protected:
            std::string m_DebugName;
    };

}
