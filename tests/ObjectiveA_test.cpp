#include <cmath>
#include <iostream>

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

bool test_helper_integrators() {
    const glm::vec3 linear_change = integrateLinear(0.5f, glm::vec3(2.0f, -4.0f, 1.0f));
    if (!nearly_equal_vec3(linear_change, glm::vec3(1.0f, -2.0f, 0.5f), 0.0001f)) {
        return false;
    }

    const Quaternion delta_rotation = integrateAngular(
        0.5f, glm::vec3(0.0f, glm::pi<float>(), 0.0f));
    const glm::vec3 rotated =
        delta_rotation * glm::vec3(1.0f, 0.0f, 0.0f);
    return nearly_equal_vec3(rotated, glm::vec3(0.0f, 0.0f, -1.0f), 0.001f);
}

bool test_object_kinematics_and_bounds() {
    clearActiveGameObjects();
    clearLocalBoundsForRenderElement(2001);
    utility::setFrameDelta(500, 0.5f);

    if (!setLocalBoundsForRenderElement(2001,
                                        glm::vec3(-1.0f, -0.5f, -0.25f),
                                        glm::vec3(1.0f, 0.5f, 0.25f))) {
        return false;
    }

    if (!spawnRtsGameObject(2001, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.0f))) {
        return false;
    }

    if (!hasAabbForRenderElement(2001) ||
        !nearly_equal_vec3(getAabbMinForRenderElement(2001),
                           glm::vec3(-1.0f, -0.5f, -0.25f), 0.001f) ||
        !nearly_equal_vec3(getAabbMaxForRenderElement(2001),
                           glm::vec3(1.0f, 0.5f, 0.25f), 0.001f)) {
        clearActiveGameObjects();
        clearLocalBoundsForRenderElement(2001);
        return false;
    }

    if (!integrateAcceleration(2001, getDeltaSeconds(), glm::vec3(4.0f, 0.0f, 0.0f)) ||
        !nearly_equal_vec3(getLinearVelocityForRenderElement(2001),
                           glm::vec3(2.0f, 0.0f, 0.0f), 0.001f)) {
        clearActiveGameObjects();
        clearLocalBoundsForRenderElement(2001);
        return false;
    }

    if (!integrateLinear(2001) ||
        !nearly_equal_vec3(getPositionForRenderElement(2001),
                           glm::vec3(1.0f, 0.0f, 0.0f), 0.001f) ||
        !nearly_equal_vec3(getAabbMinForRenderElement(2001),
                           glm::vec3(0.0f, -0.5f, -0.25f), 0.001f) ||
        !nearly_equal_vec3(getAabbMaxForRenderElement(2001),
                           glm::vec3(2.0f, 0.5f, 0.25f), 0.001f)) {
        clearActiveGameObjects();
        clearLocalBoundsForRenderElement(2001);
        return false;
    }

    if (!applyLinearImpulse(2001, glm::vec3(0.0f, 1.0f, 0.0f)) ||
        !nearly_equal_vec3(getLinearVelocityForRenderElement(2001),
                           glm::vec3(2.0f, 1.0f, 0.0f), 0.001f)) {
        clearActiveGameObjects();
        clearLocalBoundsForRenderElement(2001);
        return false;
    }

    if (!integrateAngularAcceleration(2001,
                                      getDeltaSeconds(),
                                      glm::vec3(0.0f, glm::pi<float>(), 0.0f)) ||
        !applyAngularImpulse(2001, glm::vec3(0.0f, glm::half_pi<float>(), 0.0f)) ||
        !nearly_equal_vec3(getAngularVelocityForRenderElement(2001),
                           glm::vec3(0.0f, glm::pi<float>(), 0.0f), 0.001f)) {
        clearActiveGameObjects();
        clearLocalBoundsForRenderElement(2001);
        return false;
    }

    if (!integrateAngular(2001)) {
        clearActiveGameObjects();
        clearLocalBoundsForRenderElement(2001);
        return false;
    }

    const glm::mat4 model = getModelForRenderElement(2001);
    const glm::vec3 facing_x_axis(model[0][0], model[0][1], model[0][2]);
    const bool rotated =
        nearly_equal_vec3(facing_x_axis, glm::vec3(0.0f, 0.0f, -1.0f), 0.001f);
    const bool bounds_rotated =
        nearly_equal_vec3(getAabbMinForRenderElement(2001),
                          glm::vec3(0.75f, -0.5f, -1.0f), 0.001f) &&
        nearly_equal_vec3(getAabbMaxForRenderElement(2001),
                          glm::vec3(1.25f, 0.5f, 1.0f), 0.001f);

    const bool cleared = destroyGameObject(2001) && clearLocalBoundsForRenderElement(2001);
    clearActiveGameObjects();
    return rotated && bounds_rotated && cleared;
}
}  // namespace

int main() {
    int failures = 0;

    if (!test_helper_integrators()) {
        std::cerr << "ObjectiveA test failure: helper integrators\n";
        ++failures;
    }

    if (!test_object_kinematics_and_bounds()) {
        std::cerr << "ObjectiveA test failure: object kinematics and bounds\n";
        ++failures;
    }

    return failures == 0 ? 0 : 1;
}
