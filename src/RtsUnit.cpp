#include "RtsUnit.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kMinMoveSpeed = 0.0001f;
constexpr float kMinArrivalRadius = 0.001f;
constexpr float kMinDirectionLength = 0.0001f;
}  // namespace

RtsUnit::RtsUnit(std::uint32_t render_element,
                 const glm::vec3& position,
                 const glm::vec3& linear_velocity,
                 const glm::vec3& angular_velocity)
    : GameObject(render_element,
                 position,
                 Quaternion(glm::vec3(0.0f, 1.0f, 0.0f), 0.0f),
                 linear_velocity,
                 angular_velocity),
      has_move_target_(false),
      // default the stored target to the spawn point so the class starts in a stable state
      move_target_(position),
      move_speed_(0.0f),
      arrival_radius_(0.05f) {}

void RtsUnit::update(float delta_seconds) {
    if (has_move_target_) {
        // command driven mode pushes the unit toward the target each frame
        const glm::vec3 to_target = move_target_ - position_;
        const float distance_to_target = glm::length(to_target);
        if (distance_to_target <= arrival_radius_) {
            // if the target is already within the snap radius finish immediately
            // snapping avoids tiny jitter from taking many nearly zero sized steps at the end
            position_ = move_target_;
            linear_velocity_ = glm::vec3(0.0f);
            has_move_target_ = false;
            return;
        }

        if (delta_seconds <= 0.0f) {
            return;
        }

        const glm::vec3 direction = to_target / distance_to_target;
        // keep the rendered facing aligned with movement
        faceDirection(direction);

        const float max_step = move_speed_ * delta_seconds;
        if (max_step >= distance_to_target) {
            // if this frame could overshoot clamp to the exact target instead
            position_ = move_target_;
            linear_velocity_ = glm::vec3(0.0f);
            has_move_target_ = false;
            return;
        }

        // otherwise advance by one speed limited step and let the next frame continue
        linear_velocity_ = direction * move_speed_;
        integrateVelocity(delta_seconds);
        return;
    }

    // if no move command is active fall back to plain GameObject velocity integration
    integrateVelocity(delta_seconds);
    integrateAngularVelocity(delta_seconds);
}

bool RtsUnit::issueMoveCommand(const glm::vec3& target_position,
                               float move_speed,
                               float arrival_radius) {
    if (move_speed < kMinMoveSpeed) {
        return false;
    }

    // store the new command parameters first so update can continue from them next frame
    move_target_ = target_position;
    move_speed_ = move_speed;
    arrival_radius_ = std::max(arrival_radius, kMinArrivalRadius);
    // move commands take over orientation so angular velocity is cleared
    angular_velocity_ = glm::vec3(0.0f);

    const glm::vec3 to_target = move_target_ - position_;
    const float distance_to_target = glm::length(to_target);
    if (distance_to_target <= arrival_radius_) {
        // commands issued very near the target resolve instantly
        // that keeps click spam near the current position from creating fake movement frames
        position_ = move_target_;
        linear_velocity_ = glm::vec3(0.0f);
        has_move_target_ = false;
        return true;
    }

    has_move_target_ = true;
    // orient right away so the unit visibly turns even before the next update tick
    faceDirection(to_target / distance_to_target);
    return true;
}

bool RtsUnit::stopMoveCommand() {
    // stopping clears both linear and angular motion so the unit fully settles
    // unlike the full rts world there is no order queue here so stop just drops the one active command
    has_move_target_ = false;
    linear_velocity_ = glm::vec3(0.0f);
    angular_velocity_ = glm::vec3(0.0f);
    return true;
}

bool RtsUnit::isMoving() const {
    // for this class moving simply means a target is active
    return has_move_target_;
}

void RtsUnit::faceDirection(const glm::vec3& direction) {
    const glm::vec2 direction_xz(direction.x, direction.z);
    if (glm::length(direction_xz) < kMinDirectionLength) {
        return;
    }

    // local plus x is treated as the forward axis for this unit mesh
    const float yaw_radians = std::atan2(-direction.z, direction.x);
    rotation_ = Quaternion(glm::vec3(0.0f, 1.0f, 0.0f), yaw_radians);
}
