---
layout: default
title: Objective E Guide
---

# Objective E Guide

Objective E in the Assignment 3 handout is the demonstration objective. The engine needs to show that the new simulation-stage pieces actually work together in the runtime order:

1. update time
2. update `GameObject` state
3. run collision tests and event responses
4. render graphics

## Current Demonstration Path

The current repository demonstrates that pipeline through a mix of executable runtime code and focused tests.

## Where The Objective E Story Lives

- `src/main.cpp`
  Owns the main frame loop and render submission path.
- `src/Engine.cpp`
  Exposes the assignment-facing update and control API.
- `src/Utility.cpp`
  Executes per-frame object updates, animation updates, broad-phase collision, and deferred callback dispatch.
- `tests/ObjectiveA_test.cpp`
  Demonstrates kinematics and automatic AABB refresh.
- `tests/ObjectiveBC_test.cpp`
  Demonstrates forward-kinematics animation and event-driven collision callbacks.
- `tests/ObjectiveE_test.cpp`
  Demonstrates the BVH-backed `SceneGraph` operations that support spatial queries.

## Runtime Order In The Current Engine

The implemented runtime story is:

```text
[frame delta stored in utility]
        |
        v
[updateActiveGameObjects]
        |
        v
[each GameObject updates gameplay state]
        |
        v
[animation playback updates skin matrices]
        |
        v
[collision broad phase rebuilds and queries BVH]
        |
        v
[exact 3D AABB overlap tests]
        |
        v
[registered collision callbacks are dispatched]
        |
        v
[main.cpp builds render commands and draws]
```

That is the DES-style flow the assignment asked to see.

## What The Tests Demonstrate

`tests/ObjectiveA_test.cpp` demonstrates Objective A feeding the simulation stage:

- linear and angular integration change object state correctly
- acceleration and impulses update velocity state
- AABB bounds refresh automatically after translation and rotation

`tests/ObjectiveBC_test.cpp` demonstrates the two major runtime interactions added for Assignment 3:

- forward-kinematics animation updates skinned bone matrices over time
- overlapping broad bounds invoke a user-provided collision callback
- collision callbacks can mutate gameplay state by applying impulses

`tests/ObjectiveE_test.cpp` demonstrates the spatial structure that the runtime now depends on:

- parent-child transform propagation
- BVH rebuilds
- radius and AABB spatial queries
- object removal and query correctness after rebuild

## What This Means For Objective E

The current branch does show the simulation-stage additions working together for Assignment 3:

- time is tracked once and reused through the engine API
- `GameObject` state changes update transforms and bounds
- the spatial graph reduces broad-phase candidate work
- event-driven collision callbacks are routed by type pair
- rendering consumes the resulting object transforms and optional skin matrices

The only Assignment 3 feature still missing from that end-to-end story is Objective D's convex narrow-phase algorithm.

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
