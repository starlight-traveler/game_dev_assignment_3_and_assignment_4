/**
 * @file SceneGraph.h
 * @brief Hierarchical transform tree with BVH spatial culling for RTS worlds
 */
#ifndef SCENE_GRAPH_H
#define SCENE_GRAPH_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

/**
 * @brief Tree node id type used by SceneGraph
 */
using SceneNodeId = std::uint32_t;

/**
 * @brief Node for hierarchical transform propagation and object reference
 */
struct SceneNode {
    SceneNodeId id;
    SceneNodeId parent;
    std::vector<SceneNodeId> children;
    std::optional<std::uint32_t> object_reference;
    glm::mat4 local_transform;
    glm::mat4 world_transform;
    float bounding_radius;
    bool active;
};

/**
 * @brief Scene graph class with tree traversal and BVH render culling
 *
 * The class does two related but different jobs
 * 1. keep a parent-child transform tree so child nodes inherit parent motion
 * 2. build a bounding volume hierarchy over active object nodes for broad spatial queries
 *
 * The BVH is a binary tree of bounding boxes
 * - leaf nodes store a contiguous range of object-backed scene nodes
 * - internal nodes store bounds that contain both child nodes
 * - queries first test against the large parent bounds and only descend when overlap exists
 *
 * This matters because broad-phase traversal becomes much cheaper than checking every object
 * especially once objects start spreading through a larger RTS-style world
 */
class SceneGraph {
public:
    /**
     * @brief Constructs graph root and default BVH configuration
     */
    SceneGraph();

    /**
     * @brief Sets the maximum number of objects stored in one BVH leaf
     * @param max_leaf_objects New positive BVH leaf capacity
     */
    void setMaxLeafObjects(std::size_t max_leaf_objects);

    /**
     * @brief Gets the maximum number of objects stored in one BVH leaf
     * @return BVH leaf capacity
     */
    std::size_t maxLeafObjects() const;

    /**
     * @brief Returns the root node id
     * @return Root id
     */
    SceneNodeId rootNodeId() const;

    /**
     * @brief Creates a node with optional object reference
     * @param parent Parent node id
     * @param object_reference Object id from memory pool
     * @param local_transform Local transform matrix
     * @param bounding_radius Bounding radius used for culling
     * @return Created node id
     */
    SceneNodeId createNode(SceneNodeId parent,
                           std::optional<std::uint32_t> object_reference,
                           const glm::mat4& local_transform,
                           float bounding_radius);

    /**
     * @brief Reparents an existing node under another node
     * @param child Node id to move
     * @param new_parent New parent node id
     * @return True when reparent succeeds
     */
    bool setParent(SceneNodeId child, SceneNodeId new_parent);

    /**
     * @brief Reparents an existing object node under another object node
     * @param child_object_reference Child object id from memory pool
     * @param parent_object_reference Parent object id or nullopt for root
     * @return True when reparent succeeds
     */
    bool setParentByObject(std::uint32_t child_object_reference,
                           std::optional<std::uint32_t> parent_object_reference);

    /**
     * @brief Removes a node by object reference
     * @param object_reference Object id from memory pool
     * @return True when removed
     */
    bool removeNodeByObject(std::uint32_t object_reference);

    /**
     * @brief Updates local transform by object reference
     * @param object_reference Object id from memory pool
     * @param local_transform New local transform
     * @return True when updated
     */
    bool setLocalTransformByObject(std::uint32_t object_reference, const glm::mat4& local_transform);

    /**
     * @brief Updates culling radius by object reference
     * @param object_reference Object id from memory pool
     * @param bounding_radius New radius used for culling and broad-phase overlap
     * @return True when updated
     */
    bool setBoundingRadiusByObject(std::uint32_t object_reference, float bounding_radius);

    /**
     * @brief Propagates parent-child transforms through hierarchy
     */
    void updateWorldTransforms();

    /**
     * @brief Rebuilds the BVH from active node world positions
     *
     * The rebuild gathers all active object-backed nodes, computes simple bounds from their
     * world positions plus bounding radii, then recursively partitions that list into a binary tree
     * A full rebuild is simpler than incremental updates for this assignment-sized engine
     */
    void rebuildSpatialIndex();

    /**
     * @brief Traverses visible spatial buckets and appends to render queue
     * @param render_queue Output queue of visible object references
     * @param camera_position Camera position in world space
     * @param cull_radius Query radius around camera for simple culling
     *
     * This is a convenience wrapper over queryRadius used by the render path
     * The important BVH idea is that most nodes are rejected before individual objects are tested
     */
    void render(std::vector<std::uint32_t>& render_queue,
                const glm::vec3& camera_position,
                float cull_radius) const;

    /**
     * @brief Queries object references overlapping an XZ radius around center
     * @param out_objects Output list cleared then filled with matching objects
     * @param center Query center in world space
     * @param radius Query radius in world units
     */
    void queryRadius(std::vector<std::uint32_t>& out_objects,
                     const glm::vec3& center,
                     float radius) const;

    /**
     * @brief Queries object references overlapping an XZ axis-aligned box
     * @param out_objects Output list cleared then filled with matching objects
     * @param min_xz Inclusive world-space minimum for XZ
     * @param max_xz Inclusive world-space maximum for XZ
     */
    void queryAabb(std::vector<std::uint32_t>& out_objects,
                   const glm::vec2& min_xz,
                   const glm::vec2& max_xz) const;

