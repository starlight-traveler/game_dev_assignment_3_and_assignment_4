#include "Engine.h"

#include <memory>

#include "GameObject.h"
#include "RtsUnit.h"
#include "Utility.h"

std::uint64_t getDeltaTime() {
    return utility::deltaTimeMs();
}

float getDeltaSeconds() {
    return utility::deltaSeconds();
}

bool spawnRtsGameObject(std::uint32_t render_element,
                        const glm::vec3& position,
                        const glm::vec3& linear_velocity,
                        const glm::vec3& angular_velocity) {
    auto object = std::make_unique<RtsUnit>(render_element, position, linear_velocity, angular_velocity);
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

bool isRtsGameObjectMoving(std::uint32_t render_element) {
    const GameObject* object = utility::findGameObjectByRenderElement(render_element);
    if (!object) {
        return false;
    }
    return object->isMoving();
}
