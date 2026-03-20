#include "GameObject.h"

#include <glm/gtc/matrix_transform.hpp>

GameObject::GameObject(std::uint32_t render_element,
                       const glm::vec3& position,
                       const Quaternion& rotation,
                       const glm::vec3& linear_velocity,
                       const glm::vec3& angular_velocity)
    : position_(position),
      rotation_(rotation),
      render_element_(render_element),
      linear_velocity_(linear_velocity),
      angular_velocity_(angular_velocity) {}

glm::mat4 GameObject::getModel() const {
    const glm::mat4 translation = glm::translate(glm::mat4(1.0f), position_);
    const glm::mat4 rotation = static_cast<glm::mat4>(rotation_);
    return translation * rotation;
}

std::uint32_t GameObject::getRenderElement() const {
    return render_element_;
}

void GameObject::integrateVelocity(float delta_seconds) {
    position_ += linear_velocity_ * delta_seconds;
}

void GameObject::integrateAngularVelocity(float delta_seconds) {
    const float speed = glm::length(angular_velocity_);
    if (speed <= 0.000001f || delta_seconds <= 0.0f) {
        return;
    }

    const glm::vec3 axis = angular_velocity_ / speed;
    const float angle = speed * delta_seconds;
    const Quaternion delta_rotation(axis, angle);
    rotation_ = delta_rotation * rotation_;
    rotation_.normalize();
}
