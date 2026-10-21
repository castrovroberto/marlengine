// LayerStack ordering, lifecycle and event propagation. Headless.
// Exits non-zero if any check fails.
#include "Marle/LayerStack.h"
#include "Marle/Events/KeyEvent.h"
#include <cstdio>
#include <string>

using namespace Marle;

static int fails = 0;
#define CHECK(c, name) do { if (!(c)) { fails++; printf("FAIL: %s\n", name); } else { printf("ok:   %s\n", name); } } while (0)

static std::string s_Log;   // "+A" attach, "-A" detach, "eA" event seen, "xA" destroyed

struct TestLayer : Layer {
    bool Handles;
    TestLayer(const std::string& name, bool handles = false) : Layer(name), Handles(handles) {}
    ~TestLayer() override { s_Log += "x" + m_DebugName + " "; }
    void OnAttach() override { s_Log += "+" + m_DebugName + " "; }
    void OnDetach() override { s_Log += "-" + m_DebugName + " "; }
    void OnEvent(Event& e) override { s_Log += "e" + m_DebugName + " "; if (Handles) e.SetHandled(); }
};

static std::string Order(LayerStack& stack)
{
    std::string out;
    for (auto& l : stack) out += l->GetName();
    return out;
}

static std::unique_ptr<Layer> Make(const char* name, bool handles = false) { return std::make_unique<TestLayer>(name, handles); }

int main()
{
    {
        LayerStack stack;
        stack.PushLayer(Make("A"));
        stack.PushLayer(Make("B"));
        stack.PushOverlay(Make("X"));
        stack.PushLayer(Make("C"));       // layers go below overlays
        stack.PushOverlay(Make("Y"));
        CHECK(Order(stack) == "ABCXY", "layers sit below overlays, in push order");
        CHECK(s_Log == "+A +B +X +C +Y ", "OnAttach runs on push");
        CHECK(stack.PushLayer(nullptr) == nullptr && stack.Size() == 5, "pushing null is ignored");

        s_Log.clear();
        KeyPressedEvent e(1, 0);
        stack.DispatchEvent(e);
        CHECK(s_Log == "eY eX eC eB eA ", "events go top-to-bottom");

        // a handled event stops propagation
        LayerStack stack2;
        stack2.PushLayer(Make("A"));
        stack2.PushLayer(Make("B", true));
        stack2.PushLayer(Make("C"));
        s_Log.clear();
        KeyPressedEvent e2(1, 0);
        stack2.DispatchEvent(e2);
        CHECK(s_Log == "eC eB " && e2.IsHandled(), "handled event is not offered to layers below");

        // an event handled before dispatch reaches nobody
        s_Log.clear();
        KeyPressedEvent e3(1, 0);
        e3.SetHandled();
        stack.DispatchEvent(e3);
        CHECK(s_Log.empty(), "already-handled event reaches no layer");
        s_Log.clear();

        Layer* b = nullptr;
        for (auto& l : stack) if (l->GetName() == "B") b = l.get();
        auto popped = stack.Pop(b);
        CHECK(popped && popped->GetName() == "B" && Order(stack) == "ACXY", "Pop removes the layer and returns ownership");
        CHECK(s_Log == "-B ", "OnDetach runs on pop, without destroying");
        stack.PushLayer(Make("D"));
        CHECK(Order(stack) == "ACDXY", "layer insert point shrinks after popping a layer");

        Layer* x = nullptr;
        for (auto& l : stack) if (l->GetName() == "X") x = l.get();
        stack.Pop(x).reset();
        stack.PushLayer(Make("E"));
        CHECK(Order(stack) == "ACDEY", "popping an overlay leaves the insert point alone");

        CHECK(stack.Pop(b) == nullptr, "popping a layer that is not in the stack returns null");

        // re-push of a popped layer
        stack.PushOverlay(std::move(popped));
        CHECK(Order(stack) == "ACDEYB", "a popped layer can be pushed again");

        s_Log.clear();
    }
    // stack2 destroyed first (C B A), then stack (B Y E D C A) - top first, detached before destroyed
    CHECK(s_Log == "-C xC -B xB -A xA -B xB -Y xY -E xE -D xD -C xC -A xA ", "destroying a stack detaches then destroys layers, top first");

    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
