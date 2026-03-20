#include <cmath>
#include <iostream>

#include <glm/gtc/constants.hpp>

#include "Engine.h"
#include "Quaternion.h"
#include "Utility.h"

namespace {
/**
 * @brief Compares two floating point values with epsilon tolerance
 * @param a Left value
 * @param b Right value
 * @param epsilon Allowed absolute tolerance
 * @return True when values are nearly equal
 */
bool nearly_equal(float a, float b, float epsilon = 0.0001f) {
    return std::fabs(a - b) <= epsilon;
}

/**
 * @brief Compares two vectors with epsilon tolerance
 * @param a Left vector
 * @param b Right vector
 * @param epsilon Allowed absolute tolerance
 * @return True when all components are nearly equal
 */
bool nearly_equal_vec3(const glm::vec3& a, const glm::vec3& b, float epsilon = 0.0001f) {
    return nearly_equal(a.x, b.x, epsilon) &&
           nearly_equal(a.y, b.y, epsilon) &&
           nearly_equal(a.z, b.z, epsilon);
}

/**
 * @brief Tests quaternion vector rotation for a 90 degree Y rotation
 * @return True when the rotated vector is correct
 */
bool test_quaternion_vector_rotation() {
    const Quaternion q(glm::vec3(0.0f, 1.0f, 0.0f), glm::half_pi<float>());
    const glm::vec3 rotated = q * glm::vec3(1.0f, 0.0f, 0.0f);
    return nearly_equal_vec3(rotated, glm::vec3(0.0f, 0.0f, -1.0f), 0.001f);
}

/**
 * @brief Tests engine delta API passthrough
 * @return True when API returns values stored in utility
 */
bool test_engine_delta_api() {
    utility::setFrameDelta(16, 0.016f);
    return getDeltaTime() == 16 && nearly_equal(getDeltaSeconds(), 0.016f, 0.00001f);
}

/**
 * @brief Tests object update and transform lookup by render element
 * @return True when translation and lifecycle behavior are correct
 */
bool test_object_update_and_lookup() {
    clearActiveGameObjects();
    utility::setFrameDelta(1000, 1.0f);
    const bool spawned = spawnRtsGameObject(1001,
                                            glm::vec3(0.0f, 0.0f, 0.0f),
                                            glm::vec3(2.0f, 0.0f, 0.0f),
                                            glm::vec3(0.0f, glm::pi<float>(), 0.0f));
    if (!spawned) {
        return false;
    }

    updateActiveGameObjects();
    const glm::mat4 model = getModelForRenderElement(1001);
    const glm::vec3 translation(model[3][0], model[3][1], model[3][2]);
    const bool moved = nearly_equal_vec3(translation, glm::vec3(2.0f, 0.0f, 0.0f), 0.001f);

    const bool removed = destroyGameObject(1001);
    const glm::mat4 fallback_model = getModelForRenderElement(1001);
    const glm::vec3 fallback_translation(fallback_model[3][0], fallback_model[3][1], fallback_model[3][2]);
    clearActiveGameObjects();
    return moved && removed && nearly_equal_vec3(fallback_translation, glm::vec3(0.0f));
}
}  // namespace

/**
 * @brief Executes Objective B unit tests
 * @return Zero on success otherwise non-zero
 */
int main() {
    int failures = 0;

    if (!test_quaternion_vector_rotation()) {
        std::cerr << "ObjectiveB test failure: quaternion rotation\n";
        ++failures;
    }
    if (!test_engine_delta_api()) {
        std::cerr << "ObjectiveB test failure: delta API\n";
        ++failures;
    }
    if (!test_object_update_and_lookup()) {
        std::cerr << "ObjectiveB test failure: object update and lookup\n";
        ++failures;
    }

    if (failures == 0) {
        std::cout << "ObjectiveB tests passed\n";
        return 0;
    }
    return 1;
}
