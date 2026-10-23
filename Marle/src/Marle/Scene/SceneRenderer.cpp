#include "mrlpch.h"
#include "Marle/Log.h"
#include "Marle/Renderer/Renderer2D.h"
#include "Marle/Renderer/OrthographicCamera.h"
#include "Marle/Renderer/SubTexture2D.h"
#include "SceneRenderer.h"
#include "Components.h"

namespace Marle {

    Entity SceneRenderer::FindPrimaryCamera(const Scene& scene)
    {
        for (Entity entity : scene.GetEntities()) {
            const Camera* camera = entity.TryGet<Camera>();
            if (camera && camera->Primary && entity.Has<Transform>()) return entity;
        }
        return Entity();
    }

    OpenGLTexture2D* SceneRenderer::GetTexture(const std::string& path)
    {
        auto it = m_Textures.find(path);
        if (it == m_Textures.end())
            it = m_Textures.emplace(path, std::make_unique<OpenGLTexture2D>(path)).first;   // a failed load is kept, not retried every frame
        return it->second.get();
    }

    void SceneRenderer::ReportOnce(const std::string& message)
    {
        if (m_Reported.insert(message).second) MRL_CORE_WARN("SceneRenderer: {}", message);
    }

    // The Transform to draw: the current one, or between the previous step's and the current one
    static Transform Interpolated(Entity entity, const Transform& current, float alpha)
    {
        const PreviousTransform* previous = entity.TryGet<PreviousTransform>();
        if (!previous || alpha >= 1.0f) return current;

        Transform drawn;
        drawn.Position = glm::mix(previous->Position, current.Position, alpha);
        drawn.Rotation = glm::mix(previous->Rotation, current.Rotation, alpha);
        drawn.Scale = glm::mix(previous->Scale, current.Scale, alpha);
        return drawn;
    }

    void SceneRenderer::Render(const Scene& scene, float viewportWidth, float viewportHeight, float alpha)
    {
        alpha = glm::clamp(alpha, 0.0f, 1.0f);

        glm::vec2 cameraPosition(0.0f);
        float cameraRotation = 0.0f, zoom = 1.0f;
        if (Entity camera = FindPrimaryCamera(scene)) {
            const Transform view = Interpolated(camera, camera.Get<Transform>(), alpha);
            cameraPosition = view.Position;
            cameraRotation = view.Rotation;
            if (camera.Get<Camera>().Zoom > 0.0f) zoom = camera.Get<Camera>().Zoom;
        } else {
            ReportOnce("scene '" + scene.GetName() + "' has no primary camera (a Camera with primary true and a Transform); using one at the origin");
        }

        // The visible area shrinks as zoom grows; the camera sits at its Transform
        const float halfWidth = viewportWidth * 0.5f / zoom, halfHeight = viewportHeight * 0.5f / zoom;
        OrthographicCamera camera(-halfWidth, halfWidth, -halfHeight, halfHeight);
        camera.SetPosition(cameraPosition);
        camera.SetRotation(cameraRotation);

        Renderer2D::BeginBatch(camera);

        for (Entity entity : scene.GetEntities()) {
            const Transform* current = entity.TryGet<Transform>();
            const Transform drawn = current ? Interpolated(entity, *current, alpha) : Transform();
            const Transform* transform = current ? &drawn : nullptr;
            const SpriteRenderer* sprite = entity.TryGet<SpriteRenderer>();
            const Tilemap* map = entity.TryGet<Tilemap>();

            if (sprite && !transform)
                ReportOnce("entity '" + entity.GetName() + "' has a SpriteRenderer but no Transform, so it is not drawn");

            if (sprite && transform) {
                if (sprite->Texture.empty()) {
                    Renderer2D::DrawRotatedQuad(transform->Position, transform->Scale, transform->Rotation, sprite->Tint);
                } else {
                    OpenGLTexture2D* texture = GetTexture(sprite->Texture);
                    if (texture->IsLoaded() && sprite->CellSize.x > 0.0f && sprite->CellSize.y > 0.0f) {
                        SubTexture2D cell = SubTexture2D::CreateFromCoords(texture, { (float)sprite->Cell.x, (float)sprite->Cell.y }, sprite->CellSize);
                        Renderer2D::DrawRotatedQuad(transform->Position, transform->Scale, transform->Rotation, cell, sprite->Tint);
                    } else {
                        Renderer2D::DrawRotatedQuad(transform->Position, transform->Scale, transform->Rotation, texture, sprite->Tint);
                    }
                }
            }

            if (map) {
                const glm::vec2 origin = transform ? transform->Position : glm::vec2(0.0f);
                const glm::vec2 tile = map->TileSize * (transform ? transform->Scale : glm::vec2(1.0f));
                OpenGLTexture2D* texture = map->Texture.empty() ? nullptr : GetTexture(map->Texture);

                // How many cells the sheet has, so a character can be turned into a column and row
                int columns = 0, sheetRows = 0;
                if (texture && texture->IsLoaded() && map->CellSize.x > 0.0f && map->CellSize.y > 0.0f) {
                    columns = (int)((float)texture->GetWidth() / map->CellSize.x);
                    sheetRows = (int)((float)texture->GetHeight() / map->CellSize.y);
                }

                const size_t rowCount = map->Rows.size();
                for (size_t row = 0; row < rowCount; row++) {
                    const std::string& tiles = map->Rows[row];
                    for (size_t column = 0; column < tiles.size(); column++) {
                        const int cell = TilemapCell(tiles[column]);
                        if (cell == -1) continue;   // no tile

                        const glm::vec2 center = TilemapTileCenter(*map, origin, tile, column, row);
                        if (cell < 0 || cell >= columns * sheetRows) {
                            Renderer2D::DrawQuad(center, tile, (OpenGLTexture2D*)nullptr);   // the error checker
                            continue;
                        }
                        SubTexture2D sub = SubTexture2D::CreateFromCoords(texture, { (float)(cell % columns), (float)(cell / columns) }, map->CellSize);
                        Renderer2D::DrawQuad(center, tile, sub);
                    }
                }
            }
        }

        Renderer2D::EndBatch();
    }

}
