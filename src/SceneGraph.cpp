#include "SceneGraph.h"

#include <algorithm>
#include <cmath>

namespace {
/**
 * @brief pulls the translation part out of a transform matrix
 * @param transform matrix to inspect
 * @return translation vector
 */
glm::vec3 matrix_translation(const glm::mat4& transform) {
    return glm::vec3(transform[3][0], transform[3][1], transform[3][2]);
}

/**
 * @brief turns a center plus radius into an axis aligned box
 * @param transform world transform containing the center
 * @param radius sphere radius
 * @return min and max world bounds
 */
std::pair<glm::vec3, glm::vec3> sphere_bounds(const glm::mat4& transform, float radius) {
    // extract the world-space center of the object from the matrix translation column
    const glm::vec3 center = matrix_translation(transform);
    // turn the scalar sphere radius into a per-axis extent for AABB expansion
    const glm::vec3 extent(std::max(radius, 0.0f));
    // min corner is center minus the extent on each axis
    // max corner is center plus the extent on each axis
    return std::make_pair(center - extent, center + extent);
}

/**
 * @brief computes squared xz distance from a point to a box
 * @param point query point
 * @param min_bounds minimum box corner
 * @param max_bounds maximum box corner
 * @return squared distance in xz
 *
 * this is used for the circle style bvh query
 * if the squared distance is greater than radius squared then the whole box misses
 */
float squared_distance_to_aabb_xz(const glm::vec3& point,
                                  const glm::vec3& min_bounds,
                                  const glm::vec3& max_bounds) {
    // dx stores horizontal separation in X between the point and the box
    float dx = 0.0f;
    if (point.x < min_bounds.x) {
        // point lies left of the box, so measure distance to the left face
        dx = min_bounds.x - point.x;
    } else if (point.x > max_bounds.x) {
        // point lies right of the box, so measure distance to the right face
        dx = point.x - max_bounds.x;
    }

    // dz stores horizontal separation in Z between the point and the box
    float dz = 0.0f;
    if (point.z < min_bounds.z) {
        // point lies in front of the box on Z, so measure to the near face
        dz = min_bounds.z - point.z;
    } else if (point.z > max_bounds.z) {
        // point lies behind the box on Z, so measure to the far face
        dz = point.z - max_bounds.z;
    }

    // squared distance avoids a square root and is enough for radius comparisons
    return dx * dx + dz * dz;
}

/**
 * @brief checks whether two boxes overlap in the xz plane
 * @param min_a minimum corner for the first box
 * @param max_a maximum corner for the first box
 * @param min_b minimum corner for the second box
 * @param max_b maximum corner for the second box
 * @return true when their xz projections overlap
 */
bool overlaps_aabb_xz(const glm::vec3& min_a,
                      const glm::vec3& max_a,
                      const glm::vec3& min_b,
                      const glm::vec3& max_b) {
    // overlap exists only when the projections overlap on both X and Z
    return (max_a.x >= min_b.x) &&
           (min_a.x <= max_b.x) &&
           (max_a.z >= min_b.z) &&
           (min_a.z <= max_b.z);
}
}  // namespace

SceneGraph::SceneGraph()
    : max_leaf_objects_(2),
      nodes_(),
      free_node_ids_(),
      object_to_node_(),
      spatial_object_nodes_(),
      bvh_nodes_() {
    // node 0 is a permanent synthetic root
    // that means every real subtree always has a valid parent anchor
    SceneNode root{};
    root.id = 0;
    root.parent = 0;
    root.children.clear();
    root.object_reference = std::nullopt;
    root.local_transform = glm::mat4(1.0f);
    root.world_transform = glm::mat4(1.0f);
    root.bounding_radius = 0.0f;
    root.active = true;
    nodes_.push_back(root);
}

SceneNodeId SceneGraph::rootNodeId() const {
    // root is always node zero by construction
    return 0;
}

void SceneGraph::setMaxLeafObjects(std::size_t max_leaf_objects) {
    if (max_leaf_objects == 0) {
        return;
    }
    // smaller leaves means deeper trees and fewer objects per leaf
    // larger leaves means shallower trees and more direct object checks per leaf
    max_leaf_objects_ = max_leaf_objects;
    rebuildSpatialIndex();
}

