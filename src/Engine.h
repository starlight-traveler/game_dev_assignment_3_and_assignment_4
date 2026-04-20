/**
 * @file Engine.h
 * @brief Public engine API functions for end-user application code
 */
#ifndef ENGINE_H
#define ENGINE_H

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "CollisionResponse.h"
#include "ConvexCollision.h"
#include "Integration.h"

class AnimationClip;
class SkeletalRig;

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
 * @brief Integrates a managed object's linear velocity into position
 * @param render_element Render element id
 * @param delta_time Delta time in seconds, or negative to use the current frame delta
 * @return True when a matching object was updated
 */
bool integrateLinear(std::uint32_t render_element, float delta_time = -1.0f);

/**
 * @brief Integrates a managed object's angular velocity into orientation
 * @param render_element Render element id
 * @param delta_time Delta time in seconds, or negative to use the current frame delta
 * @return True when a matching object was updated
 */
bool integrateAngular(std::uint32_t render_element, float delta_time = -1.0f);

/**
 * @brief Integrates linear acceleration into a managed object's velocity
 * @param render_element Render element id
 * @param delta_time Delta time in seconds
 * @param acceleration Linear acceleration in world units per second squared
 * @return True when a matching object was updated
 */
bool integrateAcceleration(std::uint32_t render_element,
                           float delta_time,
                           const glm::vec3& acceleration);

/**
 * @brief Integrates angular acceleration into a managed object's angular velocity
 * @param render_element Render element id
 * @param delta_time Delta time in seconds
 * @param acceleration Angular acceleration in radians per second squared
 * @return True when a matching object was updated
 */
bool integrateAngularAcceleration(std::uint32_t render_element,
                                  float delta_time,
                                  const glm::vec3& acceleration);

/**
 * @brief Applies an instantaneous linear velocity change to a managed object
 * @param render_element Render element id
 * @param impulse Velocity delta to apply
 * @return True when a matching object was updated
 */
bool applyLinearImpulse(std::uint32_t render_element, const glm::vec3& impulse);

/**
 * @brief Applies an instantaneous angular velocity change to a managed object
 * @param render_element Render element id
 * @param impulse Angular velocity delta to apply
 * @return True when a matching object was updated
 */
bool applyAngularImpulse(std::uint32_t render_element, const glm::vec3& impulse);

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
 * @brief Gets linear velocity for a render element id
 * @param render_element Render element id
 * @return Linear velocity or zero vector when not found
 */
glm::vec3 getLinearVelocityForRenderElement(std::uint32_t render_element);

/**
 * @brief Gets angular velocity for a render element id
 * @param render_element Render element id
 * @return Angular velocity or zero vector when not found
 */
glm::vec3 getAngularVelocityForRenderElement(std::uint32_t render_element);

/**
 * @brief Reports whether an RTS game object is currently moving under a command
 * @param render_element Render element id
 * @return True when command-driven movement is active
 */
bool isRtsGameObjectMoving(std::uint32_t render_element);

/**
 * @brief Associates a local-space bounds template with a render element id
 * @param render_element Render element id
 * @param min_bounds Minimum local-space corner
 * @param max_bounds Maximum local-space corner
 * @return True when the template was accepted
 */
bool setLocalBoundsForRenderElement(std::uint32_t render_element,
                                    const glm::vec3& min_bounds,
                                    const glm::vec3& max_bounds);

/**
 * @brief Clears a render element's local bounds template
 * @param render_element Render element id
 * @return True when a template or active object bounds were cleared
 */
bool clearLocalBoundsForRenderElement(std::uint32_t render_element);

/**
 * @brief Associates a local-space convex support set with a render element id
 * @param render_element Render element id
 * @param points Local-space support points
 * @return True when the support set was accepted
 */
bool setConvexHullForRenderElement(std::uint32_t render_element,
                                   const std::vector<glm::vec3>& points);

/**
 * @brief Clears a render element's explicit convex support set
 * @param render_element Render element id
 * @return True when a template or active object hull was cleared
 */
