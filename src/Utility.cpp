#include "Utility.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "GameObject.h"
#include "SceneGraph.h"

#include <glm/gtc/matrix_transform.hpp>

namespace {
constexpr std::uint32_t kRtsCollisionTypeId = 1;
constexpr std::size_t kCollisionBroadPhaseLeafSize = 3;

struct LocalBoundsTemplate {
    glm::vec3 min_bounds;
    glm::vec3 max_bounds;
};

struct CollisionResponseEntry {
    CollisionResponse callback = nullptr;
    std::uint32_t registered_type_a = 0;
    std::uint32_t registered_type_b = 0;
};

struct PendingCollisionEvent {
    std::uint32_t first_render_element = 0;
    std::uint32_t second_render_element = 0;
    CollisionResponse callback = nullptr;
    std::uint32_t registered_type_a = 0;
    std::uint32_t registered_type_b = 0;
};

SceneGraph make_collision_broad_phase() {
    SceneGraph graph{};
    graph.setMaxLeafObjects(kCollisionBroadPhaseLeafSize);
    return graph;
}

std::size_t triangular_entry_count(std::size_t type_capacity) {
    return (type_capacity * (type_capacity + 1)) / 2;
}

std::size_t collision_table_index(std::uint32_t type_a, std::uint32_t type_b) {
    const std::size_t low = static_cast<std::size_t>(std::min(type_a, type_b));
    const std::size_t high = static_cast<std::size_t>(std::max(type_a, type_b));
    return (high * (high + 1)) / 2 + low;
}

// frame delta state stored internally so only engine code can mutate it
std::uint64_t g_delta_time_ms = 0;
float g_delta_seconds = 0.0f;
std::vector<std::unique_ptr<GameObject>> g_game_objects;
std::unordered_map<std::uint32_t, LocalBoundsTemplate> g_local_bounds_by_render_element;
SceneGraph g_collision_broad_phase = make_collision_broad_phase();
std::uint32_t g_next_collision_type_id = kRtsCollisionTypeId + 1;
std::size_t g_collision_response_type_capacity =
    static_cast<std::size_t>(kRtsCollisionTypeId) + 1;
std::vector<CollisionResponseEntry> g_collision_response_table(
    triangular_entry_count(g_collision_response_type_capacity));

GameObject* find_game_object_by_render_element_mutable(std::uint32_t render_element) {
    for (const std::unique_ptr<GameObject>& object : g_game_objects) {
        if (object && object->getRenderElement() == render_element) {
            return object.get();
        }
    }
    return nullptr;
}

const GameObject* find_game_object_by_render_element(std::uint32_t render_element) {
    for (const std::unique_ptr<GameObject>& object : g_game_objects) {
        if (object && object->getRenderElement() == render_element) {
            return object.get();
        }
    }
    return nullptr;
}

void ensure_collision_table_capacity(std::uint32_t type_id) {
    const std::size_t desired_size = static_cast<std::size_t>(type_id) + 1;
    if (g_collision_response_type_capacity >= desired_size) {
        return;
    }

    g_collision_response_table.resize(triangular_entry_count(desired_size));
    g_collision_response_type_capacity = desired_size;
}

bool collision_entry_in_range(std::uint32_t type_a, std::uint32_t type_b) {
    return static_cast<std::size_t>(std::max(type_a, type_b)) <
           g_collision_response_type_capacity;
}

CollisionResponseEntry& collision_response_entry(std::uint32_t type_a,
                                                 std::uint32_t type_b) {
    return g_collision_response_table[collision_table_index(type_a, type_b)];
}

const CollisionResponseEntry& collision_response_entry_const(std::uint32_t type_a,
                                                             std::uint32_t type_b) {
    return g_collision_response_table[collision_table_index(type_a, type_b)];
}

bool aabbs_overlap(const GameObject& first, const GameObject& second) {
    if (!first.hasAabb() || !second.hasAabb()) {
        return false;
    }

    const glm::vec3 first_min = first.getAabbMin();
    const glm::vec3 first_max = first.getAabbMax();
    const glm::vec3 second_min = second.getAabbMin();
    const glm::vec3 second_max = second.getAabbMax();

    return first_min.x <= second_max.x && first_max.x >= second_min.x &&
           first_min.y <= second_max.y && first_max.y >= second_min.y &&
           first_min.z <= second_max.z && first_max.z >= second_min.z;
}

glm::vec3 collision_bounds_center(const GameObject& object) {
    return (object.getAabbMin() + object.getAabbMax()) * 0.5f;
}

float collision_broad_phase_radius(const GameObject& object) {
    const glm::vec3 extent = object.getAabbMax() - object.getAabbMin();
    return 0.5f * std::sqrt(extent.x * extent.x +
                            extent.y * extent.y +
                            extent.z * extent.z);
}

glm::mat4 collision_broad_phase_transform(const GameObject& object) {
    return glm::translate(glm::mat4(1.0f), collision_bounds_center(object));
}

void sync_collision_broad_phase(
    const std::unordered_map<std::uint32_t, GameObject*>& objects_by_render_element) {
    for (const auto& [render_element, object] : objects_by_render_element) {
        if (!object) {
            continue;
        }

        if (!object->hasAabb()) {
            g_collision_broad_phase.removeNodeByObject(render_element);
            continue;
        }

        g_collision_broad_phase.createNode(
            g_collision_broad_phase.rootNodeId(),
            render_element,
            collision_broad_phase_transform(*object),
            collision_broad_phase_radius(*object));
    }

    g_collision_broad_phase.updateWorldTransforms();
    g_collision_broad_phase.rebuildSpatialIndex();
}
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

