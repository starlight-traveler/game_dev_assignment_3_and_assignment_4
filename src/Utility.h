#ifndef UTILITY_H
#define UTILITY_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "CollisionResponse.h"
#include "ConvexCollision.h"

class GameObject;

namespace utility {
// these functions expose a small shared runtime state bucket for the engine
// the assignment systems use it to share frame timing object lifetime and collision callbacks
void setFrameDelta(std::uint64_t delta_time_ms, float delta_seconds);

std::uint64_t deltaTimeMs();

float deltaSeconds();

// active game objects live in utility owned storage until removed or cleared
std::size_t addGameObject(std::unique_ptr<GameObject> object);

bool removeGameObjectByRenderElement(std::uint32_t render_element);

// this is the main per frame object update hook
// it steps gameplay animation and then dispatches collision responses after the broad phase query pass
void updateGameObjects(float delta_seconds);

void clearGameObjects();

// these lookups are mainly for glue code that only knows the render element id
const GameObject* findGameObjectByRenderElement(std::uint32_t render_element);

GameObject* findMutableGameObjectByRenderElement(std::uint32_t render_element);

// local bounds templates let the renderer side mesh data teach newly created objects about their mesh bounds
bool setLocalBoundsByRenderElement(std::uint32_t render_element,
                                   const glm::vec3& min_bounds,
                                   const glm::vec3& max_bounds);

bool clearLocalBoundsByRenderElement(std::uint32_t render_element);

bool setLocalConvexHullByRenderElement(std::uint32_t render_element,
                                       const std::vector<glm::vec3>& points);

bool clearLocalConvexHullByRenderElement(std::uint32_t render_element);

ConvexCollisionQueryResult queryConvexCollisionByRenderElement(std::uint32_t first_render_element,
                                                               std::uint32_t second_render_element);

// collision type ids let gameplay register pairwise responses without hard wiring object classes together
std::uint32_t rtsCollisionType();

std::uint32_t registerCollisionType();

bool registerCollisionResponse(std::uint32_t type_a,
                               std::uint32_t type_b,
                               CollisionResponse response);

void clearCollisionResponses();
}  // namespace utility

#endif
