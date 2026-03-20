---
layout: default
title: Objective E Guide
---

# Objective E Guide

## Short Answer

The current engine satisfies Objective E with

- a tree-like hierarchy for parent-child transforms
- an object reference back into engine-managed game objects
- a render-facing spatial query path
- a BVH as the chosen spatial structure
- rebuild-based synchronization from the object pool into the spatial structure

## Where Objective E Lives In The Code

The core implementation is in

- `src/SceneGraph.h`
- `src/SceneGraph.cpp`

It is integrated into the engine loop in

- `src/main.cpp`

It is tested in

- `tests/ObjectiveE_test.cpp`

## Step 1. The Engine Keeps SceneGraph Separate From GameObject Memory

This is one of the assignment's biggest conceptual requirements

The engine does **not** use the object pool itself as the scene graph

Instead, simulation and spatial organization are separate

That relationship is visible in the main loop

```cpp
scene_graph.setLocalTransformByObject(item.window_id,
                                      getModelForRenderElement(item.window_id));
scene_graph.updateWorldTransforms();
scene_graph.rebuildSpatialIndex();
```

Source: `src/main.cpp:500-510`

Interpretation

1. the object pool owns simulation state
2. the scene graph reads that state
3. the spatial structure is rebuilt from that state

This matches the assignment's idea that the scene graph is a representation of the memory pool in spatial form, not the memory pool itself

## Step 2. The Tree Structure Exists For Parent-Child Transform Inheritance

Each scene node stores parent and child links

```cpp
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
```

Source: `src/SceneGraph.h`

This satisfies the "at least one tree-like data structure" part of the assignment

The parent-child transform inheritance is implemented here

```cpp
node.world_transform = parent_world * node.local_transform;
for (SceneNodeId child_id : node.children) {
    updateWorldRecursive(child_id, node.world_transform);
}
```

Source: `src/SceneGraph.cpp:341-346`

That is the exact parent-to-child propagation the Objective E description asks for

## Step 3. The Chosen Spatial Structure Is A BVH

The assignment allowed multiple possible spatial structures

- uniform grid
- binary spatial partition
- octree
- k-d tree
- binary spatial partition

The current engine uses a **bounding volume hierarchy**

The intent is documented directly in the class comment

```cpp
 * 1. keep a parent-child transform tree so child nodes inherit parent motion
 * 2. build a bounding volume hierarchy over active object nodes for broad spatial queries
```

Source: `src/SceneGraph.h`

So the implementation choice is

- tree hierarchy for transforms
- BVH for spatial traversal

## Step 4. Object References Link SceneGraph Nodes Back To Engine Objects

Objective E said the node should hold some kind of object reference

The current code does that with

```cpp
std::optional<std::uint32_t> object_reference;
```

Source: `src/SceneGraph.h`

And the reverse lookup map is

```cpp
std::unordered_map<std::uint32_t, SceneNodeId> object_to_node_;
```

Source: `src/SceneGraph.h`

This gives the engine two-way coordination

- object id to scene node
- scene node back to object id

## Step 5. Node Creation Builds The Hierarchy

Nodes are inserted with a parent id, an optional object id, a local transform, and a bounding radius

```cpp
SceneNodeId SceneGraph::createNode(SceneNodeId parent,
                                   std::optional<std::uint32_t> object_reference,
                                   const glm::mat4& local_transform,
                                   float bounding_radius)
```

Source: `src/SceneGraph.cpp:112-115`

The important behavior is

- invalid parents fall back to the root
- object ids stay unique
- parent stores the child id
- the object map stores the object-to-node relationship

## Step 6. World Transforms Are Recomputed From The Root

The root is synthetic and always exists

```cpp
root.id = 0;
root.parent = 0;
root.local_transform = glm::mat4(1.0f);
root.world_transform = glm::mat4(1.0f);
```

Source: `src/SceneGraph.cpp:81-91`

Then every frame the scene graph starts from that root

```cpp
updateWorldRecursive(rootNodeId(), glm::mat4(1.0f));
```