std::size_t SceneGraph::maxLeafObjects() const {
    return max_leaf_objects_;
}

SceneNodeId SceneGraph::createNode(SceneNodeId parent,
                                   std::optional<std::uint32_t> object_reference,
                                   const glm::mat4& local_transform,
                                   float bounding_radius) {
    if (!isNodeActive(parent)) {
        // invalid parents collapse to the root so we never create dangling hierarchy links
        parent = rootNodeId();
    }

    if (object_reference.has_value()) {
        const auto existing_it = object_to_node_.find(object_reference.value());
        if (existing_it != object_to_node_.end() && isNodeActive(existing_it->second)) {
            const SceneNodeId existing_id = existing_it->second;
            // object ids stay unique
            // so asking to create an already mapped object really means update that old node
            nodes_[existing_id].local_transform = local_transform;
            nodes_[existing_id].bounding_radius = std::max(0.0f, bounding_radius);
            setParent(existing_id, parent);
            return existing_id;
        }
    }

    SceneNode node{};
    if (!free_node_ids_.empty()) {
        // recycle a dead slot instead of always growing the vector
        node.id = free_node_ids_.back();
        free_node_ids_.pop_back();
    } else {
        node.id = static_cast<SceneNodeId>(nodes_.size());
    }
    node.parent = parent;
    node.children.clear();
    node.object_reference = object_reference;
    node.local_transform = local_transform;
    node.world_transform = local_transform;
    node.bounding_radius = std::max(0.0f, bounding_radius);
    node.active = true;
    if (node.id < nodes_.size()) {
        // recycled slot already exists so overwrite it in place
        nodes_[node.id] = node;
    } else {
        nodes_.push_back(node);
    }
    // hierarchy traversal walks parent to children
    // so the parent needs to store this child id
    nodes_[parent].children.push_back(node.id);

    if (object_reference.has_value()) {
        // this map is what lets gameplay code jump straight from object id to node id
        object_to_node_[object_reference.value()] = node.id;
    }
    return node.id;
}

bool SceneGraph::setParent(SceneNodeId child, SceneNodeId new_parent) {
    if (!isNodeActive(child) || child == rootNodeId()) {
        // root stays fixed and inactive nodes are not meaningful to move
        return false;
    }
    if (!isNodeActive(new_parent)) {
        // bad targets fall back to the root instead of leaving the tree broken
        new_parent = rootNodeId();
    }
    if (child == new_parent) {
        return false;
    }
    if (isAncestor(child, new_parent)) {
        // if child is already above new_parent then linking them would create a cycle
        return false;
    }

    SceneNode& child_node = nodes_[child];
    if (child_node.parent == new_parent) {
        // already parented correctly
        return true;
    }

    if (isNodeActive(child_node.parent)) {
        SceneNode& old_parent = nodes_[child_node.parent];
        // remove the old down link before writing the new one
        old_parent.children.erase(
            std::remove(old_parent.children.begin(), old_parent.children.end(), child),
            old_parent.children.end());
    }

    child_node.parent = new_parent;
    // add the new down link so recursive transform propagation sees this node under the new parent
    nodes_[new_parent].children.push_back(child);
    return true;
}

bool SceneGraph::setParentByObject(std::uint32_t child_object_reference,
                                   std::optional<std::uint32_t> parent_object_reference) {
    const auto child_it = object_to_node_.find(child_object_reference);
    if (child_it == object_to_node_.end() || !isNodeActive(child_it->second)) {
        return false;
    }

    SceneNodeId parent_id = rootNodeId();
    if (parent_object_reference.has_value()) {
        const auto parent_it = object_to_node_.find(parent_object_reference.value());
        if (parent_it == object_to_node_.end() || !isNodeActive(parent_it->second)) {
            return false;
        }
        parent_id = parent_it->second;
    }
    return setParent(child_it->second, parent_id);
}

bool SceneGraph::removeNodeByObject(std::uint32_t object_reference) {
    const auto it = object_to_node_.find(object_reference);
    if (it == object_to_node_.end()) {
        return false;
    }
    // deleting a parent without deleting the children would leave a broken subtree
    removeNodeRecursive(it->second);
    return true;
}

