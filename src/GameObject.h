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
     * @brief Issues an RTS-style move command when supported by the concrete object
     * @param target_position World-space destination
     * @param move_speed Requested move speed in world units per second
     * @param arrival_radius Distance at which the object counts as arrived
     * @return True when the object accepted the command
     */
    virtual bool issueMoveCommand(const glm::vec3& target_position,
                                  float move_speed,
                                  float arrival_radius);

    /**
     * @brief Stops active command-driven movement when supported by the concrete object
     * @return True when the object handled the stop request
     */
    virtual bool stopMoveCommand();

    /**
     * @brief Reports whether the object is currently moving under an RTS command
     * @return True when a command-driven move is active
     */
    virtual bool isMoving() const;

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

    /**
     * @brief Returns the object's world-space position
     * @return Current position
     */
    glm::vec3 getPosition() const;

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