Source: `src/SceneGraph.cpp:257-260`

This is why the engine can support nested transforms correctly

## Step 7. The Spatial Structure Is Rebuilt From Active Object Nodes

The BVH rebuild does three key things

```cpp
spatial_object_nodes_.clear();
bvh_nodes_.clear();
for (const SceneNode& node : nodes_) {
    if (!node.active || !node.object_reference.has_value()) {
        continue;
    }
    spatial_object_nodes_.push_back(node.id);
}
buildBvhRecursive(0, spatial_object_nodes_.size());
```

Source: `src/SceneGraph.cpp:262-285`

This matches the assignment's statement that rebuilding the structure can be better than reorganizing every engine-managed object directly

## Step 8. Each Object Uses A Simple Broad Bounding Volume

The current broad bound is a sphere radius stored on each node

```cpp
float bounding_radius;
```

Source: `src/SceneGraph.h`

That radius is expanded into an axis-aligned box for the BVH

```cpp
const glm::vec3 center = matrix_translation(transform);
const glm::vec3 extent(std::max(radius, 0.0f));
return std::make_pair(center - extent, center + extent);
```

Source: `src/SceneGraph.cpp:22-25`

This is a simple and fast broad-phase representation

## Step 9. The BVH Is Built Recursively

The recursive BVH builder computes bounds for a range of objects, chooses a split axis, partitions the range, and recurses

```cpp
const auto bounds = computeRangeBounds(start, end);
const auto centroid_bounds = computeCentroidBounds(start, end);
const glm::vec3 extent = centroid_bounds.second - centroid_bounds.first;
const std::size_t mid = start + ((end - start) / 2);
std::nth_element(...);
```

Source: `src/SceneGraph.cpp:465-512`

The important idea is that large spatial regions are split into smaller ones until the leaf capacity is small enough

## Step 10. Render Traversal Uses The BVH

The main render call is intentionally simple

```cpp
void SceneGraph::render(std::vector<std::uint32_t>& render_queue,
                        const glm::vec3& camera_position,
                        float cull_radius) const {
    queryRadius(render_queue, camera_position, cull_radius);
}
```

Source: `src/SceneGraph.cpp:287-291`

The actual broad-phase work is in `queryRadiusRecursive`

```cpp
if (squared_distance_to_aabb_xz(center, bvh_node.min_bounds, bvh_node.max_bounds) >
    (radius * radius)) {
    return;
}
```

Source: `src/SceneGraph.cpp:543-546`

This is the central acceleration idea

- test one big BVH node first
- reject entire subtrees when they cannot overlap the query
- only test individual objects once the traversal reaches a leaf

## Step 11. AABB Queries Also Exist

The assignment discussed spatial traversal more generally, not only rendering

The current engine therefore supports an additional box query

```cpp
void SceneGraph::queryAabb(std::vector<std::uint32_t>& out_objects,
                           const glm::vec2& min_xz,
                           const glm::vec2& max_xz) const
```

Source: `src/SceneGraph.cpp:303-311`

That is useful for future RTS-style systems such as

- selection rectangles
- broad collision checks
- region queries

## Step 12. Removal Supports Temporary Object Lifespans

Objective E specifically mentions temporary-lifespan objects needing to play nicely with the rest of the system

The current recursive removal path supports that

```cpp
bool SceneGraph::removeNodeByObject(std::uint32_t object_reference) {
    const auto it = object_to_node_.find(object_reference);
    if (it == object_to_node_.end()) {
        return false;
    }
    removeNodeRecursive(it->second);
    return true;
}
```

Source: `src/SceneGraph.cpp:219-226`

That means dynamic objects can be deleted from the hierarchy without rebuilding the entire engine object layer by hand

## Step 13. Objective E Test Coverage

The Objective E test checks exactly the two most important requirements

1. hierarchy behavior
2. spatial query behavior

The transform inheritance test creates a parent-child relationship and reparents it

The spatial test checks

- radius query
- AABB query
- removal behavior

That test lives in `tests/ObjectiveE_test.cpp`