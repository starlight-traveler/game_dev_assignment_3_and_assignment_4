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
 * @brief Extracts translation from a model matrix
 * @param matrix Matrix to inspect
 * @return Translation vector from the last column
 */
glm::vec3 translation_from_matrix(const glm::mat4& matrix) {
    return glm::vec3(matrix[3][0], matrix[3][1], matrix[3][2]);
}

/**
 * @brief Tests quaternion vector rotation for a 90 degree Y rotation
 * @return True when the rotated vector is correct
 */
bool test_quaternion_vector_rotation() {
    // Build a quaternion that represents a quarter turn around the world Y axis
    // Objective B requires quaternion math because GameObject orientation is stored as a quaternion
    const Quaternion q(glm::vec3(0.0f, 1.0f, 0.0f), glm::half_pi<float>());

    // Rotate the +X basis vector through that quaternion
    // In a right-handed system a positive 90 degree yaw sends +X toward -Z
    const glm::vec3 rotated = q * glm::vec3(1.0f, 0.0f, 0.0f);

    // This test is about correctness, not speed
    // If this fails, every higher level system that depends on orientation would be suspect
    return nearly_equal_vec3(rotated, glm::vec3(0.0f, 0.0f, -1.0f), 0.001f);
}

/**
 * @brief Tests engine delta API passthrough
 * @return True when API returns values stored in utility
 */
bool test_engine_delta_api() {
    // Seed the backing utility state with a known frame duration
    // The engine API is meant to expose this state without callers touching utility internals directly
    utility::setFrameDelta(16, 0.016f);

    // Objective B expects GameObjects to update from frame delta
    // This verifies the public API returns the exact values stored in the engine utility layer
    return getDeltaTime() == 16 && nearly_equal(getDeltaSeconds(), 0.016f, 0.00001f);
}

/**
 * @brief Tests object update and transform lookup by render element
 * @return True when translation and lifecycle behavior are correct
 */
bool test_object_update_and_lookup() {
    // Start from a clean object pool so this test is isolated from previous runs
    clearActiveGameObjects();

    // Use a one second step so the expected motion is easy to reason about by inspection
    utility::setFrameDelta(1000, 1.0f);

    // Spawn one RTS unit through the engine API
    // The render element id doubles as the lookup key used by rendering code and helper queries
    // Linear velocity is +2 on X, so after one second the object should move exactly two units
    // Angular velocity is also supplied so the object exercises both translational and rotational state
    const bool spawned = spawnRtsGameObject(1001,
                                            glm::vec3(0.0f, 0.0f, 0.0f),
                                            glm::vec3(2.0f, 0.0f, 0.0f),
                                            glm::vec3(0.0f, glm::pi<float>(), 0.0f));
    if (!spawned) {
        return false;
    }

    // Update all active objects through the engine-facing loop
    // This is the polymorphic path used by the actual engine
    // For Objective B, correctness is checked here while virtual-versus-function-pointer cost is benchmarked
    // separately in tools/ObjectiveB_Benchmark.cpp
    updateActiveGameObjects();

    // Pull the model matrix back out the same way the renderer would
    // The fourth column holds translation because GLM matrices are column major
    const glm::mat4 model = getModelForRenderElement(1001);
    const glm::vec3 translation = translation_from_matrix(model);

    // Position should now be origin + velocity * delta_seconds = (2, 0, 0)
    const bool moved = nearly_equal_vec3(translation, glm::vec3(2.0f, 0.0f, 0.0f), 0.001f);

    // Destroy the object through the engine API to verify lifecycle management and lookup cleanup
    const bool removed = destroyGameObject(1001);

    // Once removed, querying by the old render element should fall back to an identity model
    // That means the translation portion should read as zero
    const glm::mat4 fallback_model = getModelForRenderElement(1001);
    const glm::vec3 fallback_translation = translation_from_matrix(fallback_model);

    // Leave the global object buffer in a clean state for anything that runs afterward
    clearActiveGameObjects();
    return moved && removed && nearly_equal_vec3(fallback_translation, glm::vec3(0.0f));
}

/**
 * @brief Tests RTS move-command behavior including arrival snap and facing
 * @return True when the unit moves, faces the target, and stops on arrival
 */
