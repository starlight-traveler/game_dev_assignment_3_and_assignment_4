#include "Engine.h"

#include <memory>
#include <utility>

#include "GameObject.h"
#include "RtsUnit.h"
#include "Utility.h"

std::uint64_t getDeltaTime() {
    return utility::deltaTimeMs();
}

float getDeltaSeconds() {
    return utility::deltaSeconds();
}

bool integrateLinear(std::uint32_t render_element, float delta_time) {
    GameObject* object = utility::findMutableGameObjectByRenderElement(render_element);
    if (!object) {
        return false;
    }

    const float resolved_delta = delta_time >= 0.0f ? delta_time : getDeltaSeconds();
    object->integrateVelocity(resolved_delta);
    return true;
}

bool integrateAngular(std::uint32_t render_element, float delta_time) {
    GameObject* object = utility::findMutableGameObjectByRenderElement(render_element);
    if (!object) {
        return false;
    }

    const float resolved_delta = delta_time >= 0.0f ? delta_time : getDeltaSeconds();
    object->integrateAngularVelocity(resolved_delta);
    return true;
}

bool integrateAcceleration(std::uint32_t render_element,
                           float delta_time,
                           const glm::vec3& acceleration) {
    GameObject* object = utility::findMutableGameObjectByRenderElement(render_element);
    if (!object) {
        return false;
    }

    object->integrateAcceleration(delta_time, acceleration);
    return true;
}

bool integrateAngularAcceleration(std::uint32_t render_element,
                                  float delta_time,
                                  const glm::vec3& acceleration) {
    GameObject* object = utility::findMutableGameObjectByRenderElement(render_element);
    if (!object) {
        return false;
    }

    object->integrateAngularAcceleration(delta_time, acceleration);
    return true;
}

bool applyLinearImpulse(std::uint32_t render_element, const glm::vec3& impulse) {
    GameObject* object = utility::findMutableGameObjectByRenderElement(render_element);
    if (!object) {
        return false;
    }

    object->applyLinearImpulse(impulse);
    return true;
}

bool applyAngularImpulse(std::uint32_t render_element, const glm::vec3& impulse) {
    GameObject* object = utility::findMutableGameObjectByRenderElement(render_element);
    if (!object) {
        return false;
    }

    object->applyAngularImpulse(impulse);
    return true;
}

bool spawnRtsGameObject(std::uint32_t render_element,
                        const glm::vec3& position,
                        const glm::vec3& linear_velocity,
                        const glm::vec3& angular_velocity) {
    auto object = std::make_unique<RtsUnit>(render_element, position, linear_velocity, angular_velocity);
    object->setCollisionTypeId(utility::rtsCollisionType());
    return utility::addGameObject(std::move(object)) > 0;
}

bool issueMoveCommand(std::uint32_t render_element,
                      const glm::vec3& target_position,
                      float move_speed,
                      float arrival_radius) {
    GameObject* object = utility::findMutableGameObjectByRenderElement(render_element);
    if (!object) {
        return false;
    }
    return object->issueMoveCommand(target_position, move_speed, arrival_radius);
}

bool stopRtsGameObject(std::uint32_t render_element) {
    GameObject* object = utility::findMutableGameObjectByRenderElement(render_element);
    if (!object) {
        return false;
    }
    return object->stopMoveCommand();
}

bool destroyGameObject(std::uint32_t render_element) {
    return utility::removeGameObjectByRenderElement(render_element);
}

void updateActiveGameObjects() {
    utility::updateGameObjects(getDeltaSeconds());
}

void clearActiveGameObjects() {
    utility::clearGameObjects();
}

glm::mat4 getModelForRenderElement(std::uint32_t render_element) {
    const GameObject* object = utility::findGameObjectByRenderElement(render_element);
    if (!object) {
        return glm::mat4(1.0f);
    }
    return object->getModel();
}

glm::vec3 getPositionForRenderElement(std::uint32_t render_element) {
    const GameObject* object = utility::findGameObjectByRenderElement(render_element);
    if (!object) {
        return glm::vec3(0.0f);
    }
    return object->getPosition();
}

glm::vec3 getLinearVelocityForRenderElement(std::uint32_t render_element) {
    const GameObject* object = utility::findGameObjectByRenderElement(render_element);
    if (!object) {
        return glm::vec3(0.0f);
    }
    return object->getLinearVelocity();
}

