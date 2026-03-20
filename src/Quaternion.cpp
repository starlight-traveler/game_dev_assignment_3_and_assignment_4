#include "Quaternion.h"

#include <glm/gtc/constants.hpp>

namespace {
/**
 * @brief Returns a safe normalized axis for axis-angle construction
 * @param axis Axis candidate
 * @return Unit-length axis or Y axis fallback
 */
glm::vec3 normalize_axis_or_default(const glm::vec3& axis) {
    const float axis_length = glm::length(axis);
    if (axis_length <= 0.000001f) {
        return glm::vec3(0.0f, 1.0f, 0.0f);
    }
    return axis / axis_length;
}
}  // namespace

Quaternion::Quaternion() : quaternion_(0.0f, 0.0f, 0.0f, 1.0f) {}

Quaternion::Quaternion(const glm::vec3& axis, float angle_radians) : quaternion_(0.0f) {
    const glm::vec3 normalized_axis = normalize_axis_or_default(axis);
    const float half_angle = angle_radians * 0.5f;
    const float sin_half = glm::sin(half_angle);
    const float cos_half = glm::cos(half_angle);
    quaternion_.x = normalized_axis.x * sin_half;
    quaternion_.y = normalized_axis.y * sin_half;
    quaternion_.z = normalized_axis.z * sin_half;
    quaternion_.w = cos_half;
    normalize();
}

Quaternion::Quaternion(float x, float y, float z, float w)
    : quaternion_(x, y, z, w) {}

Quaternion Quaternion::operator*(const Quaternion& rhs) const {
    const float x1 = quaternion_.x;
    const float y1 = quaternion_.y;
    const float z1 = quaternion_.z;
    const float w1 = quaternion_.w;

    const float x2 = rhs.quaternion_.x;
    const float y2 = rhs.quaternion_.y;
    const float z2 = rhs.quaternion_.z;
    const float w2 = rhs.quaternion_.w;

    const float x = (w1 * x2) + (x1 * w2) + (y1 * z2) - (z1 * y2);
    const float y = (w1 * y2) - (x1 * z2) + (y1 * w2) + (z1 * x2);
    const float z = (w1 * z2) + (x1 * y2) - (y1 * x2) + (z1 * w2);
    const float w = (w1 * w2) - (x1 * x2) - (y1 * y2) - (z1 * z2);

    return Quaternion(x, y, z, w);
}

glm::vec3 Quaternion::operator*(const glm::vec3& rhs) const {
    const Quaternion point(rhs.x, rhs.y, rhs.z, 0.0f);
    const Quaternion rotated = (*this) * point * conjugate();
    return glm::vec3(rotated.quaternion_.x, rotated.quaternion_.y, rotated.quaternion_.z);
}

Quaternion::operator glm::mat4() const {
    const glm::vec3 basis_x = (*this) * glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 basis_y = (*this) * glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 basis_z = (*this) * glm::vec3(0.0f, 0.0f, 1.0f);

    glm::mat4 rotation(1.0f);
    rotation[0] = glm::vec4(basis_x, 0.0f);
    rotation[1] = glm::vec4(basis_y, 0.0f);
    rotation[2] = glm::vec4(basis_z, 0.0f);
    return rotation;
}

Quaternion Quaternion::conjugate() const {
    return Quaternion(-quaternion_.x, -quaternion_.y, -quaternion_.z, quaternion_.w);
}

void Quaternion::normalize() {
    const float magnitude = glm::length(quaternion_);
    if (magnitude <= 0.000001f) {
        quaternion_ = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    quaternion_ /= magnitude;
}
