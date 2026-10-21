#pragma once

#include "../Core.h"
#include <glm/glm.hpp>

namespace Marle {

    // 2D orthographic camera. The projection bounds define the visible world area when
    // the camera sits at the origin (zoom = shrink/grow the bounds). Rotation is in radians,
    // counter-clockwise positive, about the camera position.
    class EXPORT OrthographicCamera
    {
        public:
            OrthographicCamera(float left, float right, float bottom, float top);

            void SetProjection(float left, float right, float bottom, float top);

            const glm::vec2& GetPosition() const { return m_Position; }
            void SetPosition(const glm::vec2& position) { m_Position = position; RecalculateViewMatrix(); }

            float GetRotation() const { return m_Rotation; }
            void SetRotation(float radians) { m_Rotation = radians; RecalculateViewMatrix(); }

            const glm::mat4& GetProjectionMatrix() const { return m_ProjectionMatrix; }
            const glm::mat4& GetViewMatrix() const { return m_ViewMatrix; }
            const glm::mat4& GetViewProjectionMatrix() const { return m_ViewProjectionMatrix; }

        private:
            void RecalculateViewMatrix();

            glm::mat4 m_ProjectionMatrix;
            glm::mat4 m_ViewMatrix = glm::mat4(1.0f);
            glm::mat4 m_ViewProjectionMatrix;

            glm::vec2 m_Position = { 0.0f, 0.0f };
            float m_Rotation = 0.0f;
    };

}
