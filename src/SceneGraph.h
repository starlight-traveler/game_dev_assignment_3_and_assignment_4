/**
 * @file SceneGraph.h
 * @brief scene tree plus bvh broad phase used for world transforms and culling
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
 * @brief integer id used to refer to one scene node
 */
using SceneNodeId = std::uint32_t;

/**
 * @brief one node inside the transform tree
 *
 * each node can optionally point at a game object
 * and each node can have children
 * the local transform is relative to the parent
 * the world transform is the final accumulated transform after propagation
 */
struct SceneNode {
    // stable integer id for this slot
    SceneNodeId id;
    // parent id used for upward links and cycle checks
    SceneNodeId parent;
    // child ids used for recursive world transform updates
    std::vector<SceneNodeId> children;
    // optional game object id if this node represents a real object
    std::optional<std::uint32_t> object_reference;
    // transform relative to the parent node
    glm::mat4 local_transform;
    // fully accumulated transform in world space
    glm::mat4 world_transform;
    // simple broad phase sphere radius for this object
    float bounding_radius;
    // recycled inactive nodes stay in storage but are skipped
    bool active;
};

/**
 * @brief manages hierarchical transforms and a bvh over active object nodes
 *
 * this class does two connected jobs
 *
 * first it acts like a normal scene graph
 * child nodes inherit movement from parent nodes
 * so moving a parent automatically moves the whole subtree in world space
 *
 * second it builds a bvh
 * bvh means bounding volume hierarchy
 * that is just a binary tree of bounding boxes
 * each internal node covers a large region
 * each leaf covers a smaller region and stores a short list of objects
 *
 * the important bvh idea is prune early
 * instead of testing every object every frame
 * we test a large parent box first
 * if the query misses that box we skip every object under it
 * that is why this structure gets much cheaper than a flat scan as worlds spread out
 */
class SceneGraph {
public:
    /**
     * @brief creates the synthetic root node and default bvh settings
     */
    SceneGraph();

    /**
     * @brief changes how many objects are allowed in one leaf before splitting
     * @param max_leaf_objects positive leaf capacity
     */
    void setMaxLeafObjects(std::size_t max_leaf_objects);

    /**
     * @brief returns the current leaf capacity setting
     * @return bvh leaf capacity
     */
    std::size_t maxLeafObjects() const;

    /**
     * @brief returns the permanent root node id
     * @return root id
     */
    SceneNodeId rootNodeId() const;

    /**
     * @brief creates or reuses one node under a parent
     * @param parent parent node id
     * @param object_reference game object id if this node maps to a real object
     * @param local_transform transform relative to the parent
     * @param bounding_radius simple sphere radius used by the bvh
     * @return created node id
     */
    SceneNodeId createNode(SceneNodeId parent,
                           std::optional<std::uint32_t> object_reference,
                           const glm::mat4& local_transform,
                           float bounding_radius);

    /**
     * @brief reparents one node under another node
     * @param child node to move
     * @param new_parent new parent node
     * @return true when the tree remains valid after the move
     */
    bool setParent(SceneNodeId child, SceneNodeId new_parent);

    /**
     * @brief reparents using game object ids instead of raw node ids
     * @param child_object_reference child object id
     * @param parent_object_reference parent object id or nullopt for root
     * @return true when the move succeeds
     */
    bool setParentByObject(std::uint32_t child_object_reference,
                           std::optional<std::uint32_t> parent_object_reference);

    /**
     * @brief removes the node mapped to one object id
     * @param object_reference object id
     * @return true when a node was found and removed
     */
    bool removeNodeByObject(std::uint32_t object_reference);

    /**
     * @brief replaces the local transform for one object backed node
     * @param object_reference object id
     * @param local_transform new local transform
     * @return true when the node exists
     */
    bool setLocalTransformByObject(std::uint32_t object_reference, const glm::mat4& local_transform);

    /**
     * @brief changes the broad phase sphere radius for one object
     * @param object_reference object id
     * @param bounding_radius new sphere radius
     * @return true when the node exists
     */
    bool setBoundingRadiusByObject(std::uint32_t object_reference, float bounding_radius);

    /**
     * @brief recomputes world transforms for the whole tree
     */
    void updateWorldTransforms();

    /**
     * @brief rebuilds the entire bvh from active object nodes
     *
     * rebuild means
     * collect all active object backed nodes
     * compute one simple aabb from each nodes world position and radius
     * recursively split the list into a binary tree
     *
     * for this assignment a full rebuild is simpler than trying to update the tree incrementally
     */
    void rebuildSpatialIndex();

    /**
     * @brief convenience wrapper that fills a render queue through radius culling
     * @param render_queue output object ids
     * @param camera_position camera center in world space
     * @param cull_radius radius around the camera
     *
     * this is just a render flavored name for the same bvh radius query
     */
    void render(std::vector<std::uint32_t>& render_queue,
                const glm::vec3& camera_position,
                float cull_radius) const;

    /**
     * @brief finds object ids whose xz footprint overlaps a circle
     * @param out_objects output list that gets cleared first
     * @param center query center in world space
     * @param radius query radius
     */
    void queryRadius(std::vector<std::uint32_t>& out_objects,
                     const glm::vec3& center,
                     float radius) const;

    /**
     * @brief finds object ids whose broad phase boxes overlap an xz aabb
     * @param out_objects output list that gets cleared first
     * @param min_xz minimum xz corner
     * @param max_xz maximum xz corner
     */
    void queryAabb(std::vector<std::uint32_t>& out_objects,
                   const glm::vec2& min_xz,
                   const glm::vec2& max_xz) const;

