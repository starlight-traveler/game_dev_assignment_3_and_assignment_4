/**
 * @file RtsUnit.h
 * @brief Concrete GameObject type for minimal RTS-style world entities
 */
#ifndef RTSUNIT_H
#define RTSUNIT_H

#include "GameObject.h"

/**
 * @brief Basic RTS unit with velocity-driven movement and rotation
 */
class RtsUnit final : public GameObject {
public:
    /**
     * @brief Constructs a minimal RTS unit
     * @param render_element Render element id
     * @param position Initial world position
     * @param linear_velocity Linear velocity in world units per second
     * @param angular_velocity Angular velocity vector in radians per second
     */
    RtsUnit(std::uint32_t render_element,
            const glm::vec3& position,
            const glm::vec3& linear_velocity,
            const glm::vec3& angular_velocity);

    /**
     * @brief Updates this unit for one frame
     * @param delta_seconds Delta time in seconds
     */
    void update(float delta_seconds) override;
};

#endif
