/**
 * @file Quaternion.h
 * @brief Custom quaternion class for 3D orientation and rotation
 */
#ifndef QUATERNION_H
#define QUATERNION_H

#include <glm/glm.hpp>

/**
 * @brief Stores a unit quaternion and exposes rotation operations
 */
class Quaternion {
public:
    /**
     * @brief Constructs an identity quaternion
     */
    Quaternion();

    /**
     * @brief Constructs a quaternion from axis-angle data
     * @param axis Rotation axis
     * @param angle_radians Rotation angle in radians
     */
    Quaternion(const glm::vec3& axis, float angle_radians);

    /**
     * @brief Multiplies two quaternions with the Hamilton product
     * @param rhs Right-hand quaternion
     * @return Result quaternion
     */
    Quaternion operator*(const Quaternion& rhs) const;

    /**
     * @brief Rotates a vector by this quaternion
     * @param rhs Vector to rotate
     * @return Rotated vector
     */
    glm::vec3 operator*(const glm::vec3& rhs) const;

    /**
     * @brief Converts this quaternion to a rotation matrix
     */
    operator glm::mat4() const;

    /**
     * @brief Returns the conjugate quaternion
     * @return Conjugate result
     */
    Quaternion conjugate() const;

    /**
     * @brief Normalizes this quaternion to unit length
     */
    void normalize();

private:
    /**
     * @brief Constructs directly from quaternion components
     * @param x X component
     * @param y Y component
     * @param z Z component
     * @param w W component
     */
    Quaternion(float x, float y, float z, float w);

    // storage order is x, y, z, w
    glm::vec4 quaternion_;
};

#endif