    const std::uint32_t render_element = object->getRenderElement();
    const auto bounds_it =
        g_local_bounds_by_render_element.find(render_element);
    if (bounds_it != g_local_bounds_by_render_element.end()) {
        object->setLocalBounds(bounds_it->second.min_bounds, bounds_it->second.max_bounds);
    }
    if (object->hasAabb()) {
        g_collision_broad_phase.createNode(
            g_collision_broad_phase.rootNodeId(),
            render_element,
            collision_broad_phase_transform(*object),
            collision_broad_phase_radius(*object));
        g_collision_broad_phase.updateWorldTransforms();
        g_collision_broad_phase.rebuildSpatialIndex();
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
    g_collision_broad_phase.removeNodeByObject(render_element);
    return g_game_objects.size() != previous_size;
}

void updateGameObjects(float delta_seconds) {
    std::vector<PendingCollisionEvent> pending_events;
    std::unordered_map<std::uint32_t, GameObject*> objects_by_render_element;
    objects_by_render_element.reserve(g_game_objects.size());
    for (const std::unique_ptr<GameObject>& object : g_game_objects) {
        if (!object) {
            continue;
        }
        object->update(delta_seconds);
        object->updateAnimation(delta_seconds);
        objects_by_render_element[object->getRenderElement()] = object.get();
    }

    sync_collision_broad_phase(objects_by_render_element);

    std::vector<std::uint32_t> candidate_render_elements;
    for (const std::unique_ptr<GameObject>& first_object_ptr : g_game_objects) {
        GameObject* first_object = first_object_ptr.get();
        if (!first_object || !first_object->hasAabb()) {
            continue;
        }

        const std::uint32_t first_type = first_object->getCollisionTypeId();
        if (first_type == 0) {
            continue;
        }

        const std::uint32_t first_render_element = first_object->getRenderElement();
        g_collision_broad_phase.queryAabb(
            candidate_render_elements,
            glm::vec2(first_object->getAabbMin().x, first_object->getAabbMin().z),
            glm::vec2(first_object->getAabbMax().x, first_object->getAabbMax().z));

        for (const std::uint32_t second_render_element : candidate_render_elements) {
            if (second_render_element <= first_render_element) {
                continue;
            }

            const auto second_object_it =
                objects_by_render_element.find(second_render_element);
            if (second_object_it == objects_by_render_element.end()) {
                continue;
            }

            GameObject* second_object = second_object_it->second;
            if (!second_object || !second_object->hasAabb()) {
                continue;
            }

            const std::uint32_t second_type = second_object->getCollisionTypeId();
            if (second_type == 0 || !collision_entry_in_range(first_type, second_type)) {
                continue;
            }

            const CollisionResponseEntry& response_entry =
                collision_response_entry_const(first_type, second_type);
            if (!response_entry.callback || !aabbs_overlap(*first_object, *second_object)) {
                continue;
            }

            pending_events.push_back(PendingCollisionEvent{
                first_render_element,
                second_render_element,
                response_entry.callback,
                response_entry.registered_type_a,
                response_entry.registered_type_b});
        }
    }

    for (const PendingCollisionEvent& event : pending_events) {
        GameObject* first_object =
            find_game_object_by_render_element_mutable(event.first_render_element);
        GameObject* second_object =
            find_game_object_by_render_element_mutable(event.second_render_element);
        if (!first_object || !second_object || !event.callback) {
            continue;
        }

        const std::uint32_t first_type = first_object->getCollisionTypeId();
        const std::uint32_t second_type = second_object->getCollisionTypeId();
        if (first_type == event.registered_type_a &&
            second_type == event.registered_type_b) {
            event.callback(*first_object, *second_object);
        } else if (first_type == event.registered_type_b &&
                   second_type == event.registered_type_a) {
            event.callback(*second_object, *first_object);
        }
    }
}

void clearGameObjects() {
    g_game_objects.clear();
    g_collision_broad_phase = make_collision_broad_phase();
}

const GameObject* findGameObjectByRenderElement(std::uint32_t render_element) {
    return find_game_object_by_render_element(render_element);
}

GameObject* findMutableGameObjectByRenderElement(std::uint32_t render_element) {
    return find_game_object_by_render_element_mutable(render_element);
}

bool setLocalBoundsByRenderElement(std::uint32_t render_element,
                                   const glm::vec3& min_bounds,
                                   const glm::vec3& max_bounds) {
    if (render_element == 0) {
        return false;
    }

    g_local_bounds_by_render_element[render_element] = LocalBoundsTemplate{min_bounds, max_bounds};
    GameObject* object = findMutableGameObjectByRenderElement(render_element);
    if (object) {
        object->setLocalBounds(min_bounds, max_bounds);
        if (object->hasAabb()) {
            g_collision_broad_phase.createNode(
                g_collision_broad_phase.rootNodeId(),
                render_element,
                collision_broad_phase_transform(*object),
                collision_broad_phase_radius(*object));
            g_collision_broad_phase.updateWorldTransforms();
            g_collision_broad_phase.rebuildSpatialIndex();
        }
    }
    return true;
}

bool clearLocalBoundsByRenderElement(std::uint32_t render_element) {
    if (render_element == 0) {
        return false;
    }

    const std::size_t erased = g_local_bounds_by_render_element.erase(render_element);
    GameObject* object = findMutableGameObjectByRenderElement(render_element);
    if (object) {
        object->clearLocalBounds();
    }
    g_collision_broad_phase.removeNodeByObject(render_element);
    return erased > 0 || object != nullptr;
}

std::uint32_t rtsCollisionType() {
    return kRtsCollisionTypeId;
}

std::uint32_t registerCollisionType() {
    const std::uint32_t type_id = g_next_collision_type_id++;
    ensure_collision_table_capacity(type_id);
    return type_id;
}

bool registerCollisionResponse(std::uint32_t type_a,
                               std::uint32_t type_b,
                               CollisionResponse response) {
    if (type_a == 0 || type_b == 0 || !response) {
        return false;
    }

    ensure_collision_table_capacity(std::max(type_a, type_b));
    collision_response_entry(type_a, type_b) =
        CollisionResponseEntry{response, type_a, type_b};
    return true;
}

void clearCollisionResponses() {
    std::fill(g_collision_response_table.begin(),
              g_collision_response_table.end(),
              CollisionResponseEntry{});
}
}  // namespace utility
