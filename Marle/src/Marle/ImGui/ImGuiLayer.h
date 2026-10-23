#pragma once

#include "Marle/Core.h"
#include "Marle/Layer.h"
#include "Marle/LayerStack.h"

namespace Marle {

    // Dear ImGui debug overlay. Push it with Application::PushOverlay, passing the application's
    // layer stack. Each frame it begins an ImGui frame, calls OnImGuiRender() on every layer in the
    // stack (bottom to top), then draws the result over everything else.
    // Marle events and polled Input drive ImGui; mouse and keyboard events ImGui wants
    // (WantCaptureMouse/Keyboard) are marked handled so layers below don't also react to them.
    class EXPORT ImGuiLayer : public Layer
    {
        public:
            explicit ImGuiLayer(const LayerStack& layers);

            void OnAttach() override;
            void OnDetach() override;
            void OnRender(double interpolation_alpha) override;
            void OnEvent(Event& e) override;

        private:
            const LayerStack& m_Layers;
            double m_LastTime = 0.0;
    };

}
