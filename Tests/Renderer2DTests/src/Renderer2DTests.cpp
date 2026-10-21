// Renderer2D pixel-readback tests. Needs a display and GL 3.3; run from the repo root.
// Exits non-zero if any check fails.
#include "Marle/Application.h"
#include "Marle/Renderer/Renderer2D.h"
#include <glad/gl.h>
#include <memory>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
using namespace Marle;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } else { printf("ok:   "); printf(__VA_ARGS__); printf("\n"); } } while (0)

struct Px { int r, g, b; };
static Px Read(int x, int y) { unsigned char p[4]; glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p); return {p[0], p[1], p[2]}; }
static bool Near(Px a, int r, int g, int b, int tol = 2) { return abs(a.r-r) <= tol && abs(a.g-g) <= tol && abs(a.b-b) <= tol; }


// Writes a 64x64 uncompressed 32-bit TGA made of four 32x32 cells and returns its path.
// As displayed: bottom-left red, bottom-right green, top-left blue, top-right yellow.
static std::string WriteSheet()
{
    std::string path = (std::filesystem::temp_directory_path() / "marle_sheet_test.tga").string();
    unsigned char header[18] = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 64, 0, 64, 0, 32, 0x08 }; // origin bottom-left
    std::ofstream f(path, std::ios::binary);
    f.write((const char*)header, sizeof(header));
    for (int y = 0; y < 64; y++) {          // rows are stored bottom-to-top
        for (int x = 0; x < 64; x++) {
            bool top = y >= 32, right = x >= 32;
            unsigned char r = 0, g = 0, b = 0;
            if (!top && !right) r = 255;
            else if (!top && right) g = 255;
            else if (top && !right) b = 255;
            else { r = 255; g = 255; }
            unsigned char bgra[4] = { b, g, r, 255 };
            f.write((const char*)bgra, 4);
        }
    }
    return path;
}

