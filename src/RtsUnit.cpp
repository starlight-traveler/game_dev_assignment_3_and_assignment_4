#include "RtsUnit.h"

RtsUnit::RtsUnit(std::uint32_t render_element,
                 const glm::vec3& position,
                 const glm::vec3& linear_velocity,
                 const glm::vec3& angular_velocity)
    : GameObject(render_element,
                 position,
                 Quaternion(glm::vec3(0.0f, 1.0f, 0.0f), 0.0f),
                 linear_velocity,
                 angular_velocity) {}

void RtsUnit::update(float delta_seconds) {
    integrateVelocity(delta_seconds);
    integrateAngularVelocity(delta_seconds);
}
