/**
 * @file Utility.h
 * @brief Internal utility state and helpers for engine runtime data
 */
#ifndef UTILITY_H
#define UTILITY_H

#include <cstddef>
#include <cstdint>
#include <memory>

#include <glm/glm.hpp>

#include "CollisionResponse.h"

class GameObject;

namespace utility {
/**
 * @brief Stores the current frame delta values
 * @param delta_time_ms Frame delta in milliseconds
 * @param delta_seconds Frame delta in seconds
 */
void setFrameDelta(std::uint64_t delta_time_ms, float delta_seconds);

/**
 * @brief Gets the current frame delta in milliseconds
 * @return Frame delta in milliseconds
 */
std::uint64_t deltaTimeMs();

/**
 * @brief Gets the current frame delta in seconds
 * @return Frame delta in seconds
 */
float deltaSeconds();

/**
 * @brief Adds a game object to the active engine list
 * @param object Game object ownership pointer
 * @return New active object count
 */
std::size_t addGameObject(std::unique_ptr<GameObject> object);

/**
 * @brief Removes a game object by its render element id
 * @param render_element Render element id
 * @return True when an object was removed
 */
bool removeGameObjectByRenderElement(std::uint32_t render_element);

/**
 * @brief Updates all active game objects and runs BVH-backed broad-phase collision dispatch
 * @param delta_seconds Delta time in seconds
 */
void updateGameObjects(float delta_seconds);

/**
 * @brief Clears all active game objects
 */
void clearGameObjects();

/**
 * @brief Finds an active game object by render element id
 * @param render_element Render element id
 * @return Pointer to matching object or nullptr
 */
const GameObject* findGameObjectByRenderElement(std::uint32_t render_element);

/**
 * @brief Finds a mutable active game object by render element id
 * @param render_element Render element id
 * @return Mutable pointer to matching object or nullptr
 */
GameObject* findMutableGameObjectByRenderElement(std::uint32_t render_element);

/**
 * @brief Stores a local-space bounds template for a render element id
 * @param render_element Render element id
 * @param min_bounds Minimum local-space corner
 * @param max_bounds Maximum local-space corner
 * @return True when the template was accepted
 */
bool setLocalBoundsByRenderElement(std::uint32_t render_element,
                                   const glm::vec3& min_bounds,
                                   const glm::vec3& max_bounds);

/**
 * @brief Clears any local-space bounds template for a render element id
 * @param render_element Render element id
 * @return True when a template or active object bounds were cleared
 */
bool clearLocalBoundsByRenderElement(std::uint32_t render_element);

/**
 * @brief Returns the built-in collision type id used by RTS units
 * @return RTS collision type id
 */
std::uint32_t rtsCollisionType();

/**
 * @brief Registers a new collision type id for callback routing
 * @return Newly allocated collision type id
 */
std::uint32_t registerCollisionType();

/**
 * @brief Registers one callback for collisions between two type ids
 * @param type_a First collision type id
 * @param type_b Second collision type id
 * @param response Callback to invoke on overlap
 * @return True when the callback was stored in the triangular collision table
 */
bool registerCollisionResponse(std::uint32_t type_a,
                               std::uint32_t type_b,
                               CollisionResponse response);

/**
 * @brief Clears all registered collision response callbacks
 */
void clearCollisionResponses();
}  // namespace utility

#endif
