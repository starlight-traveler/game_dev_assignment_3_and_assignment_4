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
