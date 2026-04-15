/**
 * @file Quaternion.h
 * @brief Custom quaternion class for 3D orientation and rotation
 */
#ifndef QUATERNION_H
#define QUATERNION_H

#include <glm/glm.hpp>

/**
 * @brief Stores a unit quaternion and exposes rotation operations
 *
 * This class is the engine's orientation type for 3D rotation
 * Instead of storing yaw, pitch, and roll directly on every object,
 * the engine stores a unit quaternion so rotations can be composed
 * cleanly over time without relying on repeated Euler-angle accumulation
 *
 * Conceptually, the quaternion stores two kinds of information
 * - the imaginary xyz part encodes the rotation axis contribution
 * - the real w part encodes the scalar contribution
 *
 * In this engine the quaternion is kept normalized because unit quaternions
 * represent pure rotations
 */
class Quaternion {
public:
    /**
     * @brief Constructs an identity quaternion
     *
     * Identity means "no rotation"
     * Rotating any vector by the identity quaternion leaves it unchanged
     */
    Quaternion();

    /**
     * @brief Constructs a quaternion from axis-angle data
     * @param axis Rotation axis
     * @param angle_radians Rotation angle in radians
     *
     * This is the main constructor used by the engine when angular velocity
     * is converted into a frame-sized rotation
     *
     * The axis is normalized internally
     * The quaternion components are built from the standard axis-angle form
     * using sine and cosine of the half-angle
     */
    Quaternion(const glm::vec3& axis, float angle_radians);

    /**
     * @brief Constructs directly from quaternion components and normalizes the result
     * @param x X component
     * @param y Y component
     * @param z Z component
     * @param w W component
     * @return Quaternion built from those components
     */
    static Quaternion fromComponents(float x, float y, float z, float w);

    /**
     * @brief Multiplies two quaternions with the Hamilton product
     * @param rhs Right-hand quaternion
     * @return Result quaternion
     *
     * Quaternion multiplication composes rotations
     * The order matters, so this is not interchangeable like scalar multiplication
     */
    Quaternion operator*(const Quaternion& rhs) const;

    /**
     * @brief Rotates a vector by this quaternion
     * @param rhs Vector to rotate
     * @return Rotated vector
     *
     * Internally the vector is treated as a pure quaternion with w = 0
     * and rotated with q * p * q.conjugate()
     */
    glm::vec3 operator*(const glm::vec3& rhs) const;

    /**
     * @brief Converts this quaternion to a rotation matrix
     *
     * This is used by GameObject when it builds a model matrix for rendering
     */
    operator glm::mat4() const;

    /**
     * @brief Returns the conjugate quaternion
     * @return Conjugate result
     *
     * For a unit quaternion, the conjugate is also the inverse rotation
     */
    Quaternion conjugate() const;

    /**
     * @brief Returns the raw quaternion components as x y z w
     * @return Underlying component vector
     */
    glm::vec4 components() const;

    /**
     * @brief Normalizes this quaternion to unit length
     *
     * Re-normalization is important after repeated floating point operations
     * so the quaternion continues to represent a stable pure rotation
     */
    void normalize();

    /**
     * @brief Spherically interpolates between two quaternions
     * @param from Start rotation
     * @param to End rotation
     * @param t Blend factor on [0, 1]
     * @return Interpolated unit quaternion
     */
    static Quaternion slerp(const Quaternion& from, const Quaternion& to, float t);

private:
    /**
     * @brief Constructs directly from quaternion components
     * @param x X component
     * @param y Y component
     * @param z Z component
     * @param w W component
     *
     * This constructor is private because external code should usually build
     * quaternions through the safer identity or axis-angle paths
     */
    Quaternion(float x, float y, float z, float w);

    // storage order is x, y, z, w
    // GLM vec4 is used simply as a compact container for the four quaternion components
    glm::vec4 quaternion_;
};

#endif
