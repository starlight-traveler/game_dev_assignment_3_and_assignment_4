#include "GameObject.h"

#include <cmath>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>

#include "Integration.h"

GameObject::GameObject(std::uint32_t render_element,
                       const glm::vec3& position,
                       const Quaternion& rotation,
                       const glm::vec3& linear_velocity,
                       const glm::vec3& angular_velocity)
    : position_(position),
      rotation_(rotation),
      render_element_(render_element),
      linear_velocity_(linear_velocity),
      angular_velocity_(angular_velocity),
      local_bounds_corners_{},
      local_convex_hull_points_(),
      aabb_min_(0.0f),
      aabb_max_(0.0f),
      has_local_bounds_(false),
      has_world_aabb_(false),
      collision_type_id_(0),
      skeletal_rig_(nullptr),
      animation_clip_(nullptr),
      skin_matrices_(),
      animation_time_seconds_(0.0f),
      animation_loop_(false),
      animation_playing_(false) {}

bool GameObject::issueMoveCommand(const glm::vec3& target_position,
                                  float move_speed,
                                  float arrival_radius) {
    static_cast<void>(target_position);
    static_cast<void>(move_speed);
    static_cast<void>(arrival_radius);
    return false;
}

bool GameObject::stopMoveCommand() {
    return false;
}

bool GameObject::isMoving() const {
    return false;
}

glm::mat4 GameObject::getModel() const {
    const glm::mat4 translation = glm::translate(glm::mat4(1.0f), position_);
    const glm::mat4 rotation = static_cast<glm::mat4>(rotation_);
    return translation * rotation;
}

std::uint32_t GameObject::getRenderElement() const {
    return render_element_;
}

glm::vec3 GameObject::getPosition() const {
    return position_;
}

Quaternion GameObject::getRotation() const {
    return rotation_;
}

glm::vec3 GameObject::getLinearVelocity() const {
    return linear_velocity_;
}

glm::vec3 GameObject::getAngularVelocity() const {
    return angular_velocity_;
}

void GameObject::setPosition(const glm::vec3& position) {
    position_ = position;
    refreshAabb();
}

void GameObject::setRotation(const Quaternion& rotation) {
    rotation_ = rotation;
    rotation_.normalize();
    refreshAabb();
}

void GameObject::setLinearVelocity(const glm::vec3& linear_velocity) {
    linear_velocity_ = linear_velocity;
}

void GameObject::setAngularVelocity(const glm::vec3& angular_velocity) {
    angular_velocity_ = angular_velocity;
}

void GameObject::integrateVelocity(float delta_seconds) {
    setPosition(position_ + integrateLinear(delta_seconds, linear_velocity_));
}

void GameObject::integrateAngularVelocity(float delta_seconds) {
    setRotation(integrateAngular(delta_seconds, angular_velocity_) * rotation_);
}

glm::vec3 GameObject::integrateAcceleration(float delta_seconds, const glm::vec3& acceleration) {
    linear_velocity_ += integrateLinear(delta_seconds, acceleration);
    return linear_velocity_;
}

glm::vec3 GameObject::integrateAngularAcceleration(float delta_seconds,
                                                   const glm::vec3& acceleration) {
    angular_velocity_ += integrateLinear(delta_seconds, acceleration);
    return angular_velocity_;
}

glm::vec3 GameObject::applyLinearImpulse(const glm::vec3& impulse) {
    linear_velocity_ += impulse;
    return linear_velocity_;
}

glm::vec3 GameObject::applyAngularImpulse(const glm::vec3& impulse) {
    angular_velocity_ += impulse;
    return angular_velocity_;
}

void GameObject::setLocalBounds(const glm::vec3& min_bounds, const glm::vec3& max_bounds) {
    const glm::vec3 normalized_min = glm::min(min_bounds, max_bounds);
    const glm::vec3 normalized_max = glm::max(min_bounds, max_bounds);

    local_bounds_corners_[0] = glm::vec3(normalized_min.x, normalized_min.y, normalized_min.z);
    local_bounds_corners_[1] = glm::vec3(normalized_min.x, normalized_min.y, normalized_max.z);
    local_bounds_corners_[2] = glm::vec3(normalized_min.x, normalized_max.y, normalized_min.z);
    local_bounds_corners_[3] = glm::vec3(normalized_min.x, normalized_max.y, normalized_max.z);
    local_bounds_corners_[4] = glm::vec3(normalized_max.x, normalized_min.y, normalized_min.z);
    local_bounds_corners_[5] = glm::vec3(normalized_max.x, normalized_min.y, normalized_max.z);
    local_bounds_corners_[6] = glm::vec3(normalized_max.x, normalized_max.y, normalized_min.z);
    local_bounds_corners_[7] = glm::vec3(normalized_max.x, normalized_max.y, normalized_max.z);
    has_local_bounds_ = true;
    refreshAabb();
}

void GameObject::clearLocalBounds() {
    has_local_bounds_ = false;
    has_world_aabb_ = false;
    aabb_min_ = glm::vec3(0.0f);
    aabb_max_ = glm::vec3(0.0f);
}

void GameObject::setLocalConvexHull(const std::vector<glm::vec3>& points) {
    local_convex_hull_points_ = points;
}

void GameObject::clearLocalConvexHull() {
    local_convex_hull_points_.clear();
}

bool GameObject::hasAabb() const {
    return has_world_aabb_;
}

glm::vec3 GameObject::getAabbMin() const {
    return aabb_min_;
}

