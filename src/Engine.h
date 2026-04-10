/**
 * @file Engine.h
 * @brief Public engine API functions for end-user application code
 */
#ifndef ENGINE_H
#define ENGINE_H

#include <cstdint>

#include <glm/glm.hpp>

/**
 * @brief Called automatically during engine boot to initialize user game state
 */
void initialize();

/**
 * @brief Gets the most recent frame delta in milliseconds
 * @return Delta time in milliseconds
 */
std::uint64_t getDeltaTime();

/**
 * @brief Gets the most recent frame delta in seconds
 * @return Delta time in seconds
 */
float getDeltaSeconds();

/**
 * @brief Spawns an RTS game object tied to a render element id
 * @param render_element Render element id
 * @param position Initial world position
 * @param linear_velocity Linear velocity in world units per second
 * @param angular_velocity Angular velocity vector in radians per second
 * @return True when spawn succeeds
 */
bool spawnRtsGameObject(std::uint32_t render_element,
                        const glm::vec3& position,
                        const glm::vec3& linear_velocity,
                        const glm::vec3& angular_velocity);

/**
 * @brief Issues a move command to an RTS game object
 * @param render_element Render element id
 * @param target_position Destination in world space
 * @param move_speed Requested speed in world units per second
 * @param arrival_radius Distance threshold for snapping to the target
 * @return True when the object accepted the move command
 */
bool issueMoveCommand(std::uint32_t render_element,
                      const glm::vec3& target_position,
                      float move_speed,
                      float arrival_radius = 0.05f);

/**
 * @brief Stops an RTS game object that is currently moving under a command
 * @param render_element Render element id
 * @return True when the stop request succeeded
 */
bool stopRtsGameObject(std::uint32_t render_element);

/**
 * @brief Removes a game object by render element id
 * @param render_element Render element id
 * @return True when removal succeeds
 */
bool destroyGameObject(std::uint32_t render_element);

/**
 * @brief Updates all active game objects for one frame
 */
void updateActiveGameObjects();

/**
 * @brief Clears all active game objects
 */
void clearActiveGameObjects();

/**
 * @brief Gets model transform for a render element id
 * @param render_element Render element id
 * @return Model matrix or identity when not found
 */
glm::mat4 getModelForRenderElement(std::uint32_t render_element);

/**
 * @brief Gets world position for a render element id
 * @param render_element Render element id
 * @return Position or zero vector when not found
 */
glm::vec3 getPositionForRenderElement(std::uint32_t render_element);

/**
 * @brief Reports whether an RTS game object is currently moving under a command
 * @param render_element Render element id
 * @return True when command-driven movement is active
 */
bool isRtsGameObjectMoving(std::uint32_t render_element);

#endif