bool SceneGraph::setLocalTransformByObject(std::uint32_t object_reference, const glm::mat4& local_transform) {
    const auto it = object_to_node_.find(object_reference);
    if (it == object_to_node_.end()) {
        return false;
    }
    const SceneNodeId node_id = it->second;
    if (node_id >= nodes_.size() || !nodes_[node_id].active) {
        return false;
    }
    // this only changes the local value
    // the final world matrix will be recomputed during the next propagation pass
    nodes_[node_id].local_transform = local_transform;
    return true;
}

bool SceneGraph::setBoundingRadiusByObject(std::uint32_t object_reference, float bounding_radius) {
    const auto it = object_to_node_.find(object_reference);
    if (it == object_to_node_.end()) {
        return false;
    }
    const SceneNodeId node_id = it->second;
    if (!isNodeActive(node_id)) {
        return false;
    }
    // bvh bounds come from world position plus this sphere radius
    nodes_[node_id].bounding_radius = std::max(0.0f, bounding_radius);
    return true;
}

void SceneGraph::updateWorldTransforms() {
    // start recursion from the synthetic root with identity as the incoming parent transform
    updateWorldRecursive(rootNodeId(), glm::mat4(1.0f));
}

void SceneGraph::rebuildSpatialIndex() {
    // full rebuild is easy to reason about
    // gather active object nodes into one flat list then recursively split that list into a bvh
    // parent boxes become coarse regions
    // child boxes become finer regions inside them
    // clear the previous frame's flat object list
    spatial_object_nodes_.clear();
    // clear the previous frame's BVH nodes
    bvh_nodes_.clear();
    for (const SceneNode& node : nodes_) {
        if (!node.active || !node.object_reference.has_value()) {
            // helper nodes and inactive nodes do not represent visible spatial objects
            continue;
        }
        // store just the scene node id
        // the real node data stays in nodes_
        spatial_object_nodes_.push_back(node.id);
    }

    if (spatial_object_nodes_.empty()) {
        // no objects means no tree
        return;
    }

    // reserve enough room so recursion does not keep reallocating as the flat bvh grows
    bvh_nodes_.reserve(spatial_object_nodes_.size() * 2);
    // the first recursive build call creates the root node at index zero
    buildBvhRecursive(0, spatial_object_nodes_.size());
}

void SceneGraph::render(std::vector<std::uint32_t>& render_queue,
                        const glm::vec3& camera_position,
                        float cull_radius) const {
    queryRadius(render_queue, camera_position, cull_radius);
}

void SceneGraph::queryRadius(std::vector<std::uint32_t>& out_objects,
                             const glm::vec3& center,
                             float radius) const {
    out_objects.clear();
    if (bvh_nodes_.empty() || radius < 0.0f) {
        return;
    }
    queryRadiusRecursive(out_objects, 0, center, radius);
}

void SceneGraph::queryAabb(std::vector<std::uint32_t>& out_objects,
                           const glm::vec2& min_xz,
                           const glm::vec2& max_xz) const {
    out_objects.clear();
    if (bvh_nodes_.empty()) {
        return;
    }
    queryAabbRecursive(out_objects, 0, min_xz, max_xz);
}

glm::mat4 SceneGraph::worldTransformForObject(std::uint32_t object_reference) const {
    const auto it = object_to_node_.find(object_reference);
    if (it == object_to_node_.end()) {
        // missing objects get identity as a safe fallback
        return glm::mat4(1.0f);
    }
    const SceneNodeId node_id = it->second;
    if (node_id >= nodes_.size() || !nodes_[node_id].active) {
        return glm::mat4(1.0f);
    }
    return nodes_[node_id].world_transform;
}

std::size_t SceneGraph::activeObjectCount() const {
    // only active object backed nodes stay in this lookup map
    return object_to_node_.size();
}

void SceneGraph::updateWorldRecursive(SceneNodeId node_id, const glm::mat4& parent_world) {
    if (node_id >= nodes_.size()) {
        return;
    }

    SceneNode& node = nodes_[node_id];
    if (!node.active) {
        return;
    }

    // this is the core scene graph rule
    // world transform equals parent world times local transform
    node.world_transform = parent_world * node.local_transform;
    for (SceneNodeId child_id : node.children) {
        // pass the newly computed world transform down to each child
        updateWorldRecursive(child_id, node.world_transform);
    }
}

