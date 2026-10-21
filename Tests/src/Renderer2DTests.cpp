// Renderer2D pixel-readback tests. Needs a display and GL 3.3; run from the repo root.
// Exits non-zero if any check fails.
#include "Marle/Application.h"
#include "Marle/Renderer/Renderer2D.h"
#include <glad/gl.h>
#include <memory>
#include <vector>
#include <cstdio>
#include <cstdlib>
using namespace Marle;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } else { printf("ok:   "); printf(__VA_ARGS__); printf("\n"); } } while (0)

struct Px { int r, g, b; };
static Px Read(int x, int y) { unsigned char p[4]; glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p); return {p[0], p[1], p[2]}; }
static bool Near(Px a, int r, int g, int b) { return abs(a.r-r) <= 2 && abs(a.g-g) <= 2 && abs(a.b-b) <= 2; }

class RendererTestApp : public Application {
    std::vector<std::unique_ptr<OpenGLTexture2D>> tex;
    int frame = 0;
public:
    RendererTestApp() : Application({"rtest", 1024, 768}) { for (int i = 0; i < 20; i++) tex.emplace_back(new OpenGLTexture2D("Assets/Textures/test_sprite.tga")); }
protected:
    void OnRender(double) override {
        if (++frame < 3) return;
        const float W = 1024, H = 768;
        Px bg = {26, 26, 38};

        // 1. flat colour quad, center-anchored
        glClear(GL_COLOR_BUFFER_BIT); Renderer2D::ResetStats();
        Renderer2D::BeginScene(W, H);
        Renderer2D::DrawQuad({100, 100}, {50, 50}, glm::vec4(1, 0, 0, 1));
        Renderer2D::EndScene();
        CHECK(Near(Read(100, 100), 255, 0, 0), "flat quad center is red");
        CHECK(Near(Read(78, 100), 255, 0, 0), "flat quad inside left edge (x=78)");
        CHECK(Near(Read(70, 100), bg.r, bg.g, bg.b), "outside quad is clear color");
        CHECK(Near(Read(130, 100), bg.r, bg.g, bg.b), "outside right of quad is clear color");
        CHECK(Renderer2D::GetStats().DrawCalls == 1 && Renderer2D::GetStats().QuadCount == 1, "1 quad = 1 draw call");

        // 2. textured quad (legacy API) differs from background; tint changes colour
        glClear(GL_COLOR_BUFFER_BIT);
        Renderer2D::BeginScene(W, H);
        Renderer2D::DrawQuad({300, 300}, {128, 128}, tex[0].get());
        Renderer2D::DrawQuad({600, 300}, {128, 128}, tex[0].get(), glm::vec4(0, 1, 0, 1));
        Renderer2D::EndScene();
        Px a = Read(300, 300), b = Read(600, 300);
        printf("      textured px=(%d,%d,%d) tinted px=(%d,%d,%d)\n", a.r, a.g, a.b, b.r, b.g, b.b);
        CHECK(!Near(a, bg.r, bg.g, bg.b), "textured quad drew something");
        CHECK(b.r <= 2 && b.b <= 2, "green tint zeroes red/blue");

        // 3. batching across MaxQuads
        glClear(GL_COLOR_BUFFER_BIT); Renderer2D::ResetStats();
        Renderer2D::BeginScene(W, H);
        for (int i = 0; i < 25000; i++) Renderer2D::DrawQuad({(float)(i % 1000), (float)(i / 1000) + 400}, {1, 1}, glm::vec4(0, 0, 1, 1));
        Renderer2D::EndScene();
        auto s = Renderer2D::GetStats();
        CHECK(s.QuadCount == 25000 && s.DrawCalls == 3, "25000 quads -> 3 draw calls (got %u quads, %u calls)", s.QuadCount, s.DrawCalls);
        CHECK(Near(Read(500, 410), 0, 0, 255), "quad from final batch rendered");
        CHECK(Near(Read(500, 405), 0, 0, 255), "quad from first batch rendered");

        // 4. many distinct textures: 15 fit in one call, 20 need two
        glClear(GL_COLOR_BUFFER_BIT); Renderer2D::ResetStats();
        Renderer2D::BeginScene(W, H);
        for (int i = 0; i < 15; i++) Renderer2D::DrawQuad({(float)(40 + i * 60), 600}, {32, 32}, tex[i].get());
        Renderer2D::EndScene();
        CHECK(Renderer2D::GetStats().DrawCalls == 1, "15 textures -> 1 draw call (got %u)", Renderer2D::GetStats().DrawCalls);
        Renderer2D::ResetStats();
        Renderer2D::BeginScene(W, H);
        for (int i = 0; i < 20; i++) Renderer2D::DrawQuad({(float)(40 + i * 45), 600}, {32, 32}, tex[i].get());
        Renderer2D::EndScene();
        CHECK(Renderer2D::GetStats().DrawCalls == 2, "20 textures -> 2 draw calls (got %u)", Renderer2D::GetStats().DrawCalls);
        Px lo = Read(40, 600), hi = Read(40 + 19 * 45, 600);
        printf("      first-slot px=(%d,%d,%d) last-batch px=(%d,%d,%d)\n", lo.r, lo.g, lo.b, hi.r, hi.g, hi.b);
        CHECK(!Near(lo, bg.r, bg.g, bg.b) && !Near(hi, bg.r, bg.g, bg.b), "textures in both batches rendered");

        // 5. GL errors
        GLenum err = glGetError();
        CHECK(err == GL_NO_ERROR, "no GL error (0x%x)", err);

        printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
        exit(fails ? 1 : 0);
    }
};

int main() { RendererTestApp app; app.Run(); }