glm::vec3 getAngularVelocityForRenderElement(std::uint32_t render_element) {
    const GameObject* object = utility::findGameObjectByRenderElement(render_element);
    if (!object) {
        return glm::vec3(0.0f);
    }
    return object->getAngularVelocity();
}

bool isRtsGameObjectMoving(std::uint32_t render_element) {
    const GameObject* object = utility::findGameObjectByRenderElement(render_element);
    if (!object) {
        return false;
    }
    return object->isMoving();
}

bool setLocalBoundsForRenderElement(std::uint32_t render_element,
                                    const glm::vec3& min_bounds,
                                    const glm::vec3& max_bounds) {
    return utility::setLocalBoundsByRenderElement(render_element, min_bounds, max_bounds);
}

bool clearLocalBoundsForRenderElement(std::uint32_t render_element) {
    return utility::clearLocalBoundsByRenderElement(render_element);
}

bool hasAabbForRenderElement(std::uint32_t render_element) {
    const GameObject* object = utility::findGameObjectByRenderElement(render_element);
    return object ? object->hasAabb() : false;
}

glm::vec3 getAabbMinForRenderElement(std::uint32_t render_element) {
    const GameObject* object = utility::findGameObjectByRenderElement(render_element);
    if (!object || !object->hasAabb()) {
        return glm::vec3(0.0f);
    }
    return object->getAabbMin();
}

glm::vec3 getAabbMaxForRenderElement(std::uint32_t render_element) {
    const GameObject* object = utility::findGameObjectByRenderElement(render_element);
    if (!object || !object->hasAabb()) {
        return glm::vec3(0.0f);
    }
    return object->getAabbMax();
}

std::uint32_t getRtsGameObjectCollisionType() {
    return utility::rtsCollisionType();
}

std::uint32_t registerCollisionType() {
    return utility::registerCollisionType();
}

bool setCollisionTypeForRenderElement(std::uint32_t render_element,
                                      std::uint32_t collision_type_id) {
    GameObject* object = utility::findMutableGameObjectByRenderElement(render_element);
    if (!object || collision_type_id == 0) {
        return false;
    }

    object->setCollisionTypeId(collision_type_id);
    return true;
}

bool registerCollisionResponse(std::uint32_t type_a,
                               std::uint32_t type_b,
                               CollisionResponse response) {
    return utility::registerCollisionResponse(type_a, type_b, response);
}

void clearCollisionResponses() {
    utility::clearCollisionResponses();
}

bool setSkeletalRigForRenderElement(std::uint32_t render_element,
                                    std::shared_ptr<const SkeletalRig> skeletal_rig) {
    GameObject* object = utility::findMutableGameObjectByRenderElement(render_element);
    if (!object) {
        return false;
    }

    object->setSkeletalRig(std::move(skeletal_rig));
    return true;
}

bool setAnimationClipForRenderElement(
    std::uint32_t render_element,
    std::shared_ptr<const AnimationClip> animation_clip) {
    GameObject* object = utility::findMutableGameObjectByRenderElement(render_element);
    if (!object) {
        return false;
    }

    object->setAnimationClip(std::move(animation_clip));
    return true;
}

bool playAnimationForRenderElement(std::uint32_t render_element,
                                   bool loop,
                                   bool restart) {
    GameObject* object = utility::findMutableGameObjectByRenderElement(render_element);
    if (!object) {
        return false;
    }

    return object->playAnimation(loop, restart);
}

bool stopAnimationForRenderElement(std::uint32_t render_element) {
    GameObject* object = utility::findMutableGameObjectByRenderElement(render_element);
    if (!object) {
        return false;
    }

    object->stopAnimation();
    return true;
}

bool isAnimationPlayingForRenderElement(std::uint32_t render_element) {
    const GameObject* object = utility::findGameObjectByRenderElement(render_element);
    if (!object) {
        return false;
    }

    return object->isAnimationPlaying();
}

std::vector<glm::mat4> getSkinMatricesForRenderElement(std::uint32_t render_element) {
    const GameObject* object = utility::findGameObjectByRenderElement(render_element);
    if (!object) {
        return {};
    }

    return object->getSkinMatrices();
}
