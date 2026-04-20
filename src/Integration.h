#ifndef INTEGRATION_H
#define INTEGRATION_H

#include <glm/glm.hpp>

#include "Quaternion.h"

// these helpers turn per second motion values into one frame sized motion step
// the rest of the engine can stay simple and ask for the change over just this frame
inline glm::vec3 integrateLinear(float delta_time, const glm::vec3& linear) {
    // a zero or negative step should not move anything
    if (delta_time <= 0.0f) {
        return glm::vec3(0.0f);
    }

    // linear velocity times elapsed time gives the displacement for this frame
    return linear * delta_time;
}

// angular integration is a little different because we want a rotation object back
// first we measure how fast the angular vector is spinning and then convert it to axis angle form
inline Quaternion integrateAngular(float delta_time, const glm::vec3& angular) {
    const float speed = glm::length(angular);

    // if there is no time or no meaningful angular speed then the identity rotation is correct
    if (delta_time <= 0.0f || speed <= 0.000001f) {
        return Quaternion();
    }

    // normalize the angular vector to recover just the spin axis
    const glm::vec3 axis = angular / speed;

    // total angle moved this frame is angular speed times elapsed time
    const float angle = speed * delta_time;

    // the quaternion constructor handles turning axis angle into a usable rotation
    return Quaternion(axis, angle);
}

#endif
