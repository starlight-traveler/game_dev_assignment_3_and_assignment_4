#include "Utility.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "GameObject.h"

namespace {
// frame delta state stored internally so only engine code can mutate it
std::uint64_t g_delta_time_ms = 0;
float g_delta_seconds = 0.0f;
std::vector<std::unique_ptr<GameObject>> g_game_objects;
}  // namespace

namespace utility {
void setFrameDelta(std::uint64_t delta_time_ms, float delta_seconds) {
    g_delta_time_ms = delta_time_ms;
    g_delta_seconds = delta_seconds;
}

std::uint64_t deltaTimeMs() {
    return g_delta_time_ms;
}

float deltaSeconds() {
    return g_delta_seconds;
}

std::size_t addGameObject(std::unique_ptr<GameObject> object) {
    if (!object) {
        return g_game_objects.size();
    }
    g_game_objects.push_back(std::move(object));
    return g_game_objects.size();
}

bool removeGameObjectByRenderElement(std::uint32_t render_element) {
    const auto previous_size = g_game_objects.size();
    g_game_objects.erase(
        std::remove_if(g_game_objects.begin(), g_game_objects.end(),
                       [render_element](const std::unique_ptr<GameObject>& object) {
                           return object && object->getRenderElement() == render_element;
                       }),
        g_game_objects.end());
    return g_game_objects.size() != previous_size;
}

void updateGameObjects(float delta_seconds) {
    for (const std::unique_ptr<GameObject>& object : g_game_objects) {
        if (!object) {
            continue;
        }
        object->update(delta_seconds);
    }
}

void clearGameObjects() {
    g_game_objects.clear();
}

const GameObject* findGameObjectByRenderElement(std::uint32_t render_element) {
    for (const std::unique_ptr<GameObject>& object : g_game_objects) {
        if (object && object->getRenderElement() == render_element) {
            return object.get();
        }
    }
    return nullptr;
}

GameObject* findMutableGameObjectByRenderElement(std::uint32_t render_element) {
    for (const std::unique_ptr<GameObject>& object : g_game_objects) {
        if (object && object->getRenderElement() == render_element) {
            return object.get();
        }
    }
    return nullptr;
}
}  // namespace utility
