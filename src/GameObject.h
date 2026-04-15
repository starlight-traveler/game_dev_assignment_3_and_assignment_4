/**
 * @file GameObject.h
 * @brief Abstract base class for engine-managed 3D game objects
 */
#ifndef GAMEOBJECT_H
#define GAMEOBJECT_H

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "AnimationClip.h"
#include "Quaternion.h"
#include "SkeletalRig.h"

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

    /**
     * @brief Returns the object's world-space orientation
     * @return Current quaternion rotation
     */
    Quaternion getRotation() const;

    /**
     * @brief Returns the current linear velocity
     * @return Velocity in world units per second
     */
    glm::vec3 getLinearVelocity() const;

    /**
     * @brief Returns the current angular velocity
     * @return Angular velocity in radians per second
     */
    glm::vec3 getAngularVelocity() const;

    /**
     * @brief Sets the world-space position and refreshes bounds
     * @param position New position
     */
    void setPosition(const glm::vec3& position);

    /**
     * @brief Sets the world-space rotation and refreshes bounds
     * @param rotation New rotation
     */
    void setRotation(const Quaternion& rotation);

    /**
     * @brief Sets the linear velocity
     * @param linear_velocity New linear velocity
     */
    void setLinearVelocity(const glm::vec3& linear_velocity);

    /**
     * @brief Sets the angular velocity
     * @param angular_velocity New angular velocity
     */
    void setAngularVelocity(const glm::vec3& angular_velocity);

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

    /**
     * @brief Integrates linear acceleration into velocity
     * @param delta_seconds Delta time in seconds
     * @param acceleration Linear acceleration in world units per second squared
     * @return Updated linear velocity
     */
    glm::vec3 integrateAcceleration(float delta_seconds, const glm::vec3& acceleration);

    /**
     * @brief Integrates angular acceleration into angular velocity
     * @param delta_seconds Delta time in seconds
     * @param acceleration Angular acceleration in radians per second squared
     * @return Updated angular velocity
     */
    glm::vec3 integrateAngularAcceleration(float delta_seconds, const glm::vec3& acceleration);

    /**
     * @brief Applies an instantaneous linear velocity change
     * @param impulse Velocity delta to apply
     * @return Updated linear velocity
     */
    glm::vec3 applyLinearImpulse(const glm::vec3& impulse);

    /**
     * @brief Applies an instantaneous angular velocity change
     * @param impulse Angular velocity delta to apply
     * @return Updated angular velocity
     */
    glm::vec3 applyAngularImpulse(const glm::vec3& impulse);

    /**
     * @brief Sets the local-space bounds template used to refresh the world AABB
     * @param min_bounds Minimum local-space corner
     * @param max_bounds Maximum local-space corner
     */
    void setLocalBounds(const glm::vec3& min_bounds, const glm::vec3& max_bounds);

    /**
     * @brief Clears the local bounds template and any cached world AABB
     */
    void clearLocalBounds();

    /**
     * @brief Reports whether a valid world AABB is available
     * @return True when bounds have been configured
     */
    bool hasAabb() const;

    /**
     * @brief Returns the cached world-space AABB minimum
     * @return Minimum corner
     */
    glm::vec3 getAabbMin() const;

    /**
     * @brief Returns the cached world-space AABB maximum
     * @return Maximum corner
     */
    glm::vec3 getAabbMax() const;

    /**
     * @brief Returns the collision type id used by the event table
     * @return Collision type id or zero when unset
     */
    std::uint32_t getCollisionTypeId() const;

    /**
     * @brief Sets the collision type id used by the event table
     * @param collision_type_id New type id
     */
    void setCollisionTypeId(std::uint32_t collision_type_id);

    /**
     * @brief Sets or clears the skeletal rig used for skinning
     * @param skeletal_rig Shared rig pointer or nullptr
     */
    void setSkeletalRig(std::shared_ptr<const SkeletalRig> skeletal_rig);

    /**
     * @brief Returns the currently assigned rig
     * @return Shared rig pointer or nullptr
     */
    std::shared_ptr<const SkeletalRig> getSkeletalRig() const;

    /**
     * @brief Reports whether this object currently has skinning data
     * @return True when a rig and skin matrices are available
     */
    bool hasSkinning() const;

    /**
     * @brief Returns the current skinning matrices for rendering
     * @return Skinning matrix array
     */
    const std::vector<glm::mat4>& getSkinMatrices() const;

    /**
     * @brief Sets or clears the active animation clip
     * @param animation_clip Shared clip pointer or nullptr
     */
    void setAnimationClip(std::shared_ptr<const AnimationClip> animation_clip);

    /**
     * @brief Starts playing the current animation clip
     * @param loop Whether the clip should wrap after its last keyframe
     * @param restart Whether playback time should be reset to zero
     * @return True when playback started
     */
    bool playAnimation(bool loop = false, bool restart = true);

    /**
     * @brief Stops animation playback but keeps the current clip assigned
     */
    void stopAnimation();

    /**
     * @brief Reports whether this object is currently playing an animation
     * @return True when playback is active
     */
    bool isAnimationPlaying() const;

    /**
     * @brief Advances the active animation clip and refreshes skin matrices
     * @param delta_seconds Delta time for this frame
     */
    void updateAnimation(float delta_seconds);

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

    glm::vec3 position_;
    Quaternion rotation_;
    std::uint32_t render_element_;
    glm::vec3 linear_velocity_;
    glm::vec3 angular_velocity_;

private:
    /**
     * @brief Recomputes the world-space AABB from the current transform
     */
    void refreshAabb();

    std::array<glm::vec3, 8> local_bounds_corners_;
    glm::vec3 aabb_min_;
    glm::vec3 aabb_max_;
    bool has_local_bounds_;
    bool has_world_aabb_;
    std::uint32_t collision_type_id_;
    std::shared_ptr<const SkeletalRig> skeletal_rig_;
    std::shared_ptr<const AnimationClip> animation_clip_;
    std::vector<glm::mat4> skin_matrices_;
    float animation_time_seconds_;
    bool animation_loop_;
    bool animation_playing_;
};

#endif
