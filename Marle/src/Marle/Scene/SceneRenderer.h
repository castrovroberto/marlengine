#pragma once

#include "Marle/Core.h"
#include "Marle/Platform/OpenGL/OpenGLTexture.h"
#include "Scene.h"
#include <memory>
#include <set>
#include <string>
#include <unordered_map>

namespace Marle {

    // Draws a Scene with Renderer2D: needs a GL context, and must be destroyed while it is still alive
    // (it owns the textures the scene refers to, loaded the first time they are used; a file that
    // cannot be loaded is drawn as the error checker).
    //
    // The scene is drawn through its primary camera (the first entity, in creation order, with a
    // Camera that is Primary and a Transform), or through a default camera at the origin if there is
    // none. Entities are drawn in creation order, so later ones are on top:
    //   - a SpriteRenderer at its Transform: a flat color, a texture, or one cell of a sheet
    //   - a Tilemap at its Transform: one quad per tile
    // An entity needs a Transform to be drawn (a Tilemap without one sits at the origin).
    class EXPORT SceneRenderer
    {
        public:
            SceneRenderer() = default;
            SceneRenderer(const SceneRenderer&) = delete;
            SceneRenderer& operator=(const SceneRenderer&) = delete;

            // Draws the scene as one frame batch into the current framebuffer. The viewport size is
            // in pixels; the camera shows that many world units at zoom 1. `alpha` (0 to 1) says how
            // far the frame is between the previous simulation step and the current one: an entity
            // with a PreviousTransform (recorded by Simulation::Step) is drawn that far from where it
            // was toward where it is, camera included. 1, the default, draws the current state.
            void Render(const Scene& scene, float viewportWidth, float viewportHeight, float alpha = 1.0f);

            static Entity FindPrimaryCamera(const Scene& scene);

        private:
            OpenGLTexture2D* GetTexture(const std::string& path);
            void ReportOnce(const std::string& message);

            std::unordered_map<std::string, std::unique_ptr<OpenGLTexture2D>> m_Textures;
            std::set<std::string> m_Reported;
    };

}
