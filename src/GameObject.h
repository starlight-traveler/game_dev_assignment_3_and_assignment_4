/**
 * @file GameObject.h
 * @brief Abstract base class for engine-managed 3D game objects
 */
#ifndef GAMEOBJECT_H
#define GAMEOBJECT_H

#include <cstdint>

#include <glm/glm.hpp>

#include "Quaternion.h"

/**
 * @brief Base class for visible objects in world space
 */
class GameObject {
public:
    /**
     * @brief Virtual destructor for polymorphic ownership
     */
    virtual ~GameObject() = default;

    /**
     * @brief Executes one frame of object logic
     * @param delta_seconds Delta time for the frame in seconds
     */
    virtual void update(float delta_seconds) = 0;

    /**
     * @brief Builds a world model matrix from position and rotation
     * @return Model transform matrix
     */
    glm::mat4 getModel() const;

    /**
     * @brief Returns render element id mapped to this object
     * @return Render element id
     */
    std::uint32_t getRenderElement() const;

protected:
    /**
     * @brief Constructs base state for a game object
     * @param render_element Render element id
     * @param position Initial world position
     * @param rotation Initial world rotation
     * @param linear_velocity Linear velocity in world units per second
     * @param angular_velocity Angular velocity vector in radians per second
     */
    GameObject(std::uint32_t render_element,
               const glm::vec3& position,
               const Quaternion& rotation,
               const glm::vec3& linear_velocity,
               const glm::vec3& angular_velocity);

    /**
     * @brief Integrates linear velocity into position
     * @param delta_seconds Delta time in seconds
     */
    void integrateVelocity(float delta_seconds);

    /**
     * @brief Integrates angular velocity into quaternion rotation
     * @param delta_seconds Delta time in seconds
     */
    void integrateAngularVelocity(float delta_seconds);

    glm::vec3 position_;
    Quaternion rotation_;
    std::uint32_t render_element_;
    glm::vec3 linear_velocity_;
    glm::vec3 angular_velocity_;
};

#endif
