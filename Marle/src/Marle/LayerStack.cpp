#include "mrlpch.h"
#include "LayerStack.h"

namespace Marle {

    LayerStack::~LayerStack()
    {
        Clear();
    }

    Layer* LayerStack::PushLayer(std::unique_ptr<Layer> layer)
    {
        Layer* raw = layer.get();
        if (!raw) return nullptr;
        m_Layers.insert(m_Layers.begin() + m_LayerInsertIndex, std::move(layer));
        m_LayerInsertIndex++;
        raw->OnAttach();
        return raw;
    }

    Layer* LayerStack::PushOverlay(std::unique_ptr<Layer> layer)
    {
        Layer* raw = layer.get();
        if (!raw) return nullptr;
        m_Layers.push_back(std::move(layer));
        raw->OnAttach();
        return raw;
    }

    std::unique_ptr<Layer> LayerStack::Pop(Layer* layer)
    {
        for (size_t i = 0; i < m_Layers.size(); i++) {
            if (m_Layers[i].get() != layer) continue;

            std::unique_ptr<Layer> owned = std::move(m_Layers[i]);
            m_Layers.erase(m_Layers.begin() + i);
            if (i < m_LayerInsertIndex) m_LayerInsertIndex--;
            owned->OnDetach();
            return owned;
        }
        return nullptr;
    }

    void LayerStack::Clear()
    {
        while (!m_Layers.empty()) {
            std::unique_ptr<Layer> top = std::move(m_Layers.back());
            m_Layers.pop_back();
            if (m_Layers.size() < m_LayerInsertIndex) m_LayerInsertIndex = m_Layers.size();
            top->OnDetach();
        }
        m_LayerInsertIndex = 0;
    }

    void LayerStack::DispatchEvent(Event& e)
    {
        for (size_t i = m_Layers.size(); i-- > 0 && !e.IsHandled(); )
            m_Layers[i]->OnEvent(e);
    }

}