bool test_rts_move_command() {
    clearActiveGameObjects();
    utility::setFrameDelta(1000, 1.0f);

    const bool spawned = spawnRtsGameObject(1101,
                                            glm::vec3(0.0f, 0.0f, 0.0f),
                                            glm::vec3(0.0f),
                                            glm::vec3(0.0f));
    if (!spawned || !issueMoveCommand(1101, glm::vec3(0.0f, 0.0f, 5.0f), 2.0f, 0.05f)) {
        return false;
    }

    updateActiveGameObjects();

    const glm::vec3 first_step_position = getPositionForRenderElement(1101);
    if (!nearly_equal_vec3(first_step_position, glm::vec3(0.0f, 0.0f, 2.0f), 0.001f)) {
        clearActiveGameObjects();
        return false;
    }

    const glm::mat4 model = getModelForRenderElement(1101);
    const glm::vec3 facing_x_axis(model[0][0], model[0][1], model[0][2]);
    if (!nearly_equal_vec3(facing_x_axis, glm::vec3(0.0f, 0.0f, 1.0f), 0.001f)) {
        clearActiveGameObjects();
        return false;
    }

    if (!isRtsGameObjectMoving(1101)) {
        clearActiveGameObjects();
        return false;
    }

    updateActiveGameObjects();
    updateActiveGameObjects();

    const glm::vec3 arrived_position = getPositionForRenderElement(1101);
    const bool arrived = nearly_equal_vec3(arrived_position, glm::vec3(0.0f, 0.0f, 5.0f), 0.001f);
    const bool stopped = !isRtsGameObjectMoving(1101);

    clearActiveGameObjects();
    return arrived && stopped;
}

/**
 * @brief Tests that a stop command freezes an active RTS move
 * @return True when the unit remains still after stop is issued
 */
bool test_rts_stop_command() {
    clearActiveGameObjects();
    utility::setFrameDelta(500, 0.5f);

    const bool spawned = spawnRtsGameObject(1102,
                                            glm::vec3(0.0f, 0.0f, 0.0f),
                                            glm::vec3(0.0f),
                                            glm::vec3(0.0f));
    if (!spawned || !issueMoveCommand(1102, glm::vec3(10.0f, 0.0f, 0.0f), 4.0f, 0.05f)) {
        return false;
    }

    updateActiveGameObjects();
    const glm::vec3 before_stop = getPositionForRenderElement(1102);
    if (!stopRtsGameObject(1102)) {
        clearActiveGameObjects();
        return false;
    }

    updateActiveGameObjects();
    const glm::vec3 after_stop = getPositionForRenderElement(1102);
    const bool stayed_put = nearly_equal_vec3(before_stop, after_stop, 0.001f);
    const bool stopped = !isRtsGameObjectMoving(1102);

    clearActiveGameObjects();
    return stayed_put && stopped;
}
}  // namespace

/**
 * @brief Executes Objective B unit tests
 * @return Zero on success otherwise non-zero
 */
int main() {
    int failures = 0;

    // Quaternion math is a foundational Objective B requirement because GameObject rotation depends on it
    if (!test_quaternion_vector_rotation()) {
        std::cerr << "ObjectiveB test failure: quaternion rotation\n";
        ++failures;
    }

    // Delta-time access is the link between the engine timing module and object update logic
    if (!test_engine_delta_api()) {
        std::cerr << "ObjectiveB test failure: delta API\n";
        ++failures;
    }

    // This final test checks the practical GameObject workflow
    // spawn -> update through the engine loop -> query render transform -> destroy
    // The benchmark for virtual dispatch versus function-pointer dispatch lives in the benchmark tool
    // because that comparison is about runtime cost rather than pass or fail correctness
    if (!test_object_update_and_lookup()) {
        std::cerr << "ObjectiveB test failure: object update and lookup\n";
        ++failures;
    }

    if (!test_rts_move_command()) {
        std::cerr << "ObjectiveB test failure: RTS move command\n";
        ++failures;
    }

    if (!test_rts_stop_command()) {
        std::cerr << "ObjectiveB test failure: RTS stop command\n";
        ++failures;
    }

    if (failures == 0) {
        std::cout << "ObjectiveB tests passed\n";
        return 0;
    }
    return 1;
}
