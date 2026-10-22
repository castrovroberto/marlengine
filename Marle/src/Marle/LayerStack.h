#pragma once

#include "Core.h"
#include "Layer.h"
#include <memory>
#include <vector>

namespace Marle {

    // Ordered, owning list of layers. Layers sit at the bottom, overlays on top of all layers.
    // Iteration order is bottom-to-top. Do not push or pop while the stack is being iterated
    // (i.e. from inside a layer's OnUpdate/OnRender/OnEvent).
    class EXPORT LayerStack
    {
        public:
            LayerStack() = default;
            ~LayerStack();

            // Take ownership, call OnAttach, and return the raw pointer for later Pop().
            Layer* PushLayer(std::unique_ptr<Layer> layer);     // above existing layers, below overlays
            Layer* PushOverlay(std::unique_ptr<Layer> layer);   // above everything

            // Calls OnDetach and hands ownership back; nullptr if the layer isn't in the stack.
            std::unique_ptr<Layer> Pop(Layer* layer);

            // Detaches and destroys every layer, top first.
            void Clear();

            // Offer the event to layers top-to-bottom, stopping once it is handled.
            void DispatchEvent(Event& e);

            size_t Size() const { return m_Layers.size(); }
            std::vector<std::unique_ptr<Layer>>::iterator begin() { return m_Layers.begin(); }
            std::vector<std::unique_ptr<Layer>>::iterator end() { return m_Layers.end(); }
            std::vector<std::unique_ptr<Layer>>::const_iterator begin() const { return m_Layers.begin(); }
            std::vector<std::unique_ptr<Layer>>::const_iterator end() const { return m_Layers.end(); }

        private:
            std::vector<std::unique_ptr<Layer>> m_Layers;
            size_t m_LayerInsertIndex = 0;   // layers occupy [0, index), overlays [index, size)
    };

}