glm::vec3 GameObject::getAabbMax() const {
    return aabb_max_;
}

bool GameObject::hasConvexHull() const {
    return !local_convex_hull_points_.empty() || has_local_bounds_;
}

glm::vec3 GameObject::supportPointWorld(const glm::vec3& direction) const {
    const std::vector<glm::vec3>* explicit_points =
        local_convex_hull_points_.empty() ? nullptr : &local_convex_hull_points_;
    if (!explicit_points && !has_local_bounds_) {
        return position_;
    }

    const glm::vec3 local_direction = rotation_.conjugate() * direction;
    glm::vec3 best_local_point = explicit_points ? (*explicit_points)[0] : local_bounds_corners_[0];
    float best_projection = explicit_points
                                ? glm::dot(best_local_point, local_direction)
                                : glm::dot(local_bounds_corners_[0], local_direction);

    if (explicit_points) {
        for (std::size_t i = 1; i < explicit_points->size(); ++i) {
            const float projection = glm::dot((*explicit_points)[i], local_direction);
            if (projection > best_projection) {
                best_projection = projection;
                best_local_point = (*explicit_points)[i];
            }
        }
    } else {
        for (std::size_t i = 1; i < local_bounds_corners_.size(); ++i) {
            const float projection = glm::dot(local_bounds_corners_[i], local_direction);
            if (projection > best_projection) {
                best_projection = projection;
                best_local_point = local_bounds_corners_[i];
            }
        }
    }

    return position_ + (rotation_ * best_local_point);
}

std::uint32_t GameObject::getCollisionTypeId() const {
    return collision_type_id_;
}

void GameObject::setCollisionTypeId(std::uint32_t collision_type_id) {
    collision_type_id_ = collision_type_id;
}

void GameObject::setSkeletalRig(std::shared_ptr<const SkeletalRig> skeletal_rig) {
    skeletal_rig_ = std::move(skeletal_rig);
    if (skeletal_rig_) {
        skin_matrices_ = skeletal_rig_->buildSkinMatrices(std::vector<Quaternion>{});
    } else {
        skin_matrices_.clear();
    }

    if (animation_clip_) {
        updateAnimation(0.0f);
    }
}

std::shared_ptr<const SkeletalRig> GameObject::getSkeletalRig() const {
    return skeletal_rig_;
}

bool GameObject::hasSkinning() const {
    return skeletal_rig_ && !skin_matrices_.empty();
}

const std::vector<glm::mat4>& GameObject::getSkinMatrices() const {
    return skin_matrices_;
}

void GameObject::setAnimationClip(std::shared_ptr<const AnimationClip> animation_clip) {
    animation_clip_ = std::move(animation_clip);
    animation_time_seconds_ = 0.0f;
    animation_playing_ = false;

    if (skeletal_rig_ && animation_clip_) {
        skin_matrices_ = skeletal_rig_->buildSkinMatrices(
            animation_clip_->sampleLocalRotations(0.0f));
    } else if (skeletal_rig_) {
        skin_matrices_ = skeletal_rig_->buildSkinMatrices(std::vector<Quaternion>{});
    } else {
        skin_matrices_.clear();
    }
}

bool GameObject::playAnimation(bool loop, bool restart) {
    if (!skeletal_rig_ || !animation_clip_ || animation_clip_->empty()) {
        return false;
    }

    animation_loop_ = loop;
    animation_playing_ = true;
    if (restart) {
        animation_time_seconds_ = 0.0f;
    }
    updateAnimation(0.0f);
    return true;
}

void GameObject::stopAnimation() {
    animation_playing_ = false;
}

bool GameObject::isAnimationPlaying() const {
    return animation_playing_;
}

void GameObject::updateAnimation(float delta_seconds) {
    if (!skeletal_rig_) {
        skin_matrices_.clear();
        return;
    }

    if (!animation_clip_ || animation_clip_->empty()) {
        skin_matrices_ = skeletal_rig_->buildSkinMatrices(std::vector<Quaternion>{});
        animation_playing_ = false;
        return;
    }

    if (animation_playing_ && delta_seconds > 0.0f) {
        animation_time_seconds_ += delta_seconds;
        const float duration = animation_clip_->durationSeconds();
        if (duration > 0.0f) {
            if (animation_loop_) {
                animation_time_seconds_ =
                    std::fmod(animation_time_seconds_, duration);
            } else if (animation_time_seconds_ >= duration) {
                animation_time_seconds_ = duration;
                animation_playing_ = false;
            }
        } else {
            animation_time_seconds_ = 0.0f;
            animation_playing_ = false;
        }
    }

    skin_matrices_ = skeletal_rig_->buildSkinMatrices(
        animation_clip_->sampleLocalRotations(animation_time_seconds_));
}

void GameObject::refreshAabb() {
    if (!has_local_bounds_) {
        has_world_aabb_ = false;
        return;
    }

    glm::vec3 min_bounds = rotation_ * local_bounds_corners_[0];
    glm::vec3 max_bounds = min_bounds;
    for (std::size_t i = 1; i < local_bounds_corners_.size(); ++i) {
        const glm::vec3 rotated_corner = rotation_ * local_bounds_corners_[i];
        min_bounds = glm::min(min_bounds, rotated_corner);
        max_bounds = glm::max(max_bounds, rotated_corner);
    }

    aabb_min_ = min_bounds + position_;
    aabb_max_ = max_bounds + position_;
    has_world_aabb_ = true;
}