    /**
     * @brief Returns world transform for an object reference
     * @param object_reference Object id from memory pool
     * @return World transform matrix or identity
     */
    glm::mat4 worldTransformForObject(std::uint32_t object_reference) const;

    /**
     * @brief Returns active object count mapped into the scene graph
     * @return Number of active object-backed nodes
     */
    std::size_t activeObjectCount() const;

private:
    /**
     * @brief BVH node used for broad-phase traversal
     *
     * A BVH node stores one bounding box over a region of space
     * If it is a leaf, start/count identify which objects live in that region
     * If it is internal, left_child/right_child identify two smaller regions inside it
     */
    struct BvhNode {
        // bounds that contain either all objects in this leaf or both child nodes
        glm::vec3 min_bounds;
        glm::vec3 max_bounds;
        // child indices are only valid when is_leaf is false
        std::uint32_t left_child;
        std::uint32_t right_child;
        // start/count point into spatial_object_nodes_ for leaf storage
        std::size_t start;
        std::size_t count;
        bool is_leaf;
    };

    /**
     * @brief Recursive world transform update for one subtree
     * @param node_id Node to update
     * @param parent_world Parent world transform
     */
    void updateWorldRecursive(SceneNodeId node_id, const glm::mat4& parent_world);

    /**
     * @brief Removes node recursively from hierarchy
     * @param node_id Node to remove
     */
    void removeNodeRecursive(SceneNodeId node_id);

    /**
     * @brief Returns true when a node id is in-range and active
     * @param node_id Candidate node id
     * @return Valid and active status
     */
    bool isNodeActive(SceneNodeId node_id) const;

    /**
     * @brief Returns true when ancestor is in parent chain of node
     * @param ancestor Candidate ancestor node id
     * @param node Node id to test
     * @return True when ancestor is on the chain
     */
    bool isAncestor(SceneNodeId ancestor, SceneNodeId node) const;

    /**
     * @brief Extracts world position for one active node
     * @param node_id Node to inspect
     * @return Node world-space translation
     */
    glm::vec3 worldPositionForNode(SceneNodeId node_id) const;

    /**
     * @brief Computes a sphere-based AABB for one active node
     * @param node_id Node to inspect
     * @return Pair of minimum and maximum world-space bounds
     */
    std::pair<glm::vec3, glm::vec3> boundsForNode(SceneNodeId node_id) const;

    /**
     * @brief Computes combined bounds for a BVH object range
     * @param start Inclusive start index in spatial object array
     * @param end Exclusive end index in spatial object array
     * @return Pair of minimum and maximum world-space bounds
     *
     * These are the true object bounds used to contain all geometry assigned to one BVH subtree
     */
    std::pair<glm::vec3, glm::vec3> computeRangeBounds(std::size_t start, std::size_t end) const;

    /**
     * @brief Computes centroid bounds for a BVH object range
     * @param start Inclusive start index in spatial object array
     * @param end Exclusive end index in spatial object array
     * @return Pair of minimum and maximum centroid bounds
     *
     * Centroid bounds are used only for choosing a split axis
     * This is different from computeRangeBounds, which measures actual object volume
     */
    std::pair<glm::vec3, glm::vec3> computeCentroidBounds(std::size_t start, std::size_t end) const;

    /**
     * @brief Builds a BVH subtree over a contiguous spatial object range
     * @param start Inclusive start index in spatial object array
     * @param end Exclusive end index in spatial object array
     * @return BVH node index for the subtree root
     *
     * The builder chooses a split axis from the widest centroid dimension
     * then partitions the objects around the median so the subtree stays reasonably balanced
     */
    std::uint32_t buildBvhRecursive(std::size_t start, std::size_t end);

    /**
     * @brief Traverses the BVH for a circular XZ query
     * @param out_objects Output object references
     * @param bvh_node_index Current BVH node index
     * @param center Query center in world space
     * @param radius Query radius in world units
     *
     * The traversal is prune-first
     * A whole subtree is skipped when its node bounds cannot overlap the query region
     */
    void queryRadiusRecursive(std::vector<std::uint32_t>& out_objects,
                              std::uint32_t bvh_node_index,
                              const glm::vec3& center,
                              float radius) const;

    /**
     * @brief Traverses the BVH for an XZ AABB query
     * @param out_objects Output object references
     * @param bvh_node_index Current BVH node index
     * @param min_xz Inclusive minimum XZ corner
     * @param max_xz Inclusive maximum XZ corner
     *
     * This is the same traversal strategy as the radius query but with box overlap tests
     * Broad-phase rejection happens on parent boxes before leaf objects are checked
     */
    void queryAabbRecursive(std::vector<std::uint32_t>& out_objects,
                            std::uint32_t bvh_node_index,
                            const glm::vec2& min_xz,
                            const glm::vec2& max_xz) const;

    // upper bound on how many objects stay in one BVH leaf before splitting
    std::size_t max_leaf_objects_;
    std::vector<SceneNode> nodes_;
    std::vector<SceneNodeId> free_node_ids_;
    std::unordered_map<std::uint32_t, SceneNodeId> object_to_node_;
    // compact array of active object-backed scene nodes used during BVH build
    std::vector<SceneNodeId> spatial_object_nodes_;
    // flat BVH storage so traversal can recurse by node index
    std::vector<BvhNode> bvh_nodes_;
};

#endif
