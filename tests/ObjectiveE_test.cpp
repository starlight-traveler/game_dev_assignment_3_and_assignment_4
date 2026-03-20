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
    // The scene graph stores full 4x4 transforms, but these tests only care about world-space position
    // In GLM's column-major layout the translation lives in the last column
    return glm::vec3(matrix[3][0], matrix[3][1], matrix[3][2]);
}

/**
 * @brief Tests parent-child transform propagation and reparenting
 * @return True when propagation and reparenting are correct
 */
bool test_transform_hierarchy_and_reparent() {
    // Build a fresh scene graph
    // Each test owns its own graph so no state leaks between assertions
    SceneGraph graph{};

    // Parent A sits four units on +X
    // Parent B sits five units on +Z
    // The child is one unit on +X relative to whichever parent owns it
    const glm::mat4 parent_a_local = glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, 0.0f, 0.0f));
    const glm::mat4 parent_b_local = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 5.0f));
    const glm::mat4 child_local = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));

    // Insert two parents under the synthetic root
    // Then attach the child under parent A
    // These object ids give us a stable handle back into the graph by object reference
    const SceneNodeId parent_a = graph.createNode(graph.rootNodeId(), 2001, parent_a_local, 0.5f);
    graph.createNode(graph.rootNodeId(), 2002, parent_b_local, 0.5f);
    graph.createNode(parent_a, 2003, child_local, 0.5f);

    // Propagate local transforms down the tree
    // The child should inherit parent A's +4 X translation and then add its own +1 X offset
    graph.updateWorldTransforms();
    const glm::vec3 child_world_a = translation_from_matrix(graph.worldTransformForObject(2003));
    if (!nearly_equal_vec3(child_world_a, glm::vec3(5.0f, 0.0f, 0.0f), 0.001f)) {
        return false;
    }

    // Reparent the child from object 2001 to object 2002
    // This is one of the most important hierarchy operations because the same local transform
    // must now be interpreted relative to a different ancestor chain
    if (!graph.setParentByObject(2003, 2002)) {
        return false;
    }

    // After recomputing world transforms, the child should keep its local +1 X offset
    // but inherit parent B's +5 Z translation instead of parent A's +4 X translation
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

    // Force the BVH to split aggressively so this test exercises internal nodes instead of a single leaf
    // With one object per leaf, the rebuild will produce a deeper tree and make traversal behavior clearer
    graph.setMaxLeafObjects(1);

    // Object 3001 starts at the origin with a small radius
    // Object 3002 is far away on +X and should be rejected by local queries near the origin
    // Object 3003 sits near the origin diagonally with a larger radius so it overlaps broader queries
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

    // As with all scene graph queries, world transforms must be current before spatial bounds are built
    graph.updateWorldTransforms();

    // Rebuild the BVH from the active object-backed nodes
    // The tree is a broad-phase acceleration structure layered on top of the transform hierarchy
    graph.rebuildSpatialIndex();

    // Query by radius around the origin
    // 3001 is centered at the query origin, 3003 is close enough to overlap, and 3002 is too far away
    std::vector<std::uint32_t> radius_query{};
    graph.queryRadius(radius_query, glm::vec3(0.0f, 0.0f, 0.0f), 2.8f);
    const bool found_3001 = std::find(radius_query.begin(), radius_query.end(), 3001) != radius_query.end();
    const bool found_3002 = std::find(radius_query.begin(), radius_query.end(), 3002) != radius_query.end();
    const bool found_3003 = std::find(radius_query.begin(), radius_query.end(), 3003) != radius_query.end();
    if (!(found_3001 && found_3003 && !found_3002)) {
        return false;
    }

    // Query with a 2D AABB in the XZ plane
    // This mirrors the kind of top-down selection or culling region an RTS-style engine would often use
    // The box covers the origin and extends far enough to include object 3003 but not the distant 3002
    std::vector<std::uint32_t> aabb_query{};
    graph.queryAabb(aabb_query, glm::vec2(-1.0f, -1.0f), glm::vec2(3.1f, 3.1f));
    const bool aabb_has_3001 = std::find(aabb_query.begin(), aabb_query.end(), 3001) != aabb_query.end();
    const bool aabb_has_3003 = std::find(aabb_query.begin(), aabb_query.end(), 3003) != aabb_query.end();
    const bool aabb_has_3002 = std::find(aabb_query.begin(), aabb_query.end(), 3002) != aabb_query.end();
    if (!(aabb_has_3001 && aabb_has_3003 && !aabb_has_3002)) {
        return false;
    }

    // Remove one object by its engine-facing object id
    // This validates that the scene graph can drop spatial entries without corrupting later queries
    if (!graph.removeNodeByObject(3001)) {
        return false;
    }

    // The BVH is rebuild-based, so removal is followed by a fresh spatial index build
    graph.rebuildSpatialIndex();
    radius_query.clear();

    // Re-run the same radius query and confirm the removed object no longer appears
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

    // Objective E is split into two big ideas
    // first, the graph must behave like a transform hierarchy
    // second, it must also support accelerated spatial queries over those objects
    if (!test_transform_hierarchy_and_reparent()) {
        std::cerr << "ObjectiveE test failure: transform hierarchy and reparent\n";
        ++failures;
    }

    // This second test focuses on the BVH-backed broad phase rather than transform inheritance
    // Together these two tests cover the dual role of the SceneGraph implementation
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
