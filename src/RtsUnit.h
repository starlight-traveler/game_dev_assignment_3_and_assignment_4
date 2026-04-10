/**
 * @file RtsUnit.h
 * @brief small concrete GameObject used for unit style movement demos
 */
#ifndef RTSUNIT_H
#define RTSUNIT_H

#include <glm/glm.hpp>

#include "GameObject.h"

/**
 * @brief minimal command driven unit wrapper on top of GameObject
 *
 * this class is much simpler than RtsWorld UnitState
 * it is mainly useful where a concrete GameObject subclass is needed
 * so this is more like a tiny teaching example than the full simulation unit model
 *
 * think of it as a tiny self contained movement example
 * it remembers one target point
 * walks toward that point at a constant speed
 * and rotates to face the direction of travel
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
     *
     * when a move target is active update behaves like a guided mover
     * otherwise it falls back to the inherited GameObject velocity integration
     */
    void update(float delta_seconds) override;

    /**
     * @brief Starts command-driven movement toward a world-space target
     * @param target_position Destination in world space
     * @param move_speed Requested speed in world units per second
     * @param arrival_radius Distance threshold for snapping to the target
     * @return True when the command is valid and accepted
     *
     * this does not pathfind
     * it only drives straight toward the target in open space
     */
    bool issueMoveCommand(const glm::vec3& target_position,
                          float move_speed,
                          float arrival_radius) override;

    /**
     * @brief Stops command-driven movement immediately
     * @return Always true for RTS units
     */
    bool stopMoveCommand() override;

    /**
     * @brief Reports whether a move command is currently active
     * @return True when the unit is navigating toward a target
     */
    bool isMoving() const override;

private:
    /**
     * @brief Rotates the unit so its local +X axis faces the given direction on the XZ plane
     * @param direction Desired facing direction
     *
     * only x and z matter here because the unit uses yaw only
     */
    void faceDirection(const glm::vec3& direction);

    // whether this wrapper is currently overriding base GameObject movement
    bool has_move_target_;
    // current world space destination for the straight line move command
    glm::vec3 move_target_;
    // requested travel speed while the command is active
    float move_speed_;
    // distance threshold where the unit snaps to the exact target and stops
    float arrival_radius_;
};

#endif
