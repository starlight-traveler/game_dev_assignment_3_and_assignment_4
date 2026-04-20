#include "Utility.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ConvexCollision.h"
#include "GameObject.h"
#include "SceneGraph.h"

#include <glm/gtc/matrix_transform.hpp>

namespace {
// reserve collision type one for the built in rts unit logic
constexpr std::uint32_t kRtsCollisionTypeId = 1;

// smaller leaf sizes keep broad phase queries tighter for this project scale
constexpr std::size_t kCollisionBroadPhaseLeafSize = 3;

// some render items exist before a gameplay object is created
// this template lets us remember mesh bounds and apply them later
struct LocalBoundsTemplate {
    glm::vec3 min_bounds;
    glm::vec3 max_bounds;
};

struct LocalConvexHullTemplate {
    std::vector<glm::vec3> points;
};

// one table entry stores the callback plus the exact registration order
// the order matters because the callback may treat the two parameters differently
struct CollisionResponseEntry {
    CollisionResponse callback = nullptr;
    std::uint32_t registered_type_a = 0;
    std::uint32_t registered_type_b = 0;
};

// collision work is staged into pending events first
// this avoids mutating objects while the broad phase iteration is still scanning pairs
struct PendingCollisionEvent {
    std::uint32_t first_render_element = 0;
    std::uint32_t second_render_element = 0;
    CollisionResponse callback = nullptr;
    std::uint32_t registered_type_a = 0;
    std::uint32_t registered_type_b = 0;
};

// the broad phase is a scene graph configured as a spatial index
// each gameplay object contributes one sphere like proxy built from its aabb
SceneGraph make_collision_broad_phase() {
    SceneGraph graph{};
    graph.setMaxLeafObjects(kCollisionBroadPhaseLeafSize);
    return graph;
}

// the collision table is stored in triangular form because type a with type b
// is the same pairing as type b with type a for lookup purposes
std::size_t triangular_entry_count(std::size_t type_capacity) {
    return (type_capacity * (type_capacity + 1)) / 2;
}

std::size_t collision_table_index(std::uint32_t type_a, std::uint32_t type_b) {
    const std::size_t low = static_cast<std::size_t>(std::min(type_a, type_b));
    const std::size_t high = static_cast<std::size_t>(std::max(type_a, type_b));
    return (high * (high + 1)) / 2 + low;
}

// all of this state lives in the utility translation unit
// other files interact through the namespace functions instead of touching globals directly
std::uint64_t g_delta_time_ms = 0;
float g_delta_seconds = 0.0f;
std::vector<std::unique_ptr<GameObject>> g_game_objects;
std::unordered_map<std::uint32_t, LocalBoundsTemplate> g_local_bounds_by_render_element;
std::unordered_map<std::uint32_t, LocalConvexHullTemplate> g_local_convex_hulls_by_render_element;
SceneGraph g_collision_broad_phase = make_collision_broad_phase();
std::uint32_t g_next_collision_type_id = kRtsCollisionTypeId + 1;
std::size_t g_collision_response_type_capacity =
    static_cast<std::size_t>(kRtsCollisionTypeId) + 1;
std::vector<CollisionResponseEntry> g_collision_response_table(
    triangular_entry_count(g_collision_response_type_capacity));

GameObject* find_game_object_by_render_element_mutable(std::uint32_t render_element) {
    // linear search is fine here because the active object count in this project is modest
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

    // if the table already covers this type id there is nothing to do
    if (g_collision_response_type_capacity >= desired_size) {
        return;
    }

    // resizing with value initialized entries preserves old callbacks and creates empty new slots
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
    // if either object has no valid aabb there is nothing to test
    if (!first.hasAabb() || !second.hasAabb()) {
        return false;
    }

    // this is the standard axis aligned overlap check
    // every axis must overlap for the full boxes to overlap
    const glm::vec3 first_min = first.getAabbMin();
    const glm::vec3 first_max = first.getAabbMax();
    const glm::vec3 second_min = second.getAabbMin();
    const glm::vec3 second_max = second.getAabbMax();

    return first_min.x <= second_max.x && first_max.x >= second_min.x &&
           first_min.y <= second_max.y && first_max.y >= second_min.y &&
           first_min.z <= second_max.z && first_max.z >= second_min.z;
}

ConvexCollisionQueryResult query_convex_collision(const GameObject& first,
                                                  const GameObject& second) {
    ConvexCollisionQueryResult result{};
    if (!first.hasConvexHull() || !second.hasConvexHull()) {
        return result;
    }

    result = convex_collision::query(
        [&first](const glm::vec3& direction) {
            return first.supportPointWorld(direction);
        },
        [&second](const glm::vec3& direction) {
            return second.supportPointWorld(direction);
        },
        second.getPosition() - first.getPosition());
    return result;
}

glm::vec3 collision_bounds_center(const GameObject& object) {
    // center is used as the spatial graph node position
    return (object.getAabbMin() + object.getAabbMax()) * 0.5f;
}

float collision_broad_phase_radius(const GameObject& object) {
    // the broad phase stores a coarse radius that encloses the aabb
    // half of the box diagonal gives that enclosing sphere radius
    const glm::vec3 extent = object.getAabbMax() - object.getAabbMin();
    return 0.5f * std::sqrt(extent.x * extent.x +
                            extent.y * extent.y +
                            extent.z * extent.z);
}

glm::mat4 collision_broad_phase_transform(const GameObject& object) {
    // only translation matters for this proxy node
    return glm::translate(glm::mat4(1.0f), collision_bounds_center(object));
}

void sync_collision_broad_phase(
    const std::unordered_map<std::uint32_t, GameObject*>& objects_by_render_element) {
    // rebuild or remove broad phase entries so the spatial graph matches the latest aabbs
    for (const auto& [render_element, object] : objects_by_render_element) {
        if (!object) {
            continue;
        }

        // objects without bounds should not appear in collision queries
        if (!object->hasAabb()) {
            g_collision_broad_phase.removeNodeByObject(render_element);
            continue;
        }

        // create or refresh the proxy node for this object using its current bounds
        g_collision_broad_phase.createNode(
            g_collision_broad_phase.rootNodeId(),
            render_element,
            collision_broad_phase_transform(*object),
            collision_broad_phase_radius(*object));
    }

    // once every node is in place update transforms and rebuild the spatial partitions
    g_collision_broad_phase.updateWorldTransforms();
    g_collision_broad_phase.rebuildSpatialIndex();
}
}  // namespace

