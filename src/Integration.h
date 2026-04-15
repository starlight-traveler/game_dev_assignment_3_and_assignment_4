/**
 * @file Integration.h
 * @brief Small kinematic integration helpers for Objective A
 */
#ifndef INTEGRATION_H
#define INTEGRATION_H

#include <glm/glm.hpp>

#include "Quaternion.h"

/**
 * @brief Integrates a linear quantity over a discrete time step
 * @param delta_time Delta time in seconds
 * @param linear Linear rate of change in units per second
 * @return Integrated change for the step
 */
inline glm::vec3 integrateLinear(float delta_time, const glm::vec3& linear) {
    if (delta_time <= 0.0f) {
        return glm::vec3(0.0f);
    }

    return linear * delta_time;
}

/**
 * @brief Integrates angular velocity into a frame-sized rotation quaternion
 * @param delta_time Delta time in seconds
 * @param angular Angular velocity in radians per second
 * @return Delta rotation quaternion for the step
 */
inline Quaternion integrateAngular(float delta_time, const glm::vec3& angular) {
    const float speed = glm::length(angular);
    if (delta_time <= 0.0f || speed <= 0.000001f) {
        return Quaternion();
    }

    const glm::vec3 axis = angular / speed;
    const float angle = speed * delta_time;
    return Quaternion(axis, angle);
}

#endif
