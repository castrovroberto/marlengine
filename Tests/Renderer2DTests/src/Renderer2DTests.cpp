// Renderer2D pixel-readback tests. Needs a display and GL 3.3; run from the repo root.
// Exits non-zero if any check fails.
#include "Marle/Application.h"
#include "Marle/Renderer/Renderer2D.h"
#include "Marle/Platform/OpenGL/OpenGLFramebuffer.h"
#include "Marle/Scene/SceneLoader.h"
#include "Marle/Scene/SceneRenderer.h"
#include "Marle/Scene/Components.h"
#include "Marle/Scene/Simulation.h"
#include <glad/gl.h>
#include <memory>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <type_traits>
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

struct Slide { glm::vec2 Velocity = { 0.0f, 0.0f }; };   // a behavior for the interpolation test

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
        Renderer2D::BeginBatch(W, H);
        Renderer2D::DrawQuad({100, 100}, {50, 50}, glm::vec4(1, 0, 0, 1));
        Renderer2D::EndBatch();
        CHECK(Near(Read(100, 100), 255, 0, 0), "flat quad center is red");
        CHECK(Near(Read(78, 100), 255, 0, 0), "flat quad inside left edge (x=78)");
        CHECK(Near(Read(70, 100), bg.r, bg.g, bg.b), "outside quad is clear color");
        CHECK(Near(Read(130, 100), bg.r, bg.g, bg.b), "outside right of quad is clear color");
        CHECK(Renderer2D::GetStats().DrawCalls == 1 && Renderer2D::GetStats().QuadCount == 1, "1 quad = 1 draw call");

        // 2. textured quad (legacy API) differs from background; tint changes colour
        glClear(GL_COLOR_BUFFER_BIT);
        Renderer2D::BeginBatch(W, H);
        Renderer2D::DrawQuad({300, 300}, {128, 128}, tex[0].get());
        Renderer2D::DrawQuad({600, 300}, {128, 128}, tex[0].get(), glm::vec4(0, 1, 0, 1));
        Renderer2D::EndBatch();
        Px a = Read(300, 300), b = Read(600, 300);
        printf("      textured px=(%d,%d,%d) tinted px=(%d,%d,%d)\n", a.r, a.g, a.b, b.r, b.g, b.b);
        CHECK(!Near(a, bg.r, bg.g, bg.b), "textured quad drew something");
        CHECK(b.r <= 2 && b.b <= 2, "green tint zeroes red/blue");

        // 3. batching across MaxQuads
        glClear(GL_COLOR_BUFFER_BIT); Renderer2D::ResetStats();
        Renderer2D::BeginBatch(W, H);
        for (int i = 0; i < 25000; i++) Renderer2D::DrawQuad({(float)(i % 1000), (float)(i / 1000) + 400}, {1, 1}, glm::vec4(0, 0, 1, 1));
        Renderer2D::EndBatch();
        auto s = Renderer2D::GetStats();
        CHECK(s.QuadCount == 25000 && s.DrawCalls == 3, "25000 quads -> 3 draw calls (got %u quads, %u calls)", s.QuadCount, s.DrawCalls);
        CHECK(Near(Read(500, 410), 0, 0, 255), "quad from final batch rendered");
        CHECK(Near(Read(500, 405), 0, 0, 255), "quad from first batch rendered");

        // 4. many distinct textures: 15 fit in one call, 20 need two
        glClear(GL_COLOR_BUFFER_BIT); Renderer2D::ResetStats();
        Renderer2D::BeginBatch(W, H);
        for (int i = 0; i < 15; i++) Renderer2D::DrawQuad({(float)(40 + i * 60), 600}, {32, 32}, tex[i].get());
        Renderer2D::EndBatch();
        CHECK(Renderer2D::GetStats().DrawCalls == 1, "15 textures -> 1 draw call (got %u)", Renderer2D::GetStats().DrawCalls);
        Renderer2D::ResetStats();
        Renderer2D::BeginBatch(W, H);
        for (int i = 0; i < 20; i++) Renderer2D::DrawQuad({(float)(40 + i * 45), 600}, {32, 32}, tex[i].get());
        Renderer2D::EndBatch();
        CHECK(Renderer2D::GetStats().DrawCalls == 2, "20 textures -> 2 draw calls (got %u)", Renderer2D::GetStats().DrawCalls);
        Px lo = Read(40, 600), hi = Read(40 + 19 * 45, 600);
        printf("      first-slot px=(%d,%d,%d) last-batch px=(%d,%d,%d)\n", lo.r, lo.g, lo.b, hi.r, hi.g, hi.b);
        CHECK(!Near(lo, bg.r, bg.g, bg.b) && !Near(hi, bg.r, bg.g, bg.b), "textures in both batches rendered");

        // 5. rotation: radians, counter-clockwise positive, about the quad center
        glClear(GL_COLOR_BUFFER_BIT);
        Renderer2D::BeginBatch(W, H);
        Renderer2D::DrawRotatedQuad({200, 200}, {100, 20}, 1.5707963f, glm::vec4(1, 0, 0, 1));     // 90 degrees
        Renderer2D::DrawRotatedQuad({600, 200}, {100, 20}, 0.7853982f, glm::vec4(1, 0, 0, 1));     // 45 degrees
        Renderer2D::EndBatch();
        CHECK(Near(Read(200, 240), 255, 0, 0), "90 deg: long axis now vertical (200,240 filled)");
        CHECK(Near(Read(240, 200), bg.r, bg.g, bg.b), "90 deg: old horizontal extent empty (240,200)");
        CHECK(Near(Read(625, 225), 255, 0, 0), "45 deg CCW: long axis points up-right");
        CHECK(Near(Read(625, 175), bg.r, bg.g, bg.b), "45 deg CCW: down-right is empty");

        // 6. camera
        {
            OrthographicCamera pan(0, W, 0, H);
            pan.SetPosition({100, 0});
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginBatch(pan);
            Renderer2D::DrawQuad({300, 300}, {50, 50}, glm::vec4(0, 1, 0, 1));
            Renderer2D::EndBatch();
            CHECK(Near(Read(200, 300), 0, 255, 0), "camera at x=100: world x=300 appears at screen x=200");
            CHECK(Near(Read(300, 300), bg.r, bg.g, bg.b), "camera pan: old screen position empty");

            OrthographicCamera zoom(0, W / 2, 0, H / 2);
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginBatch(zoom);
            Renderer2D::DrawQuad({100, 100}, {50, 50}, glm::vec4(0, 0, 1, 1));
            Renderer2D::EndBatch();
            CHECK(Near(Read(200, 200), 0, 0, 255), "2x zoom: quad center at (200,200)");
            CHECK(Near(Read(245, 200), 0, 0, 255), "2x zoom: quad is 100px wide");
            CHECK(Near(Read(260, 200), bg.r, bg.g, bg.b), "2x zoom: nothing past 100px");

            OrthographicCamera rot(-W / 2, W / 2, -H / 2, H / 2);
            rot.SetPosition({512, 384});
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginBatch(rot);
            Renderer2D::DrawQuad({512, 384}, {20, 20}, glm::vec4(1, 1, 0, 1));
            Renderer2D::EndBatch();
            CHECK(Near(Read(512, 384), 255, 255, 0), "centered camera: world point at camera sits at screen center");

            rot.SetRotation(1.5707963f); // camera turned 90 deg CCW: the world appears turned 90 deg CW
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginBatch(rot);
            Renderer2D::DrawQuad({512, 484}, {20, 20}, glm::vec4(1, 1, 0, 1)); // 100 above the camera
            Renderer2D::EndBatch();
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
            Renderer2D::BeginBatch(W, H);
            Renderer2D::DrawQuad({100, 100}, {40, 40}, red);
            Renderer2D::DrawQuad({200, 100}, {40, 40}, green);
            Renderer2D::DrawQuad({300, 100}, {40, 40}, blue);
            Renderer2D::DrawQuad({400, 100}, {40, 40}, yellow);
            Renderer2D::EndBatch();
            CHECK(Near(Read(100, 100), 255, 0, 0), "cell (0,0) is the bottom-left (red)");
            CHECK(Near(Read(200, 100), 0, 255, 0), "cell (1,0) is bottom-right (green)");
            CHECK(Near(Read(300, 100), 0, 0, 255), "cell (0,1) is top-left (blue)");
            CHECK(Near(Read(400, 100), 255, 255, 0), "cell (1,1) is top-right (yellow)");
            CHECK(Renderer2D::GetStats().DrawCalls == 1, "4 sprites from one sheet -> 1 draw call (got %u)", Renderer2D::GetStats().DrawCalls);

            // multi-cell sprite: bottom row = red | green
            auto bottomRow = SubTexture2D::CreateFromCoords(&sheet, {0, 0}, cell, {2, 1});
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginBatch(W, H);
            Renderer2D::DrawQuad({600, 100}, {80, 40}, bottomRow);
            Renderer2D::EndBatch();
            CHECK(Near(Read(580, 100), 255, 0, 0) && Near(Read(620, 100), 0, 255, 0), "2x1 sprite spans red | green");

            // tint multiplies the sprite color
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginBatch(W, H);
            Renderer2D::DrawQuad({100, 300}, {40, 40}, yellow, glm::vec4(1, 0.5f, 1, 1));
            Renderer2D::EndBatch();
            CHECK(Near(Read(100, 300), 255, 128, 0), "tinted sprite = yellow * (1,.5,1)");

            // UVs rotate with the quad: 90 deg CCW sends the left (red) half down, right (green) half up
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginBatch(W, H);
            Renderer2D::DrawRotatedQuad({300, 300}, {80, 40}, 1.5707963f, bottomRow);
            Renderer2D::EndBatch();
            CHECK(Near(Read(300, 280), 255, 0, 0) && Near(Read(300, 320), 0, 255, 0), "rotated sprite keeps its orientation (red below, green above)");
        }

        // 8. alpha blending (background is (26,26,38))
        {
            // 50% red over the background: 0.5*255 + 0.5*26 = 140, 13, 19
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginBatch(W, H);
            Renderer2D::DrawQuad({100, 500}, {50, 50}, glm::vec4(1, 0, 0, 0.5f));
            Renderer2D::EndBatch();
            { Px q = Read(100, 500); CHECK(Near(q, 140, 13, 19, 3), "50%% red blends with the background (%d,%d,%d)", q.r, q.g, q.b); }

            // zero alpha leaves the background alone
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginBatch(W, H);
            Renderer2D::DrawQuad({100, 500}, {50, 50}, glm::vec4(1, 0, 0, 0.0f));
            Renderer2D::EndBatch();
            CHECK(Near(Read(100, 500), bg.r, bg.g, bg.b), "zero-alpha quad is invisible");

            // order matters: translucent red over opaque blue = (127,0,127); the reverse is opaque blue
            glClear(GL_COLOR_BUFFER_BIT); Renderer2D::ResetStats();
            Renderer2D::BeginBatch(W, H);
            Renderer2D::DrawQuad({200, 500}, {50, 50}, glm::vec4(0, 0, 1, 1));
            Renderer2D::DrawQuad({200, 500}, {50, 50}, glm::vec4(1, 0, 0, 0.5f));
            Renderer2D::DrawQuad({300, 500}, {50, 50}, glm::vec4(1, 0, 0, 0.5f));
            Renderer2D::DrawQuad({300, 500}, {50, 50}, glm::vec4(0, 0, 1, 1));
            Renderer2D::EndBatch();
            { Px q = Read(200, 500); CHECK(Near(q, 128, 0, 128, 3), "red over blue blends (%d,%d,%d)", q.r, q.g, q.b); }
            CHECK(Near(Read(300, 500), 0, 0, 255), "opaque blue drawn last covers red");
            CHECK(Renderer2D::GetStats().DrawCalls == 1, "blending does not break batching");

            // texture alpha (0.5) times tint alpha: white*green tint at 0.5 -> (13,140,19); at 0.5*0.5 -> (19.5,83,28.5)
            std::string path = WriteAlphaTexture(128);
            OpenGLTexture2D half(path);
            std::filesystem::remove(path);
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginBatch(W, H);
            Renderer2D::DrawQuad({400, 500}, {50, 50}, &half, glm::vec4(0, 1, 0, 1));
            Renderer2D::DrawQuad({500, 500}, {50, 50}, &half, glm::vec4(0, 1, 0, 0.5f));
            Renderer2D::EndBatch();
            { Px q = Read(400, 500); CHECK(Near(q, 13, 140, 19, 4), "texture alpha blends (%d,%d,%d)", q.r, q.g, q.b); }
            { Px q = Read(500, 500); CHECK(Near(q, 19, 83, 28, 4), "texture alpha multiplies tint alpha (%d,%d,%d)", q.r, q.g, q.b); }
        }

        // 9. texture failure handling: missing/corrupt/null textures draw as a magenta/black checker
        {
            static_assert(!std::is_copy_constructible<OpenGLTexture2D>::value && !std::is_copy_assignable<OpenGLTexture2D>::value,
                          "OpenGLTexture2D owns a GL object and must not be copyable");

            OpenGLTexture2D missing("Assets/Textures/does_not_exist.tga");
            CHECK(!missing.IsLoaded() && missing.GetWidth() == 0 && missing.GetHeight() == 0, "missing file: texture is not loaded, size 0");

            std::string junkPath = (std::filesystem::temp_directory_path() / "marle_junk_test.tga").string();
            { std::ofstream junk(junkPath, std::ios::binary); for (int i = 0; i < 200; i++) junk.put((char)(i * 37)); }
            OpenGLTexture2D junk(junkPath);
            std::filesystem::remove(junkPath);
            CHECK(!junk.IsLoaded(), "corrupt file: texture is not loaded");

            OpenGLTexture2D ok("Assets/Textures/test_sprite.tga");
            CHECK(ok.IsLoaded() && ok.GetWidth() == 256, "a valid texture reports loaded");

            const Px magenta = {255, 0, 255}, black = {0, 0, 0};
            auto IsChecker = [&](float cx, float cy, const char* what) {
                // quad of 40x40 centered on (cx,cy): quadrants (BL, BR, TR, TL) = magenta, black, magenta, black
                Px bl = Read((int)cx - 10, (int)cy - 10), br = Read((int)cx + 10, (int)cy - 10);
                Px tr = Read((int)cx + 10, (int)cy + 10), tl = Read((int)cx - 10, (int)cy + 10);
                CHECK(Near(bl, magenta.r, magenta.g, magenta.b) && Near(br, black.r, black.g, black.b) &&
                      Near(tr, magenta.r, magenta.g, magenta.b) && Near(tl, black.r, black.g, black.b), "%s draws the error checker", what);
            };

            glClear(GL_COLOR_BUFFER_BIT); Renderer2D::ResetStats();
            Renderer2D::BeginBatch(W, H);
            Renderer2D::DrawQuad({100, 100}, {40, 40}, &missing);
            Renderer2D::DrawQuad({200, 100}, {40, 40}, (OpenGLTexture2D*)nullptr);
            Renderer2D::DrawQuad({300, 100}, {40, 40}, SubTexture2D::CreateFromCoords(&missing, {0, 0}, {32, 32}));
            Renderer2D::DrawQuad({400, 100}, {40, 40}, &junk);
            Renderer2D::DrawQuad({500, 100}, {40, 40}, &ok);
            Renderer2D::EndBatch();
            IsChecker(100, 100, "missing texture");
            IsChecker(200, 100, "null texture");
            IsChecker(300, 100, "sub-texture of a missing texture");
            IsChecker(400, 100, "corrupt texture");
            Px good = Read(500, 100);
            CHECK(!Near(good, magenta.r, magenta.g, magenta.b) && !Near(good, bg.r, bg.g, bg.b), "a valid texture in the same batch is unaffected (%d,%d,%d)", good.r, good.g, good.b);
            CHECK(Renderer2D::GetStats().DrawCalls == 1, "failed textures share one slot and one draw call (got %u)", Renderer2D::GetStats().DrawCalls);

            // a failed texture must not stop later quads from drawing
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginBatch(W, H);
            Renderer2D::DrawQuad({100, 100}, {40, 40}, &missing);
            Renderer2D::DrawQuad({200, 100}, {40, 40}, glm::vec4(0, 1, 0, 1));
            Renderer2D::EndBatch();
            CHECK(Near(Read(200, 100), 0, 255, 0), "flat quads after a failed texture still draw");
        }

        // 9b. image formats: PNG, JPG and grayscale+alpha decode through stb_image and display upright
        {
            const std::string dir = "Tests/Renderer2DTests/fixtures/";
            OpenGLTexture2D png(dir + "quadrants.png"), jpg(dir + "quadrants.jpg");
            OpenGLTexture2D gray(dir + "gray_alpha.png"), truncated(dir + "truncated.png");
            CHECK(png.IsLoaded() && png.GetWidth() == 64 && png.GetHeight() == 64, "PNG loads with its size");
            CHECK(jpg.IsLoaded() && jpg.GetWidth() == 64 && jpg.GetHeight() == 64, "JPG loads with its size");
            CHECK(gray.IsLoaded() && gray.GetWidth() == 8 && gray.GetHeight() == 8, "grayscale+alpha PNG loads with its size");
            CHECK(!truncated.IsLoaded() && truncated.GetWidth() == 0, "truncated PNG: texture is not loaded, size 0");

            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginBatch(W, H);
            Renderer2D::DrawQuad({100, 300}, {64, 64}, &png);
            Renderer2D::DrawQuad({200, 300}, {64, 64}, &jpg);
            Renderer2D::DrawQuad({300, 300}, {16, 16}, &gray);
            Renderer2D::DrawQuad({400, 300}, {40, 40}, &truncated);
            Renderer2D::EndBatch();

            // The images are 1:1 on screen; cell centers are 16 px from the quad center.
            // As displayed: top-left blue, top-right yellow, bottom-left red, bottom-right green.
            auto Quadrants = [&](int cx, int cy, int tol, const char* what) {
                CHECK(Near(Read(cx - 16, cy - 16), 255, 0, 0, tol) && Near(Read(cx + 16, cy - 16), 0, 255, 0, tol) &&
                      Near(Read(cx - 16, cy + 16), 0, 0, 255, tol) && Near(Read(cx + 16, cy + 16), 255, 255, 0, tol),
                      "%s draws upright with the right colors", what);
            };
            Quadrants(100, 300, 2, "PNG");
            Quadrants(200, 300, 16, "JPG");   // lossy: allow a little drift
            CHECK(Near(Read(300, 300), 200, 200, 200), "grayscale+alpha PNG expands to RGB gray (got %d,%d,%d)", Read(300, 300).r, Read(300, 300).g, Read(300, 300).b);
            CHECK(Near(Read(390, 290), 255, 0, 255) && Near(Read(410, 310), 255, 0, 255), "truncated PNG draws the error checker");
        }

        // 9c. framebuffers: render into a texture, then back to the window
        {
            auto Viewport = [](GLint v[4]) { glGetIntegerv(GL_VIEWPORT, v); };
            auto Binding = []() { GLint b = -1; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &b); return b; };
            GLint vp[4];

            OpenGLFramebuffer fb(256, 128);
            CHECK(fb.IsComplete() && fb.GetWidth() == 256 && fb.GetHeight() == 128 && fb.GetColorAttachment() != 0, "framebuffer is complete and reports its size");

            glClear(GL_COLOR_BUFFER_BIT);
            fb.Bind();
            Viewport(vp);
            CHECK(vp[2] == 256 && vp[3] == 128, "Bind sets the viewport to the framebuffer size (got %dx%d)", vp[2], vp[3]);
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginBatch(256, 128);
            Renderer2D::DrawQuad({64, 64}, {40, 40}, glm::vec4(1, 0, 0, 1));
            Renderer2D::EndBatch();
            CHECK(Near(Read(64, 64), 255, 0, 0), "a quad drawn between Bind and Unbind lands in the framebuffer");
            CHECK(Near(Read(200, 100), bg.r, bg.g, bg.b), "outside the quad is the clear color");

            // Showing the texture over the window (ImGui panel) needs opaque alpha: translucent quads
            // drawn over the cleared framebuffer must not make it see-through.
            Renderer2D::BeginBatch(256, 128);
            Renderer2D::DrawQuad({200, 100}, {20, 20}, glm::vec4(1, 1, 1, 0.5f));
            Renderer2D::EndBatch();
            unsigned char blended[4];
            glReadPixels(200, 100, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, blended);
            CHECK(blended[3] == 255 && blended[0] > bg.r + 40, "a translucent quad blends its color but leaves alpha opaque (rgba %d,%d,%d,%d)", blended[0], blended[1], blended[2], blended[3]);
            fb.Unbind();

            Viewport(vp);
            CHECK(Binding() == 0 && vp[2] == (int)W && vp[3] == (int)H, "Unbind restores the window framebuffer and viewport (got %dx%d)", vp[2], vp[3]);
            CHECK(Near(Read(64, 64), bg.r, bg.g, bg.b), "nothing drawn into the framebuffer reached the window");

            // the color attachment is an ordinary texture holding the result
            std::vector<unsigned char> pixels(256 * 128 * 4);
            glBindTexture(GL_TEXTURE_2D, fb.GetColorAttachment());
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            glBindTexture(GL_TEXTURE_2D, 0);
            const unsigned char* red = &pixels[(64 + 64 * 256) * 4];
            CHECK(red[0] == 255 && red[1] == 0 && red[2] == 0, "the color attachment texture holds the rendered pixels (got %d,%d,%d)", red[0], red[1], red[2]);

            // resize keeps the texture id and gives a new size
            GLuint textureBefore = fb.GetColorAttachment();
            fb.Resize(64, 32);
            CHECK(fb.GetWidth() == 64 && fb.GetHeight() == 32 && fb.GetColorAttachment() == textureBefore, "Resize changes the size and keeps the texture id");
            fb.Bind();
            Viewport(vp);
            CHECK(vp[2] == 64 && vp[3] == 32, "Bind uses the resized viewport (got %dx%d)", vp[2], vp[3]);
            Renderer2D::BeginBatch(64, 32);
            Renderer2D::DrawQuad({32, 16}, {64, 32}, glm::vec4(0, 1, 0, 1));
            Renderer2D::EndBatch();
            CHECK(Near(Read(1, 1), 0, 255, 0) && Near(Read(62, 30), 0, 255, 0), "the resized framebuffer renders across its whole area");
            fb.Unbind();

            // a framebuffer bound inside another restores the outer one's target on Unbind
            OpenGLFramebuffer inner(32, 32);
            fb.Bind();
            inner.Bind();
            Viewport(vp);
            CHECK(vp[2] == 32 && vp[3] == 32, "nested Bind uses the inner size");
            inner.Unbind();
            Viewport(vp);
            CHECK(Binding() != 0 && vp[2] == 64 && vp[3] == 32, "nested Unbind returns to the outer framebuffer (got %dx%d)", vp[2], vp[3]);
            fb.Unbind();
            Viewport(vp);
            CHECK(Binding() == 0 && vp[2] == (int)W, "outer Unbind returns to the window");

            // a framebuffer that failed to create is inert
            OpenGLFramebuffer bad(0, 10);
            CHECK(!bad.IsComplete(), "a zero-size framebuffer is not complete");
            bad.Bind();
            bad.Resize(8, 8);
            Viewport(vp);
            CHECK(Binding() == 0 && vp[2] == (int)W && !bad.IsComplete(), "Bind and Resize on a failed framebuffer leave GL state alone");
            bad.Unbind();
            Viewport(vp);
            CHECK(Binding() == 0 && vp[2] == (int)W, "Unbind without a Bind leaves GL state alone");
        }

        // 9d. bitmap font text
        {
            BitmapFont font("Assets/Fonts/mono.png", 16, 32);
            CHECK(font.IsLoaded(), "the font sheet loads");
            CHECK(font.GetAdvance(32.0f) == 16.0f && font.GetAdvance(16.0f) == 8.0f, "glyphs keep the cell's 1:2 proportions");

            glm::vec2 box = font.Measure("ab\nabc\r\n", 32.0f);
            CHECK(box.x == 48.0f && box.y == 96.0f, "Measure is the widest line by the line count (got %.0f x %.0f)", box.x, box.y);
            CHECK(font.Measure("", 32.0f).x == 0.0f && font.Measure("", 32.0f).y == 32.0f, "an empty string measures zero wide, one line high");

            // quad counts: spaces and newlines draw nothing, everything shares one texture and one draw call
            auto DrawCounts = [&](const std::string& text) {
                glClear(GL_COLOR_BUFFER_BIT); Renderer2D::ResetStats();
                Renderer2D::BeginBatch(W, H);
                Renderer2D::DrawString(font, text, {100, 100}, 32.0f);
                Renderer2D::EndBatch();
                return Renderer2D::GetStats();
            };
            CHECK(DrawCounts("Hi there").QuadCount == 7 && DrawCounts("Hi there").DrawCalls == 1, "'Hi there' is 7 glyph quads in one draw call");
            CHECK(DrawCounts("a b\nc\r\n").QuadCount == 3, "spaces, newlines and carriage returns draw no quads");
            CHECK(DrawCounts("").QuadCount == 0 && DrawCounts("").DrawCalls == 0, "an empty string draws nothing");

            // Bounding box (and count) of the pixels that differ from the clear color in a region
            struct Bounds { int minX, minY, maxX, maxY, count; };
            auto Ink = [&](int x0, int y0, int w, int h) {
                std::vector<unsigned char> buf(w * h * 4);
                glReadPixels(x0, y0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
                Bounds b = { 1 << 20, 1 << 20, -1, -1, 0 };
                for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
                    const unsigned char* p = &buf[(y * w + x) * 4];
                    if (abs(p[0] - bg.r) > 12 || abs(p[1] - bg.g) > 12 || abs(p[2] - bg.b) > 12) {
                        b.minX = std::min(b.minX, x0 + x); b.maxX = std::max(b.maxX, x0 + x);
                        b.minY = std::min(b.minY, y0 + y); b.maxY = std::max(b.maxY, y0 + y);
                        b.count++;
                    }
                }
                return b;
            };
            auto DrawAt = [&](const std::string& text, float height, glm::vec4 color) {
                glClear(GL_COLOR_BUFFER_BIT);
                Renderer2D::BeginBatch(W, H);
                Renderer2D::DrawString(font, text, {100, 100}, height, color);
                Renderer2D::EndBatch();
            };

            // position is the bottom-left corner of the first cell: 'M' fills nearly all of 100..116 x 100..132
            DrawAt("M", 32.0f, glm::vec4(1.0f));
            Bounds m = Ink(60, 60, 200, 120);
            CHECK(m.count > 60 && m.minX >= 100 && m.maxX < 116 && m.minY >= 100 && m.maxY < 132, "a glyph stays inside its cell, anchored bottom-left (x %d..%d, y %d..%d)", m.minX, m.maxX, m.minY, m.maxY);
            CHECK(m.maxX - m.minX >= 10 && m.maxY - m.minY >= 14, "a glyph is drawn at the requested height (%d x %d px)", m.maxX - m.minX + 1, m.maxY - m.minY + 1);

            DrawAt("MM", 32.0f, glm::vec4(1.0f));
            Bounds mm = Ink(60, 60, 200, 120);
            CHECK(mm.minX >= 100 && mm.minX < 106 && mm.maxX >= 125 && mm.maxX < 132, "the next glyph is one advance to the right (x %d..%d)", mm.minX, mm.maxX);

            DrawAt("M\nM", 32.0f, glm::vec4(1.0f));
            Bounds nl = Ink(60, 20, 200, 160);
            CHECK(nl.minY >= 68 && nl.minY < 76 && nl.maxY >= 100 && nl.maxY < 132 && nl.minX >= 100 && nl.maxX < 116, "a newline starts the next line one height below (y %d..%d)", nl.minY, nl.maxY);

            DrawAt("M", 16.0f, glm::vec4(1.0f));
            Bounds half = Ink(60, 60, 200, 120);
            CHECK(half.maxX < 108 && half.maxY < 116 && half.count > 10 && half.count < m.count, "half the height draws a half-size glyph (x %d..%d, y %d..%d)", half.minX, half.maxX, half.minY, half.maxY);

            // different characters look different
            DrawAt(".", 32.0f, glm::vec4(1.0f));
            Bounds dot = Ink(60, 60, 200, 120);
            CHECK(dot.count > 0 && dot.count * 4 < m.count && dot.maxY < 116, "'.' is small and sits low in the cell (%d px, top y=%d)", dot.count, dot.maxY);

            // unknown characters draw as '?'
            auto Pixels = [&](const std::string& text) {
                DrawAt(text, 32.0f, glm::vec4(1.0f));
                std::vector<unsigned char> buf(40 * 40 * 4);
                glReadPixels(95, 95, 40, 40, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
                return buf;
            };
            std::vector<unsigned char> question = Pixels("?");
            CHECK(Ink(95, 95, 40, 40).count > 40, "'?' draws something");
            CHECK(Pixels("\x01") == question && Pixels("\xC3") == question && Pixels("\t") == question, "control and non-ASCII characters draw as '?'");

            // tint and alpha
            DrawAt("M", 32.0f, glm::vec4(0, 1, 0, 1));
            std::vector<unsigned char> green(200 * 120 * 4);
            glReadPixels(60, 60, 200, 120, GL_RGBA, GL_UNSIGNED_BYTE, green.data());
            int maxG = 0, maxR = 0, maxB = 0;
            for (size_t i = 0; i < green.size(); i += 4) {
                maxR = std::max(maxR, (int)green[i]); maxG = std::max(maxG, (int)green[i + 1]); maxB = std::max(maxB, (int)green[i + 2]);
            }
            CHECK(maxG >= 250 && maxR <= bg.r + 2 && maxB <= bg.b + 2, "the color tints the glyphs (max r,g,b %d,%d,%d)", maxR, maxG, maxB);

            DrawAt("M", 32.0f, glm::vec4(1, 1, 1, 0.5f));
            std::vector<unsigned char> faint(200 * 120 * 4);
            glReadPixels(60, 60, 200, 120, GL_RGBA, GL_UNSIGNED_BYTE, faint.data());
            int brightest = 0;
            for (size_t i = 0; i < faint.size(); i += 4) brightest = std::max(brightest, (int)faint[i]);
            CHECK(brightest > bg.r + 40 && brightest < 200, "alpha fades the text (brightest r %d)", brightest);

            // a font whose sheet fails to load draws the error checker, and a good one is unaffected
            BitmapFont missing("Assets/Fonts/does_not_exist.png", 16, 32);
            CHECK(!missing.IsLoaded(), "a missing font sheet is not loaded");
            glClear(GL_COLOR_BUFFER_BIT); Renderer2D::ResetStats();
            Renderer2D::BeginBatch(W, H);
            Renderer2D::DrawString(missing, "A", {100, 100}, 32.0f);
            Renderer2D::DrawQuad({300, 100}, {20, 20}, glm::vec4(0, 1, 0, 1));
            Renderer2D::EndBatch();
            Bounds err = Ink(60, 60, 100, 120);
            bool magenta = false;
            for (int y = 100; y < 132 && !magenta; y++) for (int x = 100; x < 116; x++) if (Near(Read(x, y), 255, 0, 255)) { magenta = true; break; }
            CHECK(Renderer2D::GetStats().QuadCount == 2 && err.count > 100 && magenta, "a failed font draws the error checker for each glyph");
            CHECK(Near(Read(300, 100), 0, 255, 0), "quads after a failed font still draw");
        }

        // 9e. scene rendering
        {
            ComponentRegistry registry;
            RegisterBuiltinComponents(registry);
            SceneRenderer sceneRenderer;

            auto Region = [&](int x, int y, int w, int h) {
                std::vector<unsigned char> pixels(w * h * 4);
                glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                return pixels;
            };
            auto Frame = [&]() { return Region(0, 0, (int)W, (int)H); };

            // Entities as JSON, drawn on a fresh frame through SceneRenderer
            auto Camera = [](float x, float y, float zoom, bool primary, float rotation = 0.0f) {
                return R"({"name": "cam)" + std::to_string(x) + "_" + std::to_string(primary) + R"(", "components": {"Transform": {"position": [)" + std::to_string(x) + ", " + std::to_string(y) + "], \"rotation\": " + std::to_string(rotation) + R"(}, "Camera": {"zoom": )" + std::to_string(zoom) + ", \"primary\": " + (primary ? "true" : "false") + "}}}";
            };
            auto Quad = [](const std::string& name, float x, float y, float size, const std::string& tint, float rotation = 0.0f) {
                return "{\"name\": \"" + name + R"(", "components": {"Transform": {"position": [)" + std::to_string(x) + ", " + std::to_string(y) + "], \"rotation\": " + std::to_string(rotation) + ", \"scale\": [" + std::to_string(size) + ", " + std::to_string(size) + R"(]}, "SpriteRenderer": {"tint": )" + tint + "}}}";
            };
            auto Render = [&](const std::vector<std::string>& entities) {
                std::string body;
                for (size_t i = 0; i < entities.size(); i++) body += (i ? "," : "") + entities[i];
                SceneLoadResult loaded = LoadSceneFromText(R"({"version": 1, "name": "t", "entities": [)" + body + "]}", "inline", registry);
                glClear(GL_COLOR_BUFFER_BIT); Renderer2D::ResetStats();
                sceneRenderer.Render(*loaded.Scene, W, H);
                return loaded.Problems.size();
            };
            const std::string red = "[1, 0, 0, 1]", green = "[0, 1, 0, 1]";

            // camera and sprites (the view is 1024 x 768 world units at zoom 1, the origin at the window's center)
            Render({ Camera(0, 0, 1, true), Quad("q", 100, 50, 40, red) });
            CHECK(Near(Read(612, 434), 255, 0, 0) && Near(Read(642, 434), bg.r, bg.g, bg.b) && Near(Read(612, 464), bg.r, bg.g, bg.b), "a sprite is drawn at its Transform, sized by its scale");
            Render({ Camera(100, 50, 1, true), Quad("q", 100, 50, 40, red) });
            CHECK(Near(Read(512, 384), 255, 0, 0), "the camera's position centers the view");
            Render({ Camera(0, 0, 2, true), Quad("q", 100, 50, 40, red) });
            CHECK(Near(Read(712, 484), 255, 0, 0) && Near(Read(747, 484), 255, 0, 0) && Near(Read(762, 484), bg.r, bg.g, bg.b), "zoom 2 doubles positions and sizes");

            Render({ Camera(30, -20, 1.5f, true, 0.5f), Quad("q", 100, 50, 40, red, 0.3f) });
            std::vector<unsigned char> viaScene = Frame();
            glClear(GL_COLOR_BUFFER_BIT);
            OrthographicCamera direct(-512.0f / 1.5f, 512.0f / 1.5f, -384.0f / 1.5f, 384.0f / 1.5f);
            direct.SetPosition({ 30.0f, -20.0f });
            direct.SetRotation(0.5f);
            Renderer2D::BeginBatch(direct);
            Renderer2D::DrawRotatedQuad({ 100.0f, 50.0f }, { 40.0f, 40.0f }, 0.3f, glm::vec4(1, 0, 0, 1));
            Renderer2D::EndBatch();
            std::vector<unsigned char> viaRenderer = Frame();
            size_t inked = 0;   // pixels that are not the clear color: the 40 x 40 quad at zoom 1.5 is about 3600
            for (size_t i = 0; i < viaScene.size(); i += 4)
                inked += abs(viaScene[i] - bg.r) > 2 || abs(viaScene[i + 1] - bg.g) > 2 || abs(viaScene[i + 2] - bg.b) > 2;
            CHECK(inked > 3000 && inked < 4200, "the rotated, zoomed quad is on screen");
            CHECK(viaScene == viaRenderer, "camera position, rotation and zoom give the same picture as Renderer2D with an OrthographicCamera");

            Render({ Camera(0, 0, 1, true), Quad("under", 0, 0, 60, green), Quad("over", 0, 0, 40, red) });
            CHECK(Near(Read(512, 384), 255, 0, 0) && Near(Read(512 + 25, 384), 0, 255, 0), "entities are drawn in creation order, later ones on top");
            Render({ Camera(0, 0, 1, true), Quad("over", 0, 0, 40, red), Quad("under", 0, 0, 60, green) });
            CHECK(Near(Read(512, 384), 0, 255, 0), "and in the opposite order the other is on top");

            Render({ Camera(500, 0, 1, false), Camera(0, 0, 1, true), Camera(300, 0, 1, true), Quad("q", 0, 0, 40, red) });
            CHECK(Near(Read(512, 384), 255, 0, 0), "the first primary camera is used");
            CHECK(SceneRenderer::FindPrimaryCamera(*LoadSceneFromText(R"({"version": 1, "name": "t", "entities": [{"name": "a", "components": {"Camera": {"primary": true}}}]})", "inline", registry).Scene) == Entity(), "a primary camera without a Transform is not used");
            Render({ Quad("q", 0, 0, 40, red) });
            CHECK(Near(Read(512, 384), 255, 0, 0), "without a primary camera the view is centered on the origin");
            Render({});
            CHECK(Near(Read(512, 384), bg.r, bg.g, bg.b) && Renderer2D::GetStats().QuadCount == 0, "an empty scene draws nothing");

            Render({ Camera(0, 0, 1, true), R"({"name": "ghost", "components": {"SpriteRenderer": {"tint": [1, 0, 0, 1]}}})", R"({"name": "plain", "components": {"Transform": {}}})" });
            CHECK(Near(Read(512, 384), bg.r, bg.g, bg.b) && Renderer2D::GetStats().QuadCount == 0, "a sprite without a Transform, and an entity with no sprite, are not drawn");

            // textures
            OpenGLTexture2D sprite("Assets/Textures/test_sprite.tga"), sheet("Assets/Textures/test_sheet.tga");
            const std::string sheetPath = "Assets/Textures/test_sheet.tga";
            Render({ Camera(0, 0, 1, true), R"({"name": "t", "components": {"Transform": {"scale": [128, 128]}, "SpriteRenderer": {"texture": "Assets/Textures/test_sprite.tga"}}})" });
            std::vector<unsigned char> textured = Region(448, 320, 128, 128);
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginBatch(W, H);
            Renderer2D::DrawQuad({ 512, 384 }, { 128, 128 }, &sprite);
            Renderer2D::EndBatch();
            CHECK(textured == Region(448, 320, 128, 128) && !Near(Read(512, 384), bg.r, bg.g, bg.b), "a textured sprite draws the whole texture");

            Render({ Camera(0, 0, 1, true), R"({"name": "t", "components": {"Transform": {"scale": [64, 64]}, "SpriteRenderer": {"texture": "Assets/Textures/test_sheet.tga", "cell": [1, 2], "cellSize": [32, 32], "tint": [0, 1, 0, 1]}}})" });
            std::vector<unsigned char> cell = Region(480, 352, 64, 64);
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginBatch(W, H);
            Renderer2D::DrawQuad({ 512, 384 }, { 64, 64 }, SubTexture2D::CreateFromCoords(&sheet, { 1, 2 }, { 32, 32 }), glm::vec4(0, 1, 0, 1));
            Renderer2D::EndBatch();
            CHECK(cell == Region(480, 352, 64, 64), "a sprite sheet cell and a tint draw as the same cell and tint through Renderer2D");

            Render({ Camera(0, 0, 1, true), R"({"name": "t", "components": {"Transform": {"scale": [64, 64]}, "SpriteRenderer": {"texture": "Assets/Textures/missing_for_scene_test.png"}}})", Quad("after", 200, 0, 20, green) });
            bool checker = false;
            for (int y = 352; y < 416 && !checker; y++) for (int x = 480; x < 544; x++) if (Near(Read(x, y), 255, 0, 255)) { checker = true; break; }
            CHECK(checker && Near(Read(712, 384), 0, 255, 0), "a missing texture draws the error checker and the rest of the scene is unaffected");
            Render({ Camera(0, 0, 1, true), R"({"name": "t", "components": {"Transform": {"scale": [64, 64]}, "SpriteRenderer": {"texture": "Assets/Textures/missing_for_scene_test.png", "cell": [1, 1], "cellSize": [32, 32]}}})" });
            checker = false;
            for (int y = 352; y < 416 && !checker; y++) for (int x = 480; x < 544; x++) if (Near(Read(x, y), 255, 0, 255)) { checker = true; break; }
            CHECK(checker, "a missing texture with a sheet cell draws the checker too");

            // tilemaps: rows are listed top to bottom, '.' is no tile, the origin is the bottom-left corner
            auto TilemapEntity = [](const std::string& rows, const std::string& transform = "", const std::string& texture = "Assets/Textures/test_sheet.tga") {
                return R"({"name": "ground", "components": {)" + (transform.empty() ? "" : R"("Transform": )" + transform + ", ") + R"("Tilemap": {"texture": ")" + texture + R"(", "cellSize": [32, 32], "tileSize": [64, 64], "rows": )" + rows + "}}}";
            };
            Render({ Camera(0, 0, 1, true), TilemapEntity(R"(["1.", "03"])") });
            CHECK(Renderer2D::GetStats().QuadCount == 3, "a tile that is '.' draws no quad");
            std::vector<unsigned char> tiles = Region(512, 384, 128, 128);
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginBatch(W, H);
            Renderer2D::DrawQuad({ 544, 480 }, { 64, 64 }, SubTexture2D::CreateFromCoords(&sheet, { 1, 0 }, { 32, 32 }));   // top left: '1'
            Renderer2D::DrawQuad({ 544, 416 }, { 64, 64 }, SubTexture2D::CreateFromCoords(&sheet, { 0, 0 }, { 32, 32 }));   // bottom left: '0'
            Renderer2D::DrawQuad({ 608, 416 }, { 64, 64 }, SubTexture2D::CreateFromCoords(&sheet, { 3, 0 }, { 32, 32 }));   // bottom right: '3'
            Renderer2D::EndBatch();
            CHECK(tiles == Region(512, 384, 128, 128) && Near(Read(608, 480), bg.r, bg.g, bg.b), "tiles land where the rows say, the first row on top, and '.' leaves the background");

            Render({ Camera(0, 0, 1, true), TilemapEntity(R"(["1.", "03"])", R"({"position": [100, -50], "scale": [2, 1]})") });
            std::vector<unsigned char> moved = Region(612, 334, 256, 128);
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginBatch(W, H);
            Renderer2D::DrawQuad({ 612 + 64, 334 + 96 }, { 128, 64 }, SubTexture2D::CreateFromCoords(&sheet, { 1, 0 }, { 32, 32 }));
            Renderer2D::DrawQuad({ 612 + 64, 334 + 32 }, { 128, 64 }, SubTexture2D::CreateFromCoords(&sheet, { 0, 0 }, { 32, 32 }));
            Renderer2D::DrawQuad({ 612 + 192, 334 + 32 }, { 128, 64 }, SubTexture2D::CreateFromCoords(&sheet, { 3, 0 }, { 32, 32 }));
            Renderer2D::EndBatch();
            CHECK(moved == Region(612, 334, 256, 128), "the Transform moves the map's bottom-left corner and its scale multiplies the tile size");

            Render({ Camera(0, 0, 1, true), TilemapEntity(R"(["z"])") });
            checker = false;
            for (int y = 384; y < 448 && !checker; y++) for (int x = 512; x < 576; x++) if (Near(Read(x, y), 255, 0, 255)) { checker = true; break; }
            CHECK(checker, "a tile beyond the sheet draws the error checker");
            Render({ Camera(0, 0, 1, true), TilemapEntity(R"(["ab", "cd"])", "", "Assets/Textures/missing_for_scene_test.png") });
            checker = false;
            for (int y = 384; y < 512 && !checker; y++) for (int x = 512; x < 640; x++) if (Near(Read(x, y), 255, 0, 255)) { checker = true; break; }
            CHECK(checker && Renderer2D::GetStats().QuadCount == 4, "every tile of a map whose sheet is missing draws the error checker");
            Render({ Camera(0, 0, 1, true), TilemapEntity(R"(["a"])", "", "") });
            checker = false;
            for (int y = 384; y < 448 && !checker; y++) for (int x = 512; x < 576; x++) if (Near(Read(x, y), 255, 0, 255)) { checker = true; break; }
            CHECK(checker, "a map with no sheet draws the error checker");

            // sprites and tiles share the draw order, and a big map is cheap
            std::string bigRows = "[";
            for (int r = 0; r < 24; r++) bigRows += std::string(r ? "," : "") + "\"" + std::string(32, "0123"[r % 4]) + "\"";
            bigRows += "]";
            Render({ Camera(1024, 768, 1, true), TilemapEntity(bigRows, R"({"position": [0, 0]})"), Quad("on_top", 1024, 768, 30, red) });
            auto bigStats = Renderer2D::GetStats();
            CHECK(bigStats.QuadCount == 769 && bigStats.DrawCalls == 1 && Near(Read(512, 384), 255, 0, 0), "a 32 x 24 map is 768 quads in one draw call, with a sprite on top");
        }

        // 9f. drawing between simulation steps
        {
            ComponentRegistry registry;
            RegisterBuiltinComponents(registry);
            registry.Register<Slide>("Slide", "Slides.").Field(&Slide::Velocity, "velocity", "v")
                .Behavior([](Slide& slide, BehaviorContext& ctx) { if (Transform* t = ctx.Self.TryGet<Transform>()) t->Position += slide.Velocity * (float)ctx.Dt; });
            SceneRenderer sceneRenderer;

            auto Frame = [&]() {
                std::vector<unsigned char> pixels((size_t)W * (size_t)H * 4);
                glReadPixels(0, 0, (int)W, (int)H, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                return pixels;
            };
            const std::string camera = R"({"name": "camera", "components": {"Transform": {}, "Camera": {"primary": true}}})";
            auto Scene1 = [&](const std::string& entities) {
                return LoadSceneFromText(R"({"version": 1, "name": "t", "entities": [)" + camera + "," + entities + "]}", "inline", registry);
            };
            auto Draw = [&](const Scene& scene, float alpha) { glClear(GL_COLOR_BUFFER_BIT); sceneRenderer.Render(scene, W, H, alpha); return Frame(); };
            auto Direct = [&](glm::vec2 center, glm::vec2 size, float rotation) {   // a red quad drawn straight through Renderer2D, in pixels
                glClear(GL_COLOR_BUFFER_BIT);
                Renderer2D::BeginBatch(W, H);
                Renderer2D::DrawRotatedQuad({ 512.0f + center.x, 384.0f + center.y }, size, rotation, glm::vec4(1, 0, 0, 1));
                Renderer2D::EndBatch();
                return Frame();
            };
            const std::string slider = R"({"name": "q", "components": {"Transform": {"scale": [40, 40]}, "SpriteRenderer": {"tint": [1, 0, 0, 1]}, "Slide": {"velocity": [6000, 0]}}})";

            // a step moves the entity 100 units (6000 per second for 1/60 s); the picture is where it was, between, or where it is
            SceneLoadResult slide = Scene1(slider);
            Simulation::Step(registry, *slide.Scene, InputFrame(), 1.0 / 60.0);
            CHECK(glm::length(slide.Scene->FindEntity("q").Get<Transform>().Position - glm::vec2(100.0f, 0.0f)) < 0.001f && slide.Scene->FindEntity("q").Get<PreviousTransform>().Position == glm::vec2(0.0f), "a step moves the entity (about 100 units: 6000 per second for 1/60 s) and records where it was");
            std::vector<unsigned char> atStart = Draw(*slide.Scene, 0.0f), half = Draw(*slide.Scene, 0.5f), atEnd = Draw(*slide.Scene, 1.0f);
            CHECK(atStart == Direct({ 0, 0 }, { 40, 40 }, 0.0f), "alpha 0 draws it where it was");
            CHECK(half == Direct({ 50, 0 }, { 40, 40 }, 0.0f), "alpha one half draws it halfway");
            CHECK(atEnd == Direct({ 100, 0 }, { 40, 40 }, 0.0f), "alpha 1 draws it where it is");
            glClear(GL_COLOR_BUFFER_BIT);
            sceneRenderer.Render(*slide.Scene, W, H);
            CHECK(Frame() == atEnd, "without an alpha the current state is drawn");
            CHECK(Draw(*slide.Scene, -3.0f) == atStart && Draw(*slide.Scene, 7.0f) == atEnd, "an alpha outside 0 to 1 is held to it");
            Draw(*slide.Scene, 0.5f);
            CHECK(Near(Read(562, 384), 255, 0, 0) && Near(Read(512, 384), bg.r, bg.g, bg.b) && Near(Read(612, 384), bg.r, bg.g, bg.b), "at one half the sprite is neither where it was nor where it is");
            Simulation::Step(registry, *slide.Scene, InputFrame(), 1.0 / 60.0);
            CHECK(Draw(*slide.Scene, 0.5f) == Direct({ 150, 0 }, { 40, 40 }, 0.0f), "after the next step it is drawn between the last two positions");

            // rotation and scale are drawn between too
            SceneLoadResult turning = Scene1(R"({"name": "q", "components": {"Transform": {"rotation": 1.5707963, "scale": [60, 20]}, "SpriteRenderer": {"tint": [1, 0, 0, 1]}}})");
            turning.Scene->FindEntity("q").Add<PreviousTransform>() = PreviousTransform{ { 0.0f, 0.0f }, 0.0f, { 20.0f, 60.0f } };
            CHECK(Draw(*turning.Scene, 0.5f) == Direct({ 0, 0 }, { 40, 40 }, 0.78539815f), "rotation and scale are blended too");

            // the camera moves between steps, so a still sprite appears to move the other way
            SceneLoadResult panning = Scene1(R"({"name": "still", "components": {"Transform": {"position": [50, 0], "scale": [40, 40]}, "SpriteRenderer": {"tint": [1, 0, 0, 1]}}})");
            panning.Scene->FindEntity("camera").Get<Transform>().Position = { 100.0f, 0.0f };
            panning.Scene->FindEntity("camera").Add<PreviousTransform>() = PreviousTransform{ { 0.0f, 0.0f }, 0.0f, { 1.0f, 1.0f } };
            CHECK(Draw(*panning.Scene, 0.5f) == Direct({ 0, 0 }, { 40, 40 }, 0.0f) && Draw(*panning.Scene, 1.0f) == Direct({ -50, 0 }, { 40, 40 }, 0.0f) && Draw(*panning.Scene, 0.0f) == Direct({ 50, 0 }, { 40, 40 }, 0.0f), "the camera is drawn between steps too");

            // an entity with no previous transform is drawn where it is, whatever the alpha
            SceneLoadResult fresh = Scene1(R"({"name": "new", "components": {"Transform": {"position": [100, 0], "scale": [40, 40]}, "SpriteRenderer": {"tint": [1, 0, 0, 1]}}})");
            CHECK(Draw(*fresh.Scene, 0.0f) == Direct({ 100, 0 }, { 40, 40 }, 0.0f), "an entity that has not been through a step is drawn where it is");

            // a tilemap moves with its transform
            SceneLoadResult map = Scene1(R"({"name": "ground", "components": {"Transform": {"position": [64, 0]}, "Tilemap": {"texture": "Assets/Textures/test_sheet.tga", "cellSize": [32, 32], "tileSize": [64, 64], "rows": ["0"]}}})");
            map.Scene->FindEntity("ground").Add<PreviousTransform>() = PreviousTransform{ { 0.0f, 0.0f }, 0.0f, { 1.0f, 1.0f } };
            std::vector<unsigned char> tile = Draw(*map.Scene, 0.5f);
            OpenGLTexture2D sheet("Assets/Textures/test_sheet.tga");
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginBatch(W, H);
            Renderer2D::DrawQuad({ 512.0f + 32.0f + 32.0f, 384.0f + 32.0f }, { 64, 64 }, SubTexture2D::CreateFromCoords(&sheet, { 0, 0 }, { 32, 32 }));
            Renderer2D::EndBatch();
            CHECK(tile == Frame(), "a tilemap is drawn halfway between where it was and where it is");
        }

        // 9f. lines and circles
        {
            auto IsBg = [&](int x, int y) { return Near(Read(x, y), bg.r, bg.g, bg.b); };

            // a thick horizontal line: coloured along it, square at its ends, nothing beside it
            glClear(GL_COLOR_BUFFER_BIT); Renderer2D::ResetStats();
            Renderer2D::BeginBatch(W, H);
            Renderer2D::DrawLine({ 100, 200 }, { 300, 200 }, glm::vec4(1, 0, 0, 1), 10.0f);
            Renderer2D::EndBatch();
            CHECK(Near(Read(100 + 1, 200), 255, 0, 0) && Near(Read(200, 200), 255, 0, 0) && Near(Read(300 - 2, 200), 255, 0, 0) && Near(Read(200, 203), 255, 0, 0) && Near(Read(200, 196), 255, 0, 0), "a line is coloured along its length and as thick as asked");
            CHECK(IsBg(95, 200) && IsBg(305, 200) && IsBg(200, 207) && IsBg(200, 193), "and stops at its ends and edges");
            CHECK(Renderer2D::GetStats().QuadCount == 1 && Renderer2D::GetStats().DrawCalls == 1, "a line is one quad");

            // a diagonal one, drawn from either end, is the same line
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginBatch(W, H);
            Renderer2D::DrawLine({ 600, 100 }, { 700, 200 }, glm::vec4(0, 1, 0, 1), 6.0f);
            Renderer2D::EndBatch();
            CHECK(Near(Read(650, 150), 0, 255, 0) && Near(Read(620, 120), 0, 255, 0) && IsBg(650, 170) && IsBg(670, 130), "a diagonal line follows its direction");

            // nothing to draw
            glClear(GL_COLOR_BUFFER_BIT); Renderer2D::ResetStats();
            Renderer2D::BeginBatch(W, H);
            Renderer2D::DrawLine({ 100, 100 }, { 100, 100 }, glm::vec4(1), 5.0f);
            Renderer2D::DrawLine({ 100, 100 }, { 200, 100 }, glm::vec4(1), 0.0f);
            Renderer2D::DrawCircle({ 100, 100 }, 0.0f, glm::vec4(1));
            Renderer2D::EndBatch();
            CHECK(Renderer2D::GetStats().QuadCount == 0 && IsBg(100, 100) && IsBg(150, 100), "a zero-length or zero-width line and a zero radius circle draw nothing");

            // a filled circle
            glClear(GL_COLOR_BUFFER_BIT); Renderer2D::ResetStats();
            Renderer2D::BeginBatch(W, H);
            Renderer2D::DrawCircle({ 400, 300 }, 50.0f, glm::vec4(0, 1, 0, 1));
            Renderer2D::EndBatch();
            CHECK(Near(Read(400, 300), 0, 255, 0) && Near(Read(440, 300), 0, 255, 0) && Near(Read(400, 345), 0, 255, 0) && Near(Read(360, 300), 0, 255, 0), "a circle is filled inside its radius");
            CHECK(IsBg(400, 355) && IsBg(455, 300) && IsBg(445, 345) && IsBg(355, 255), "and empty outside it, corners of its quad included");
            Px edge = Read(449, 300);
            CHECK(!Near(edge, bg.r, bg.g, bg.b, 10) && !Near(edge, 0, 255, 0, 10) && edge.g > bg.g, "its edge is anti-aliased: a pixel on it is part way between the circle and the background (g %d)", edge.g);
            CHECK(Renderer2D::GetStats().QuadCount == 1 && Renderer2D::GetStats().DrawCalls == 1, "a circle is one quad");

            // a ring
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginBatch(W, H);
            Renderer2D::DrawCircle({ 400, 300 }, 50.0f, glm::vec4(0, 0, 1, 1), 10.0f);
            Renderer2D::EndBatch();
            CHECK(IsBg(400, 300) && IsBg(400 + 30, 300) && IsBg(400, 300 + 35), "a ring is empty inside");
            CHECK(Near(Read(400 + 45, 300), 0, 0, 255) && Near(Read(400, 300 + 43), 0, 0, 255) && Near(Read(400 - 47, 300), 0, 0, 255) && IsBg(400 + 55, 300), "and coloured as wide as asked, just inside the radius");
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginBatch(W, H);
            Renderer2D::DrawCircle({ 400, 300 }, 50.0f, glm::vec4(0, 0, 1, 1), 50.0f);
            Renderer2D::DrawCircle({ 700, 300 }, 50.0f, glm::vec4(0, 0, 1, 1), 500.0f);
            Renderer2D::EndBatch();
            CHECK(Near(Read(400, 300), 0, 0, 255) && Near(Read(700, 300), 0, 0, 255), "a thickness of the radius or more is a disk");

            // a camera: the circle is round in the world and the edge stays sharp however far in it is zoomed
            glClear(GL_COLOR_BUFFER_BIT);
            Renderer2D::BeginBatch(OrthographicCamera(-W / 8.0f, W / 8.0f, -H / 8.0f, H / 8.0f));   // 4 times zoomed in
            Renderer2D::DrawCircle({ 0, 0 }, 10.0f, glm::vec4(1, 1, 0, 1));
            Renderer2D::DrawLine({ -50, 0 }, { 50, 0 }, glm::vec4(1, 0, 1, 1), 1.0f);
            Renderer2D::EndBatch();
            const int cx = (int)(W / 2), cy = (int)(H / 2);
            CHECK(Near(Read(cx + 3, cy + 20), 255, 255, 0) && Near(Read(cx + 35, cy + 3), 255, 255, 0) && IsBg(cx + 45, cy + 20) && Near(Read(cx + 100, cy), 255, 0, 255), "in a camera's world units a circle of 10 is 40 pixels at 4 times zoom, and a line is 4 pixels wide");

            // drawn in order with quads, and alpha blends
            glClear(GL_COLOR_BUFFER_BIT); Renderer2D::ResetStats();
            Renderer2D::BeginBatch(W, H);
            Renderer2D::DrawQuad({ 400, 300 }, { 200, 20 }, glm::vec4(1, 0, 0, 1));
            Renderer2D::DrawCircle({ 400, 300 }, 40.0f, glm::vec4(0, 1, 0, 1));
            Renderer2D::DrawQuad({ 400, 300 }, { 20, 200 }, glm::vec4(0, 0, 1, 1));
            Renderer2D::DrawLine({ 300, 300 }, { 500, 300 }, glm::vec4(1, 1, 1, 0.5f), 4.0f);
            Renderer2D::EndBatch();
            CHECK(Renderer2D::GetStats().QuadCount == 4 && Renderer2D::GetStats().DrawCalls == 1, "lines and circles batch with quads in one draw call");
            Px blended = Read(430, 300);
            CHECK(Near(Read(430, 306), 0, 255, 0) && Near(Read(480, 306), 255, 0, 0) && Near(Read(400, 325), 0, 0, 255), "they are drawn in submission order: the circle over the red bar, the blue bar over the circle");
            CHECK(blended.r > 100 && blended.r < 160 && blended.g > 240 && blended.b > 100 && blended.b < 160, "and a translucent line blends over what is under it (r,g,b %d,%d,%d)", blended.r, blended.g, blended.b);
        }

        // 10. GL errors
        GLenum err = glGetError();
        CHECK(err == GL_NO_ERROR, "no GL error (0x%x)", err);

        printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
        exit(fails ? 1 : 0);
    }
};

int main() { RendererTestApp app; app.Run(); }