    /**
     * @brief returns the current world transform for one object id
     * @param object_reference object id
     * @return world transform or identity if missing
     */
    glm::mat4 worldTransformForObject(std::uint32_t object_reference) const;

    /**
     * @brief returns how many active object backed nodes are tracked
     * @return active object count
     */
    std::size_t activeObjectCount() const;

private:
    /**
     * @brief one node in the flat stored bvh
     *
     * this is not a scene node
     * this is just a spatial helper structure
     *
     * if is_leaf is true
     * start and count describe a contiguous slice inside spatial_object_nodes_
     *
     * if is_leaf is false
     * left_child and right_child point to two child bvh nodes in bvh_nodes_
     */
    struct BvhNode {
        // box that encloses either the leaf objects or both child subtrees
        glm::vec3 min_bounds;
        glm::vec3 max_bounds;
        // valid only for internal nodes
        std::uint32_t left_child;
        std::uint32_t right_child;
        // valid only for leaf nodes
        std::size_t start;
        std::size_t count;
        // tells whether this node stores direct objects or child pointers
        bool is_leaf;
    };

    /**
     * @brief recursive helper for world transform propagation
     * @param node_id node to update
     * @param parent_world already accumulated parent transform
     */
    void updateWorldRecursive(SceneNodeId node_id, const glm::mat4& parent_world);

    /**
     * @brief recursively removes one node and its whole subtree
     * @param node_id node to remove
     */
    void removeNodeRecursive(SceneNodeId node_id);

    /**
     * @brief checks whether a node id currently refers to a live slot
     * @param node_id candidate node id
     * @return true when in range and active
     */
    bool isNodeActive(SceneNodeId node_id) const;

    /**
     * @brief checks whether one node appears in the parent chain of another
     * @param ancestor possible ancestor
     * @param node node being tested
     * @return true when ancestor is on the path to the root
     */
    bool isAncestor(SceneNodeId ancestor, SceneNodeId node) const;

    /**
     * @brief extracts the world position from one nodes transform
     * @param node_id node to inspect
     * @return translation in world space
     */
    glm::vec3 worldPositionForNode(SceneNodeId node_id) const;

    /**
     * @brief turns one nodes sphere radius into a simple aabb
     * @param node_id node to inspect
     * @return min and max world bounds
     */
    std::pair<glm::vec3, glm::vec3> boundsForNode(SceneNodeId node_id) const;

    /**
     * @brief computes the full object bounds over one contiguous range
     * @param start inclusive begin index in spatial_object_nodes_
     * @param end exclusive end index in spatial_object_nodes_
     * @return min and max world bounds
     *
     * this is the actual box stored on the bvh node
     * it must contain every object assigned to that subtree
     */
    std::pair<glm::vec3, glm::vec3> computeRangeBounds(std::size_t start, std::size_t end) const;

    /**
     * @brief computes bounds over object centers only for split decisions
     * @param start inclusive begin index in spatial_object_nodes_
     * @param end exclusive end index in spatial_object_nodes_
     * @return min and max centroid bounds
     *
     * this is not the same as object volume bounds
     * centroid bounds only help us decide which axis has the widest spread
     */
    std::pair<glm::vec3, glm::vec3> computeCentroidBounds(std::size_t start, std::size_t end) const;

    /**
     * @brief builds one bvh subtree over a contiguous object range
     * @param start inclusive begin index in spatial_object_nodes_
     * @param end exclusive end index in spatial_object_nodes_
     * @return index of the new bvh node inside bvh_nodes_
     *
     * the builder uses a median split on the widest centroid axis
     * that keeps the tree fairly balanced without a complicated cost model
     */
    std::uint32_t buildBvhRecursive(std::size_t start, std::size_t end);

    /**
     * @brief recursive helper for circular xz bvh queries
     * @param out_objects output object ids
     * @param bvh_node_index current bvh node index
     * @param center query center
     * @param radius query radius
     *
     * this is prune first traversal
     * if the parent box misses the circle we skip the whole subtree immediately
     */
    void queryRadiusRecursive(std::vector<std::uint32_t>& out_objects,
                              std::uint32_t bvh_node_index,
                              const glm::vec3& center,
                              float radius) const;

    /**
     * @brief recursive helper for xz box queries
     * @param out_objects output object ids
     * @param bvh_node_index current bvh node index
     * @param min_xz minimum xz corner
     * @param max_xz maximum xz corner
     *
     * same broad phase idea as the radius query
     * parent miss means whole subtree miss
     */
    void queryAabbRecursive(std::vector<std::uint32_t>& out_objects,
                            std::uint32_t bvh_node_index,
                            const glm::vec2& min_xz,
                            const glm::vec2& max_xz) const;

    // maximum number of objects allowed in one leaf before recursion tries to split
    std::size_t max_leaf_objects_;
    // dense storage for every scene node including inactive recycled slots
    std::vector<SceneNode> nodes_;
    // free list of recycled node ids so node storage does not grow forever
    std::vector<SceneNodeId> free_node_ids_;
    // map from game object id to scene node id for fast lookups from gameplay code
    std::unordered_map<std::uint32_t, SceneNodeId> object_to_node_;
    // flat array of active object backed scene node ids used during bvh construction
    std::vector<SceneNodeId> spatial_object_nodes_;
    // flat bvh storage where child links are just indices into this vector
    std::vector<BvhNode> bvh_nodes_;
};

#endif