// Writes an 8x8 uncompressed 32-bit TGA of white pixels at the given alpha and returns its path.
static std::string WriteAlphaTexture(unsigned char alpha)
{
    std::string path = (std::filesystem::temp_directory_path() / "marle_alpha_test.tga").string();
    unsigned char header[18] = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 8, 0, 32, 0x08 };
    std::ofstream f(path, std::ios::binary);
    f.write((const char*)header, sizeof(header));
    for (int i = 0; i < 64; i++) { unsigned char bgra[4] = { 255, 255, 255, alpha }; f.write((const char*)bgra, 4); }
    return path;
}

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

        // 5. rotation: radians, counter-clockwise positive, about the quad center
        glClear(GL_COLOR_BUFFER_BIT);
        Renderer2D::BeginScene(W, H);
        Renderer2D::DrawRotatedQuad({200, 200}, {100, 20}, 1.5707963f, glm::vec4(1, 0, 0, 1));     // 90 degrees
        Renderer2D::DrawRotatedQuad({600, 200}, {100, 20}, 0.7853982f, glm::vec4(1, 0, 0, 1));     // 45 degrees
        Renderer2D::EndScene();
        CHECK(Near(Read(200, 240), 255, 0, 0), "90 deg: long axis now vertical (200,240 filled)");
        CHECK(Near(Read(240, 200), bg.r, bg.g, bg.b), "90 deg: old horizontal extent empty (240,200)");
        CHECK(Near(Read(625, 225), 255, 0, 0), "45 deg CCW: long axis points up-right");
        CHECK(Near(Read(625, 175), bg.r, bg.g, bg.b), "45 deg CCW: down-right is empty");

        // 6. camera
        {
            OrthographicCamera pan(0, W, 0, H);
            pan.SetPosition({100, 0});
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginScene(pan);
            Renderer2D::DrawQuad({300, 300}, {50, 50}, glm::vec4(0, 1, 0, 1));
            Renderer2D::EndScene();
            CHECK(Near(Read(200, 300), 0, 255, 0), "camera at x=100: world x=300 appears at screen x=200");
            CHECK(Near(Read(300, 300), bg.r, bg.g, bg.b), "camera pan: old screen position empty");

            OrthographicCamera zoom(0, W / 2, 0, H / 2);
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginScene(zoom);
            Renderer2D::DrawQuad({100, 100}, {50, 50}, glm::vec4(0, 0, 1, 1));
            Renderer2D::EndScene();
            CHECK(Near(Read(200, 200), 0, 0, 255), "2x zoom: quad center at (200,200)");
            CHECK(Near(Read(245, 200), 0, 0, 255), "2x zoom: quad is 100px wide");
            CHECK(Near(Read(260, 200), bg.r, bg.g, bg.b), "2x zoom: nothing past 100px");

            OrthographicCamera rot(-W / 2, W / 2, -H / 2, H / 2);
            rot.SetPosition({512, 384});
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginScene(rot);
            Renderer2D::DrawQuad({512, 384}, {20, 20}, glm::vec4(1, 1, 0, 1));
            Renderer2D::EndScene();
            CHECK(Near(Read(512, 384), 255, 255, 0), "centered camera: world point at camera sits at screen center");

            rot.SetRotation(1.5707963f); // camera turned 90 deg CCW: the world appears turned 90 deg CW
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginScene(rot);
            Renderer2D::DrawQuad({512, 484}, {20, 20}, glm::vec4(1, 1, 0, 1)); // 100 above the camera
            Renderer2D::EndScene();
            CHECK(Near(Read(612, 384), 255, 255, 0), "camera rotated 90 CCW: point above camera appears to the right");
            CHECK(Near(Read(512, 484), bg.r, bg.g, bg.b), "camera rotated: old screen position empty");
        }

        // 7. sprite sheet sub-textures
        {
            std::string sheetPath = WriteSheet();
            OpenGLTexture2D sheet(sheetPath);
            std::filesystem::remove(sheetPath); // already uploaded to the GPU
            const glm::vec2 cell(32, 32);
            auto red   = SubTexture2D::CreateFromCoords(&sheet, {0, 0}, cell);
            auto green = SubTexture2D::CreateFromCoords(&sheet, {1, 0}, cell);
            auto blue  = SubTexture2D::CreateFromCoords(&sheet, {0, 1}, cell);
            auto yellow = SubTexture2D::CreateFromCoords(&sheet, {1, 1}, cell);

            glClear(GL_COLOR_BUFFER_BIT); Renderer2D::ResetStats();
            Renderer2D::BeginScene(W, H);
            Renderer2D::DrawQuad({100, 100}, {40, 40}, red);
            Renderer2D::DrawQuad({200, 100}, {40, 40}, green);
            Renderer2D::DrawQuad({300, 100}, {40, 40}, blue);
            Renderer2D::DrawQuad({400, 100}, {40, 40}, yellow);
            Renderer2D::EndScene();
            CHECK(Near(Read(100, 100), 255, 0, 0), "cell (0,0) is the bottom-left (red)");
            CHECK(Near(Read(200, 100), 0, 255, 0), "cell (1,0) is bottom-right (green)");
            CHECK(Near(Read(300, 100), 0, 0, 255), "cell (0,1) is top-left (blue)");
            CHECK(Near(Read(400, 100), 255, 255, 0), "cell (1,1) is top-right (yellow)");
            CHECK(Renderer2D::GetStats().DrawCalls == 1, "4 sprites from one sheet -> 1 draw call (got %u)", Renderer2D::GetStats().DrawCalls);

            // multi-cell sprite: bottom row = red | green
            auto bottomRow = SubTexture2D::CreateFromCoords(&sheet, {0, 0}, cell, {2, 1});
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginScene(W, H);
            Renderer2D::DrawQuad({600, 100}, {80, 40}, bottomRow);
            Renderer2D::EndScene();
            CHECK(Near(Read(580, 100), 255, 0, 0) && Near(Read(620, 100), 0, 255, 0), "2x1 sprite spans red | green");

            // tint multiplies the sprite color
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginScene(W, H);
            Renderer2D::DrawQuad({100, 300}, {40, 40}, yellow, glm::vec4(1, 0.5f, 1, 1));
            Renderer2D::EndScene();
            CHECK(Near(Read(100, 300), 255, 128, 0), "tinted sprite = yellow * (1,.5,1)");

            // UVs rotate with the quad: 90 deg CCW sends the left (red) half down, right (green) half up
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginScene(W, H);
            Renderer2D::DrawRotatedQuad({300, 300}, {80, 40}, 1.5707963f, bottomRow);
            Renderer2D::EndScene();
            CHECK(Near(Read(300, 280), 255, 0, 0) && Near(Read(300, 320), 0, 255, 0), "rotated sprite keeps its orientation (red below, green above)");
        }

        // 8. alpha blending (background is (26,26,38))
        {
            // 50% red over the background: 0.5*255 + 0.5*26 = 140, 13, 19
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginScene(W, H);
            Renderer2D::DrawQuad({100, 500}, {50, 50}, glm::vec4(1, 0, 0, 0.5f));
            Renderer2D::EndScene();
            { Px q = Read(100, 500); CHECK(Near(q, 140, 13, 19, 3), "50%% red blends with the background (%d,%d,%d)", q.r, q.g, q.b); }

            // zero alpha leaves the background alone
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginScene(W, H);
            Renderer2D::DrawQuad({100, 500}, {50, 50}, glm::vec4(1, 0, 0, 0.0f));
            Renderer2D::EndScene();
            CHECK(Near(Read(100, 500), bg.r, bg.g, bg.b), "zero-alpha quad is invisible");

            // order matters: translucent red over opaque blue = (127,0,127); the reverse is opaque blue
            glClear(GL_COLOR_BUFFER_BIT); Renderer2D::ResetStats();
            Renderer2D::BeginScene(W, H);
            Renderer2D::DrawQuad({200, 500}, {50, 50}, glm::vec4(0, 0, 1, 1));
            Renderer2D::DrawQuad({200, 500}, {50, 50}, glm::vec4(1, 0, 0, 0.5f));
            Renderer2D::DrawQuad({300, 500}, {50, 50}, glm::vec4(1, 0, 0, 0.5f));
            Renderer2D::DrawQuad({300, 500}, {50, 50}, glm::vec4(0, 0, 1, 1));
            Renderer2D::EndScene();
            { Px q = Read(200, 500); CHECK(Near(q, 128, 0, 128, 3), "red over blue blends (%d,%d,%d)", q.r, q.g, q.b); }
            CHECK(Near(Read(300, 500), 0, 0, 255), "opaque blue drawn last covers red");
            CHECK(Renderer2D::GetStats().DrawCalls == 1, "blending does not break batching");

            // texture alpha (0.5) times tint alpha: white*green tint at 0.5 -> (13,140,19); at 0.5*0.5 -> (19.5,83,28.5)
            std::string path = WriteAlphaTexture(128);
            OpenGLTexture2D half(path);
            std::filesystem::remove(path);
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginScene(W, H);
            Renderer2D::DrawQuad({400, 500}, {50, 50}, &half, glm::vec4(0, 1, 0, 1));
            Renderer2D::DrawQuad({500, 500}, {50, 50}, &half, glm::vec4(0, 1, 0, 0.5f));
            Renderer2D::EndScene();
            { Px q = Read(400, 500); CHECK(Near(q, 13, 140, 19, 4), "texture alpha blends (%d,%d,%d)", q.r, q.g, q.b); }
            { Px q = Read(500, 500); CHECK(Near(q, 19, 83, 28, 4), "texture alpha multiplies tint alpha (%d,%d,%d)", q.r, q.g, q.b); }
        }

        // 9. GL errors
        GLenum err = glGetError();
        CHECK(err == GL_NO_ERROR, "no GL error (0x%x)", err);

        printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
        exit(fails ? 1 : 0);
    }
};

int main() { RendererTestApp app; app.Run(); }