void SceneGraph::removeNodeRecursive(SceneNodeId node_id) {
    if (node_id >= nodes_.size() || node_id == rootNodeId()) {
        return;
    }

    SceneNode& node = nodes_[node_id];
    if (!node.active) {
        return;
    }

    // copy child ids first because recursive calls will mutate the original children vector
    const std::vector<SceneNodeId> child_ids = node.children;
    for (SceneNodeId child_id : child_ids) {
        removeNodeRecursive(child_id);
    }
    node.children.clear();

    if (isNodeActive(node.parent)) {
        SceneNode& parent = nodes_[node.parent];
        // remove this node id from the parent child list
        parent.children.erase(
            std::remove(parent.children.begin(), parent.children.end(), node_id),
            parent.children.end());
    }

    if (node.object_reference.has_value()) {
        // also remove the gameplay lookup entry so no stale object id points here
        object_to_node_.erase(node.object_reference.value());
    }
    node.parent = rootNodeId();
    node.active = false;
    node.local_transform = glm::mat4(1.0f);
    node.world_transform = glm::mat4(1.0f);
    node.bounding_radius = 0.0f;
    node.object_reference = std::nullopt;
    // save this slot for later reuse
    free_node_ids_.push_back(node_id);
}

bool SceneGraph::isNodeActive(SceneNodeId node_id) const {
    return node_id < nodes_.size() && nodes_[node_id].active;
}

bool SceneGraph::isAncestor(SceneNodeId ancestor, SceneNodeId node) const {
    if (!isNodeActive(ancestor) || !isNodeActive(node)) {
        return false;
    }

    SceneNodeId cursor = node;
    while (isNodeActive(cursor)) {
        if (cursor == ancestor) {
            return true;
        }
        const SceneNodeId parent = nodes_[cursor].parent;
        if (parent == cursor) {
            // once parent points to self we reached the synthetic root
            break;
        }
        if (!isNodeActive(parent)) {
            break;
        }
        cursor = parent;
    }
    return false;
}

glm::vec3 SceneGraph::worldPositionForNode(SceneNodeId node_id) const {
    if (!isNodeActive(node_id)) {
        // safe fallback for invalid ids
        return glm::vec3(0.0f);
    }
    // world position is stored in the translation column of the world matrix
    return matrix_translation(nodes_[node_id].world_transform);
}

std::pair<glm::vec3, glm::vec3> SceneGraph::boundsForNode(SceneNodeId node_id) const {
    if (!isNodeActive(node_id)) {
        // invalid ids return an empty safe box
        return std::make_pair(glm::vec3(0.0f), glm::vec3(0.0f));
    }
    // the broad phase uses a sphere per object because it is cheap
    // then converts that sphere into an axis aligned box because bvh nodes store boxes
    return sphere_bounds(nodes_[node_id].world_transform, nodes_[node_id].bounding_radius);
}

std::pair<glm::vec3, glm::vec3> SceneGraph::computeRangeBounds(std::size_t start, std::size_t end) const {
    if (start >= end || end > spatial_object_nodes_.size()) {
        // safe fallback for bad ranges
        return std::make_pair(glm::vec3(0.0f), glm::vec3(0.0f));
    }

    // start from the first object bounds
    // then expand until the box contains the whole range
    const auto first_bounds = boundsForNode(spatial_object_nodes_[start]);
    // initialize the running minimum corner from the first object
    glm::vec3 min_bounds = first_bounds.first;
    // initialize the running maximum corner from the first object
    glm::vec3 max_bounds = first_bounds.second;
    for (std::size_t index = start + 1; index < end; ++index) {
        // read the next object box
        const auto object_bounds = boundsForNode(spatial_object_nodes_[index]);
        // expand the minimum corner component-wise
        min_bounds = glm::min(min_bounds, object_bounds.first);
        // expand the maximum corner component-wise
        max_bounds = glm::max(max_bounds, object_bounds.second);
    }
    // final box now encloses every object assigned to this subtree candidate
    return std::make_pair(min_bounds, max_bounds);
}

