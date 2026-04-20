#include <cmath>
#include <initializer_list>
#include <iostream>
#include <vector>

#include <glm/gtc/constants.hpp>

#include "Engine.h"
#include "Utility.h"

namespace {
bool nearly_equal(float a, float b, float epsilon = 0.0001f) {
    return std::fabs(a - b) <= epsilon;
}

bool nearly_equal_vec3(const glm::vec3& a, const glm::vec3& b, float epsilon = 0.0001f) {
    return nearly_equal(a.x, b.x, epsilon) &&
           nearly_equal(a.y, b.y, epsilon) &&
           nearly_equal(a.z, b.z, epsilon);
}

void clear_ids(std::initializer_list<std::uint32_t> ids) {
    for (const std::uint32_t render_element : ids) {
        clearLocalBoundsForRenderElement(render_element);
        clearConvexHullForRenderElement(render_element);
    }
}

bool test_oriented_box_separation_output() {
    clearActiveGameObjects();
    clear_ids({4001, 4002});
    utility::setFrameDelta(16, 0.016f);

    if (!setLocalBoundsForRenderElement(4001,
                                        glm::vec3(-1.0f, -1.0f, -1.0f),
                                        glm::vec3(1.0f, 1.0f, 1.0f)) ||
        !setLocalBoundsForRenderElement(4002,
                                        glm::vec3(-1.0f, -1.0f, -1.0f),
                                        glm::vec3(1.0f, 1.0f, 1.0f)) ||
        !spawnRtsGameObject(4001, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f)) ||
        !spawnRtsGameObject(4002, glm::vec3(3.0f, 0.0f, 0.0f), glm::vec3(0.0f), glm::vec3(0.0f))) {
        clearActiveGameObjects();
        clear_ids({4001, 4002});
        return false;
    }

    const ConvexCollisionQueryResult query =
        queryConvexCollisionBetweenRenderElements(4001, 4002);

    clearActiveGameObjects();
    clear_ids({4001, 4002});
    return query.has_convex_data &&
           !query.intersecting &&
           nearly_equal(query.separation_distance, 1.0f, 0.01f) &&
           nearly_equal_vec3(query.separating_axis, glm::vec3(1.0f, 0.0f, 0.0f), 0.01f) &&
           nearly_equal_vec3(query.closest_point_first, glm::vec3(1.0f, 0.0f, 0.0f), 0.01f) &&
           nearly_equal_vec3(query.closest_point_second, glm::vec3(2.0f, 0.0f, 0.0f), 0.01f);
}

bool test_explicit_convex_hull_overrides_loose_bounds() {
    clearActiveGameObjects();
    clear_ids({4101, 4102});
    utility::setFrameDelta(500, 0.5f);

    const std::vector<glm::vec3> tetrahedron{
        glm::vec3(-0.5f, 0.0f, -0.5f),
        glm::vec3(0.5f, 0.0f, -0.5f),
        glm::vec3(0.0f, 0.0f, 0.6f),
        glm::vec3(0.0f, 1.0f, 0.0f),
    };

    if (!setLocalBoundsForRenderElement(4101,
                                        glm::vec3(-2.0f, -2.0f, -2.0f),
                                        glm::vec3(2.0f, 2.0f, 2.0f)) ||
        !setLocalBoundsForRenderElement(4102,
                                        glm::vec3(-2.0f, -2.0f, -2.0f),
                                        glm::vec3(2.0f, 2.0f, 2.0f)) ||
        !setConvexHullForRenderElement(4101, tetrahedron) ||
        !setConvexHullForRenderElement(4102, tetrahedron) ||
        !spawnRtsGameObject(4101, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f)) ||
        !spawnRtsGameObject(4102, glm::vec3(2.2f, 0.0f, 0.0f), glm::vec3(0.0f), glm::vec3(0.0f)) ||
        !applyAngularImpulse(4102, glm::vec3(0.0f, glm::half_pi<float>(), 0.0f)) ||
        !integrateAngular(4102, 0.5f)) {
        clearActiveGameObjects();
        clear_ids({4101, 4102});
        return false;
    }

    const ConvexCollisionQueryResult query =
        queryConvexCollisionBetweenRenderElements(4101, 4102);

    clearActiveGameObjects();
    clear_ids({4101, 4102});
    return query.has_convex_data &&
           !query.intersecting &&
           query.separation_distance > 0.4f &&
           query.separation_distance < 1.6f;
}
}  // namespace

int main() {
    int failures = 0;

    if (!test_oriented_box_separation_output()) {
        std::cerr << "ObjectiveD test failure: oriented box separation output\n";
        ++failures;
    }

    if (!test_explicit_convex_hull_overrides_loose_bounds()) {
        std::cerr << "ObjectiveD test failure: explicit convex hull override\n";
        ++failures;
    }

    if (failures == 0) {
        std::cout << "ObjectiveD tests passed\n";
        return 0;
    }
    return 1;
}
