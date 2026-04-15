#include "Quaternion.h"

#include <algorithm>

#include <glm/gtc/constants.hpp>

namespace {
/**
 * @brief Returns a safe normalized axis for axis-angle construction
 * @param axis Axis candidate
 * @return Unit-length axis or Y axis fallback
 */
glm::vec3 normalize_axis_or_default(const glm::vec3& axis) {
    // Axis-angle rotation needs a direction-only axis
    // If the caller passes a zero-length axis, dividing by its length would be invalid
    const float axis_length = glm::length(axis);
    if (axis_length <= 0.000001f) {
        // Fall back to world up so the quaternion is still well-defined
        return glm::vec3(0.0f, 1.0f, 0.0f);
    }

    // Standard normalization removes magnitude and keeps only direction
    return axis / axis_length;
}
}  // namespace

// Identity quaternion
// xyz = 0 means no axis contribution and w = 1 means zero rotation
Quaternion::Quaternion() : quaternion_(0.0f, 0.0f, 0.0f, 1.0f) {}

Quaternion::Quaternion(const glm::vec3& axis, float angle_radians) : quaternion_(0.0f) {
    // Convert the incoming axis into a unit direction first
    // The axis should describe direction only, not speed or scale
    const glm::vec3 normalized_axis = normalize_axis_or_default(axis);

    // Axis-angle quaternions store half of the requested angle
    // This half-angle form is what makes q * p * q.conjugate() produce the full rotation
    const float half_angle = angle_radians * 0.5f;
    const float sin_half = glm::sin(half_angle);
    const float cos_half = glm::cos(half_angle);

    // Quaternion layout here is (x, y, z, w)
    // The xyz portion is the axis scaled by sin(theta / 2)
    // The w portion is cos(theta / 2)
    quaternion_.x = normalized_axis.x * sin_half;
    quaternion_.y = normalized_axis.y * sin_half;
    quaternion_.z = normalized_axis.z * sin_half;
    quaternion_.w = cos_half;

    // Floating point math and fallback axes can still leave tiny drift
    // Normalize so the stored quaternion is unit length
    normalize();
}

Quaternion Quaternion::fromComponents(float x, float y, float z, float w) {
    Quaternion quaternion(x, y, z, w);
    quaternion.normalize();
    return quaternion;
}

Quaternion::Quaternion(float x, float y, float z, float w)
    : quaternion_(x, y, z, w) {}

Quaternion Quaternion::operator*(const Quaternion& rhs) const {
    // Pull out components into scalar temporaries so the Hamilton product reads clearly
    const float x1 = quaternion_.x;
    const float y1 = quaternion_.y;
    const float z1 = quaternion_.z;
    const float w1 = quaternion_.w;

    const float x2 = rhs.quaternion_.x;
    const float y2 = rhs.quaternion_.y;
    const float z2 = rhs.quaternion_.z;
    const float w2 = rhs.quaternion_.w;

    // Hamilton product
    // This is quaternion composition, not component-wise multiplication
    // The order matters because rotation composition is not commutative
    //
    // Conceptually:
    // - the scalar term w interacts with the vector xyz terms
    // - the xyz terms also cross-mix with sign changes
    // - the result is a new quaternion representing the combined rotation
    const float x = (w1 * x2) + (x1 * w2) + (y1 * z2) - (z1 * y2);
    const float y = (w1 * y2) - (x1 * z2) + (y1 * w2) + (z1 * x2);
    const float z = (w1 * z2) + (x1 * y2) - (y1 * x2) + (z1 * w2);
    const float w = (w1 * w2) - (x1 * x2) - (y1 * y2) - (z1 * z2);

    return Quaternion(x, y, z, w);
}

glm::vec3 Quaternion::operator*(const glm::vec3& rhs) const {
    // To rotate a 3D vector with a quaternion, the vector is embedded into quaternion space
    // as a pure quaternion whose real component is zero
    const Quaternion point(rhs.x, rhs.y, rhs.z, 0.0f);

    // Standard quaternion vector rotation
    // q * p applies the first half of the transform into quaternion space
    // multiplying by q.conjugate() finishes the rotation and cancels the extra scalar terms
    const Quaternion rotated = (*this) * point * conjugate();

    // The rotated spatial result comes back out of the xyz components
    return glm::vec3(rotated.quaternion_.x, rotated.quaternion_.y, rotated.quaternion_.z);
}

Quaternion::operator glm::mat4() const {
    // Build the rotation matrix by rotating the three basis vectors
    // The rotated basis becomes the columns of the final matrix
    const glm::vec3 basis_x = (*this) * glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 basis_y = (*this) * glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 basis_z = (*this) * glm::vec3(0.0f, 0.0f, 1.0f);

    // Start from identity so the bottom row and translation column are already valid
    glm::mat4 rotation(1.0f);

    // GLM matrices are column major
    // Writing basis vectors into columns produces the equivalent 3x3 rotation block
    rotation[0] = glm::vec4(basis_x, 0.0f);
    rotation[1] = glm::vec4(basis_y, 0.0f);
    rotation[2] = glm::vec4(basis_z, 0.0f);
    return rotation;
}

Quaternion Quaternion::conjugate() const {
    // Conjugation flips the axis portion and leaves the scalar portion unchanged
    // For a unit quaternion this is the inverse rotation
    return Quaternion(-quaternion_.x, -quaternion_.y, -quaternion_.z, quaternion_.w);
}

glm::vec4 Quaternion::components() const {
    return quaternion_;
}

void Quaternion::normalize() {
    // Unit quaternions represent pure rotations
    // Re-normalizing prevents accumulated floating point error from slowly stretching the quaternion
    const float magnitude = glm::length(quaternion_);
    if (magnitude <= 0.000001f) {
        // A near-zero quaternion cannot be normalized safely
        // Fall back to identity so the engine keeps a valid rotation
        quaternion_ = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    // Divide each component by the 4D magnitude to get unit length
    quaternion_ /= magnitude;
}

Quaternion Quaternion::slerp(const Quaternion& from, const Quaternion& to, float t) {
    const float clamped_t = std::clamp(t, 0.0f, 1.0f);

    glm::vec4 start = from.quaternion_;
    glm::vec4 end = to.quaternion_;
    float cosine = glm::dot(start, end);

    if (cosine < 0.0f) {
        end = -end;
        cosine = -cosine;
    }

    if (cosine > 0.9995f) {
        const glm::vec4 blended = start + (end - start) * clamped_t;
        return Quaternion::fromComponents(blended.x, blended.y, blended.z, blended.w);
    }

    const float theta_0 = std::acos(std::clamp(cosine, -1.0f, 1.0f));
    const float theta = theta_0 * clamped_t;
    const glm::vec4 orthogonal = glm::normalize(end - start * cosine);
    const glm::vec4 blended =
        start * std::cos(theta) + orthogonal * std::sin(theta);
    return Quaternion::fromComponents(blended.x, blended.y, blended.z, blended.w);
}