std::pair<glm::vec3, glm::vec3> SceneGraph::computeCentroidBounds(std::size_t start,
                                                                  std::size_t end) const {
    if (start >= end || end > spatial_object_nodes_.size()) {
        // safe fallback for bad ranges
        return std::make_pair(glm::vec3(0.0f), glm::vec3(0.0f));
    }

    // splitting should follow where object centers are spread out
    // so this uses centroids rather than full object volume
    glm::vec3 min_centroid = worldPositionForNode(spatial_object_nodes_[start]);
    // both min and max start from the same centroid
    glm::vec3 max_centroid = min_centroid;
    for (std::size_t index = start + 1; index < end; ++index) {
        // centroid is just the world position of the object center
        const glm::vec3 centroid = worldPositionForNode(spatial_object_nodes_[index]);
        // shrink the minimum centroid corner if needed
        min_centroid = glm::min(min_centroid, centroid);
        // grow the maximum centroid corner if needed
        max_centroid = glm::max(max_centroid, centroid);
    }
    // this measures center spread only and is used to choose a split axis
    return std::make_pair(min_centroid, max_centroid);
}

std::uint32_t SceneGraph::buildBvhRecursive(std::size_t start, std::size_t end) {
    // each recursive call owns a contiguous slice of spatial_object_nodes_
    // that slice is the full set of objects that must live under this subtree
    // first compute the box that encloses that whole slice
    const auto bounds = computeRangeBounds(start, end);
    // flat storage means the next push_back index becomes this node id
    const std::uint32_t node_index = static_cast<std::uint32_t>(bvh_nodes_.size());
    // write a provisional leaf first
    // if we later decide to split we will convert this same entry into an internal node
    bvh_nodes_.push_back(BvhNode{
        // region minimum
        bounds.first,
        // region maximum
        bounds.second,
        // child placeholders for now
        0,
        0,
        // leaf begin index into spatial_object_nodes_
        start,
        // leaf object count
        end - start,
        // default to leaf until a valid split is found
        true
    });

    // base case
    // if the slice is already small enough then stop splitting here
    if ((end - start) <= max_leaf_objects_) {
        return node_index;
    }

    // choose the axis where object centers are spread out the most
    // this is a simple heuristic for making two spatially different halves
    const auto centroid_bounds = computeCentroidBounds(start, end);
    // extent is the size of the centroid box on each axis
    const glm::vec3 extent = centroid_bounds.second - centroid_bounds.first;

    // default split axis is x
    int axis = 0;
    if (extent.y > extent.x && extent.y >= extent.z) {
        // use y if it spreads farther than x and at least as far as z
        axis = 1;
    } else if (extent.z > extent.x && extent.z >= extent.y) {
        // otherwise use z if it spreads the farthest
        axis = 2;
    }

    // if every centroid is almost on top of each other on that axis
    // a split would not create useful spatial separation
    if (extent[axis] <= 0.0001f) {
        return node_index;
    }

    // split around the median on the chosen axis
    // nth_element is useful here because we only need the middle partition not a full sort
    const std::size_t mid = start + ((end - start) / 2);
    std::nth_element(
        spatial_object_nodes_.begin() + static_cast<std::ptrdiff_t>(start),
        spatial_object_nodes_.begin() + static_cast<std::ptrdiff_t>(mid),
        spatial_object_nodes_.begin() + static_cast<std::ptrdiff_t>(end),
        [this, axis](SceneNodeId left, SceneNodeId right) {
            // compare only the selected centroid coordinate
            return worldPositionForNode(left)[axis] < worldPositionForNode(right)[axis];
        });

    // if one side ended up empty then keep this node as a leaf
    if (mid == start || mid == end) {
        return node_index;
    }

    // at this point the split is valid
    // convert this node from a leaf into an internal parent
    // internal nodes do not store direct objects
    // they only store two child indices and one large parent box
    bvh_nodes_[node_index].is_leaf = false;
    // left child covers the first half
    bvh_nodes_[node_index].left_child = buildBvhRecursive(start, mid);
    // right child covers the second half
    bvh_nodes_[node_index].right_child = buildBvhRecursive(mid, end);
    // leaf storage fields are meaningless on internal nodes so clear them
    bvh_nodes_[node_index].start = 0;
    bvh_nodes_[node_index].count = 0;
    return node_index;
}