bool clearConvexHullForRenderElement(std::uint32_t render_element);

/**
 * @brief Reports whether a world-space AABB is available for a render element id
 * @param render_element Render element id
 * @return True when the object exists and has bounds
 */
bool hasAabbForRenderElement(std::uint32_t render_element);

/**
 * @brief Gets the current world-space AABB minimum for a render element id
 * @param render_element Render element id
 * @return Minimum corner or zero vector when unavailable
 */
glm::vec3 getAabbMinForRenderElement(std::uint32_t render_element);

/**
 * @brief Gets the current world-space AABB maximum for a render element id
 * @param render_element Render element id
 * @return Maximum corner or zero vector when unavailable
 */
glm::vec3 getAabbMaxForRenderElement(std::uint32_t render_element);

/**
 * @brief Queries the convex narrow phase between two managed objects
 * @param first_render_element First render element id
 * @param second_render_element Second render element id
 * @return Intersection plus separation data for the current world transforms
 */
ConvexCollisionQueryResult queryConvexCollisionBetweenRenderElements(
    std::uint32_t first_render_element,
    std::uint32_t second_render_element);

/**
 * @brief Returns the built-in collision type used by RTS game objects
 * @return Collision type id for spawnRtsGameObject instances
 */
std::uint32_t getRtsGameObjectCollisionType();

/**
 * @brief Allocates a new collision type id for user objects
 * @return Newly allocated collision type id
 */
std::uint32_t registerCollisionType();

/**
 * @brief Assigns a collision type id to a managed object
 * @param render_element Render element id
 * @param collision_type_id Collision type to assign
 * @return True when the object exists and the type id was stored
 */
bool setCollisionTypeForRenderElement(std::uint32_t render_element,
                                      std::uint32_t collision_type_id);

/**
 * @brief Registers one event-driven callback for collisions between two types
 * @param type_a First collision type id
 * @param type_b Second collision type id
 * @param response Callback to invoke on overlap
 * @return True when the callback was stored
 */
bool registerCollisionResponse(std::uint32_t type_a,
                               std::uint32_t type_b,
                               CollisionResponse response);

/**
 * @brief Clears all registered collision response callbacks
 */
void clearCollisionResponses();

/**
 * @brief Assigns or clears a skeletal rig on a managed object
 * @param render_element Render element id
 * @param skeletal_rig Shared rig pointer or nullptr
 * @return True when the object exists and the rig was stored
 */
bool setSkeletalRigForRenderElement(std::uint32_t render_element,
                                    std::shared_ptr<const SkeletalRig> skeletal_rig);

/**
 * @brief Assigns or clears an animation clip on a managed object
 * @param render_element Render element id
 * @param animation_clip Shared clip pointer or nullptr
 * @return True when the object exists and the clip was stored
 */
bool setAnimationClipForRenderElement(
    std::uint32_t render_element,
    std::shared_ptr<const AnimationClip> animation_clip);

/**
 * @brief Starts animation playback for a managed object
 * @param render_element Render element id
 * @param loop Whether playback should wrap at the end of the clip
 * @param restart Whether playback time should be reset to zero
 * @return True when playback started
 */
bool playAnimationForRenderElement(std::uint32_t render_element,
                                   bool loop = false,
                                   bool restart = true);

/**
 * @brief Stops animation playback for a managed object
 * @param render_element Render element id
 * @return True when the object exists and playback was stopped
 */
bool stopAnimationForRenderElement(std::uint32_t render_element);

/**
 * @brief Reports whether a managed object is actively playing an animation
 * @param render_element Render element id
 * @return True when the object exists and playback is active
 */
bool isAnimationPlayingForRenderElement(std::uint32_t render_element);

/**
 * @brief Returns the current skin matrices for a managed object
 * @param render_element Render element id
 * @return Copy of the object's skin matrix array or an empty vector when unavailable
 */
std::vector<glm::mat4> getSkinMatricesForRenderElement(std::uint32_t render_element);

#endif
