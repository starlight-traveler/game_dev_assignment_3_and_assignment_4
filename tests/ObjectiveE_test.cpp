#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "SceneGraph.h"

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
 * @brief Extracts translation from a transform matrix
 * @param matrix Matrix to inspect
 * @return Translation vector
 */
glm::vec3 translation_from_matrix(const glm::mat4& matrix) {
    return glm::vec3(matrix[3][0], matrix[3][1], matrix[3][2]);
}

/**
 * @brief Tests parent-child transform propagation and reparenting
 * @return True when propagation and reparenting are correct
 */
bool test_transform_hierarchy_and_reparent() {
    SceneGraph graph{};
    const glm::mat4 parent_a_local = glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, 0.0f, 0.0f));
    const glm::mat4 parent_b_local = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 5.0f));
    const glm::mat4 child_local = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));

    const SceneNodeId parent_a = graph.createNode(graph.rootNodeId(), 2001, parent_a_local, 0.5f);
    graph.createNode(graph.rootNodeId(), 2002, parent_b_local, 0.5f);
    graph.createNode(parent_a, 2003, child_local, 0.5f);

    graph.updateWorldTransforms();
    const glm::vec3 child_world_a = translation_from_matrix(graph.worldTransformForObject(2003));
    if (!nearly_equal_vec3(child_world_a, glm::vec3(5.0f, 0.0f, 0.0f), 0.001f)) {
        return false;
    }

    if (!graph.setParentByObject(2003, 2002)) {
        return false;
    }
    graph.updateWorldTransforms();
    const glm::vec3 child_world_b = translation_from_matrix(graph.worldTransformForObject(2003));
    return nearly_equal_vec3(child_world_b, glm::vec3(1.0f, 0.0f, 5.0f), 0.001f);
}

/**
 * @brief Tests radius and AABB spatial queries and removal behavior
 * @return True when query outputs and removal updates are correct
 */
bool test_spatial_queries_and_removal() {
    SceneGraph graph{};
    graph.setMaxLeafObjects(1);

    graph.createNode(
        graph.rootNodeId(), 3001,
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)),
        0.5f);
    graph.createNode(
        graph.rootNodeId(), 3002,
        glm::translate(glm::mat4(1.0f), glm::vec3(7.0f, 0.0f, 0.0f)),
        0.5f);
    graph.createNode(
        graph.rootNodeId(), 3003,
        glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 2.0f)),
        1.0f);

    graph.updateWorldTransforms();
    graph.rebuildSpatialIndex();

    std::vector<std::uint32_t> radius_query{};
    graph.queryRadius(radius_query, glm::vec3(0.0f, 0.0f, 0.0f), 2.8f);
    const bool found_3001 = std::find(radius_query.begin(), radius_query.end(), 3001) != radius_query.end();
    const bool found_3002 = std::find(radius_query.begin(), radius_query.end(), 3002) != radius_query.end();
    const bool found_3003 = std::find(radius_query.begin(), radius_query.end(), 3003) != radius_query.end();
    if (!(found_3001 && found_3003 && !found_3002)) {
        return false;
    }

    std::vector<std::uint32_t> aabb_query{};
    graph.queryAabb(aabb_query, glm::vec2(-1.0f, -1.0f), glm::vec2(3.1f, 3.1f));
    const bool aabb_has_3001 = std::find(aabb_query.begin(), aabb_query.end(), 3001) != aabb_query.end();
    const bool aabb_has_3003 = std::find(aabb_query.begin(), aabb_query.end(), 3003) != aabb_query.end();
    const bool aabb_has_3002 = std::find(aabb_query.begin(), aabb_query.end(), 3002) != aabb_query.end();
    if (!(aabb_has_3001 && aabb_has_3003 && !aabb_has_3002)) {
        return false;
    }

    if (!graph.removeNodeByObject(3001)) {
        return false;
    }
    graph.rebuildSpatialIndex();
    radius_query.clear();
    graph.queryRadius(radius_query, glm::vec3(0.0f, 0.0f, 0.0f), 2.8f);
    return std::find(radius_query.begin(), radius_query.end(), 3001) == radius_query.end();
}
}  // namespace

/**
 * @brief Executes Objective E unit tests
 * @return Zero on success otherwise non-zero
 */
int main() {
    int failures = 0;

    if (!test_transform_hierarchy_and_reparent()) {
        std::cerr << "ObjectiveE test failure: transform hierarchy and reparent\n";
        ++failures;
    }
    if (!test_spatial_queries_and_removal()) {
        std::cerr << "ObjectiveE test failure: spatial queries and removal\n";
        ++failures;
    }

    if (failures == 0) {
        std::cout << "ObjectiveE tests passed\n";
        return 0;
    }
    return 1;
}