void SceneGraph::queryRadiusRecursive(std::vector<std::uint32_t>& out_objects,
                                      std::uint32_t bvh_node_index,
                                      const glm::vec3& center,
                                      float radius) const {
    if (bvh_node_index >= bvh_nodes_.size()) {
        // defensive range check
        return;
    }

    // read the current bvh node from flat storage
    const BvhNode& bvh_node = bvh_nodes_[bvh_node_index];
    // this is the big bvh win
    // one cheap box test can skip an entire subtree of objects
    if (squared_distance_to_aabb_xz(center, bvh_node.min_bounds, bvh_node.max_bounds) >
        (radius * radius)) {
        return;
    }

    if (bvh_node.is_leaf) {
        // leaves finally test individual objects
        for (std::size_t offset = 0; offset < bvh_node.count; ++offset) {
            // recover the scene node id from the leaf slice
            const SceneNodeId node_id = spatial_object_nodes_[bvh_node.start + offset];
            if (!isNodeActive(node_id)) {
                // ignore stale ids just in case
                continue;
            }

            // fetch the full scene node so we can read object id and radius
            const SceneNode& node = nodes_[node_id];
            if (!node.object_reference.has_value()) {
                // helper nodes should not appear in the bvh result set
                continue;
            }

            // object center in world space
            const glm::vec3 world_position = worldPositionForNode(node_id);
            // queries operate in ground space so only x and z matter here
            const glm::vec2 delta_xz(world_position.x - center.x,
                                     world_position.z - center.z);
            // object radius expands the allowed distance so large objects still count
            const float max_distance = radius + node.bounding_radius;
            // compare squared values to avoid a square root
            if (glm::dot(delta_xz, delta_xz) <= (max_distance * max_distance)) {
                // output game object ids because callers care about objects not internal node ids
                out_objects.push_back(node.object_reference.value());
            }
        }
        // leaf work is done
        return;
    }

    // parent box overlapped so either child might also overlap
    queryRadiusRecursive(out_objects, bvh_node.left_child, center, radius);
    queryRadiusRecursive(out_objects, bvh_node.right_child, center, radius);
}

void SceneGraph::queryAabbRecursive(std::vector<std::uint32_t>& out_objects,
                                    std::uint32_t bvh_node_index,
                                    const glm::vec2& min_xz,
                                    const glm::vec2& max_xz) const {
    if (bvh_node_index >= bvh_nodes_.size()) {
        // defensive range check
        return;
    }

    // normalize the query corners so callers can pass them in any order
    const float min_x = std::min(min_xz.x, max_xz.x);
    const float min_z = std::min(min_xz.y, max_xz.y);
    const float max_x = std::max(min_xz.x, max_xz.x);
    const float max_z = std::max(min_xz.y, max_xz.y);

    // reuse the same 3d helper by embedding xz into vectors with a dummy y
    const glm::vec3 query_min(min_x, 0.0f, min_z);
    const glm::vec3 query_max(max_x, 0.0f, max_z);
    // read the current parent box
    const BvhNode& bvh_node = bvh_nodes_[bvh_node_index];
    // same prune first idea as the circle query
    // if the parent box misses then every child under it also misses
    if (!overlaps_aabb_xz(query_min, query_max, bvh_node.min_bounds, bvh_node.max_bounds)) {
        return;
    }

    if (bvh_node.is_leaf) {
        // inside a leaf we finally test each object box
        for (std::size_t offset = 0; offset < bvh_node.count; ++offset) {
            // recover one scene node id from the leaf slice
            const SceneNodeId node_id = spatial_object_nodes_[bvh_node.start + offset];
            if (!isNodeActive(node_id)) {
                // skip stale or dead nodes
                continue;
            }

            // fetch the full scene node
            const SceneNode& node = nodes_[node_id];
            if (!node.object_reference.has_value()) {
                // helper nodes are never valid object results
                continue;
            }

            // compare the object own box against the query box
            const auto object_bounds = boundsForNode(node_id);
            if (overlaps_aabb_xz(query_min, query_max, object_bounds.first, object_bounds.second)) {
                // output the game object id
                out_objects.push_back(node.object_reference.value());
            }
        }
        // leaf finished
        return;
    }

    // internal nodes only forward traversal to children
    queryAabbRecursive(out_objects, bvh_node.left_child, min_xz, max_xz);
    queryAabbRecursive(out_objects, bvh_node.right_child, min_xz, max_xz);
}
