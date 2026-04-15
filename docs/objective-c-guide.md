---
layout: default
title: Objective C Guide
---

# Objective C Guide

Objective C in the current codebase is the spatial graph and event-driven collision system.

## Status

The current implementation now matches the missing pieces that had been absent before:

- broad-phase collision candidate generation is BVH-backed instead of a flat `N^2` scan
- collision responses are stored in a reduced triangular table instead of a full symmetric matrix
- callback argument order still follows the registration order even though storage is triangular

This system is intentionally a broad-phase plus callback-dispatch layer. It is not a full rigid-body physics solver.

## Main Files

- `src/Utility.cpp`
  Owns active objects, the collision response table, the collision broad phase, and the dispatch loop.
- `src/SceneGraph.h`
- `src/SceneGraph.cpp`
  Provide the BVH-backed spatial graph used by the collision broad phase.
- `src/GameObject.h`
- `src/GameObject.cpp`
  Provide the world-space AABB data consumed by broad phase and narrow phase.
- `src/Engine.h`
- `src/Engine.cpp`
  Expose the public API for collision types and callback registration.
- `tests/ObjectiveBC_test.cpp`
  Verifies callback routing, order preservation, and same-type dispatch.

## Broad-Phase Design

The collision system uses a utility-owned `SceneGraph` instance as a spatial graph dedicated to collision broad phase.

Each frame:

1. All `GameObject` instances update.
2. Their current world-space AABBs are converted into AABB-centered bounding spheres.
3. Those spheres are synchronized into the broad-phase `SceneGraph`.
4. The `SceneGraph` rebuilds its BVH.
5. Each collidable object queries that BVH with its XZ AABB footprint.
6. Only returned candidates continue to the exact 3D AABB overlap check.

That replaces the older flat pairwise scan in the utility layer.

## Event-Driven Response Table

The callback table is now stored as triangular compressed storage over unordered type pairs.

That means:

- only one entry exists for the pair `(type_a, type_b)`
- `(type_a, type_b)` and `(type_b, type_a)` share the same storage slot
- the stored entry keeps the original registration order so the callback still receives its arguments as `(type_a_object, type_b_object)`

This matches the assignment note that the pair table only needs the upper or lower triangle, not the full duplicated matrix.

## Narrow-Phase And Deferred Dispatch

The BVH query only proposes candidates.

The exact collision decision still comes from the full 3D AABB overlap test:

- `A.min <= B.max`
- `A.max >= B.min`

Callbacks are not invoked immediately during candidate collection. Instead, the engine stores pending events first and dispatches them afterward. That keeps iteration stable even if a callback destroys an object or changes object state.

## Objective C Flowchart

The required flowchart deliverable lives on its own page:

- [Collision Pipeline Flowchart](./collision-pipeline-flow.html)

## Verification

The current verification points are:

- `tests/ObjectiveBC_test.cpp`
  Confirms collision callback dispatch for different-type and same-type pairs.
- `tests/ObjectiveE_test.cpp`
  Confirms the underlying `SceneGraph` supports BVH rebuilds and spatial queries.

Together those tests cover the callback layer and the spatial graph that feeds it.