namespace utility {
void setFrameDelta(std::uint64_t delta_time_ms, float delta_seconds) {
    // store both forms because some older engine code wants milliseconds and newer code wants seconds
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
    // ignore null ownership transfers and just report the current size
    if (!object) {
        return g_game_objects.size();
    }

    const std::uint32_t render_element = object->getRenderElement();

    // if mesh bounds were recorded earlier copy them onto the new object now
    const auto bounds_it =
        g_local_bounds_by_render_element.find(render_element);
    if (bounds_it != g_local_bounds_by_render_element.end()) {
        object->setLocalBounds(bounds_it->second.min_bounds, bounds_it->second.max_bounds);
    }
    const auto convex_hull_it =
        g_local_convex_hulls_by_render_element.find(render_element);
    if (convex_hull_it != g_local_convex_hulls_by_render_element.end()) {
        object->setLocalConvexHull(convex_hull_it->second.points);
    }

    // objects with valid aabbs join the broad phase immediately
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
    // erase the owned gameplay object first
    const auto previous_size = g_game_objects.size();
    g_game_objects.erase(
        std::remove_if(g_game_objects.begin(), g_game_objects.end(),
                       [render_element](const std::unique_ptr<GameObject>& object) {
                           return object && object->getRenderElement() == render_element;
                       }),
        g_game_objects.end());

    // then drop any broad phase node that pointed at it
    g_collision_broad_phase.removeNodeByObject(render_element);
    return g_game_objects.size() != previous_size;
}

void updateGameObjects(float delta_seconds) {
    // gather collision work in a separate list so callbacks run after the object update loop
    std::vector<PendingCollisionEvent> pending_events;

    // this map makes it cheap to recover objects from broad phase query results later
    std::unordered_map<std::uint32_t, GameObject*> objects_by_render_element;
    objects_by_render_element.reserve(g_game_objects.size());
    for (const std::unique_ptr<GameObject>& object : g_game_objects) {
        if (!object) {
            continue;
        }

        // first let gameplay move the object and then advance any animation controller state
        object->update(delta_seconds);
        object->updateAnimation(delta_seconds);
        objects_by_render_element[object->getRenderElement()] = object.get();
    }

    // after motion and animation the aabbs may have changed so refresh the broad phase
    sync_collision_broad_phase(objects_by_render_element);

    // this buffer is reused for each broad phase query result set
    std::vector<std::uint32_t> candidate_render_elements;
    for (const std::unique_ptr<GameObject>& first_object_ptr : g_game_objects) {
        GameObject* first_object = first_object_ptr.get();
        if (!first_object || !first_object->hasAabb()) {
            continue;
        }

        // collision type zero means this object does not participate in callback routing
        const std::uint32_t first_type = first_object->getCollisionTypeId();
        if (first_type == 0) {
            continue;
        }

        const std::uint32_t first_render_element = first_object->getRenderElement();

        // query by the objects xz footprint because the scene graph partition works in the ground plane
        g_collision_broad_phase.queryAabb(
            candidate_render_elements,
            glm::vec2(first_object->getAabbMin().x, first_object->getAabbMin().z),
            glm::vec2(first_object->getAabbMax().x, first_object->getAabbMax().z));

        for (const std::uint32_t second_render_element : candidate_render_elements) {
            // skip self pairs and duplicate pair orderings
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

            // if the pair has no registered response there is no reason to do the narrow check
            const std::uint32_t second_type = second_object->getCollisionTypeId();
            if (second_type == 0 || !collision_entry_in_range(first_type, second_type)) {
                continue;
            }

            const CollisionResponseEntry& response_entry =
                collision_response_entry_const(first_type, second_type);

            // broad phase candidates still need an exact aabb overlap test before the narrow phase
            if (!response_entry.callback || !aabbs_overlap(*first_object, *second_object)) {
                continue;
            }

            const ConvexCollisionQueryResult narrow_phase =
                query_convex_collision(*first_object, *second_object);
            if (!narrow_phase.has_convex_data || !narrow_phase.intersecting) {
                continue;
            }

            // store the pair now and invoke the callback later after all pair discovery is done
            pending_events.push_back(PendingCollisionEvent{
                first_render_element,
                second_render_element,
                response_entry.callback,
                response_entry.registered_type_a,
                response_entry.registered_type_b});
        }
    }

    // now replay the pending events against the current live objects
    // this second lookup step is safer because callbacks may have removed earlier objects
    for (const PendingCollisionEvent& event : pending_events) {
        GameObject* first_object =
            find_game_object_by_render_element_mutable(event.first_render_element);
        GameObject* second_object =
            find_game_object_by_render_element_mutable(event.second_render_element);
        if (!first_object || !second_object || !event.callback) {
            continue;
        }

        // preserve the original registration order when calling the callback
        // if the live pair order is reversed then swap the arguments
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
    // clearing objects also resets the broad phase so stale nodes do not survive
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
    // zero is treated as an invalid render element id across the engine
    if (render_element == 0) {
        return false;
    }

    // remember the template even if the gameplay object does not exist yet
    g_local_bounds_by_render_element[render_element] = LocalBoundsTemplate{min_bounds, max_bounds};
    GameObject* object = findMutableGameObjectByRenderElement(render_element);
    if (object) {
        // if the object already exists update its bounds immediately
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

    // remove the stored template and clear any active object copy of those bounds
    const std::size_t erased = g_local_bounds_by_render_element.erase(render_element);
    GameObject* object = findMutableGameObjectByRenderElement(render_element);
    if (object) {
        object->clearLocalBounds();
    }

    // once bounds are gone this object should not remain in the broad phase
    g_collision_broad_phase.removeNodeByObject(render_element);
    return erased > 0 || object != nullptr;
}

bool setLocalConvexHullByRenderElement(std::uint32_t render_element,
                                       const std::vector<glm::vec3>& points) {
    if (render_element == 0 || points.empty()) {
        return false;
    }

    g_local_convex_hulls_by_render_element[render_element] = LocalConvexHullTemplate{points};
    GameObject* object = findMutableGameObjectByRenderElement(render_element);
    if (object) {
        object->setLocalConvexHull(points);
    }
    return true;
}

bool clearLocalConvexHullByRenderElement(std::uint32_t render_element) {
    if (render_element == 0) {
        return false;
    }

    const std::size_t erased = g_local_convex_hulls_by_render_element.erase(render_element);
    GameObject* object = findMutableGameObjectByRenderElement(render_element);
    if (object) {
        object->clearLocalConvexHull();
    }
    return erased > 0 || object != nullptr;
}

ConvexCollisionQueryResult queryConvexCollisionByRenderElement(std::uint32_t first_render_element,
                                                               std::uint32_t second_render_element) {
    const GameObject* first = findGameObjectByRenderElement(first_render_element);
    const GameObject* second = findGameObjectByRenderElement(second_render_element);
    if (!first || !second) {
        return ConvexCollisionQueryResult{};
    }
    return query_convex_collision(*first, *second);
}

std::uint32_t rtsCollisionType() {
    return kRtsCollisionTypeId;
}

std::uint32_t registerCollisionType() {
    // type ids grow monotonically and the response table expands on demand
    const std::uint32_t type_id = g_next_collision_type_id++;
    ensure_collision_table_capacity(type_id);
    return type_id;
}

bool registerCollisionResponse(std::uint32_t type_a,
                               std::uint32_t type_b,
                               CollisionResponse response) {
    // invalid ids or a missing callback are rejected up front
    if (type_a == 0 || type_b == 0 || !response) {
        return false;
    }

    // make sure the triangular table is large enough for this pair and then store it
    ensure_collision_table_capacity(std::max(type_a, type_b));
    collision_response_entry(type_a, type_b) =
        CollisionResponseEntry{response, type_a, type_b};
    return true;
}

void clearCollisionResponses() {
    // resetting to default entries drops every registered callback at once
    std::fill(g_collision_response_table.begin(),
              g_collision_response_table.end(),
              CollisionResponseEntry{});
}
}  // namespace utility
