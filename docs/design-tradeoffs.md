---
layout: default
title: Design Tradeoffs
---

# Design Tradeoffs

This page captures the main design decisions in the current engine.

## 1. Thin Public API Over Shared Runtime State

Choice:

- keep `Engine` as a narrow facade
- keep mutable runtime state in `utility`

Why:

- simple assignment-scale API
- one source of truth for objects, delta time, and collision registration

Tradeoff:

- easier to reason about than a larger service graph
- less modular than a fuller instance-based engine runtime

## 2. Virtual `GameObject` Base Class

Choice:

- store objects as `std::unique_ptr<GameObject>`

Why:

- supports per-type private state
- keeps update logic encapsulated in object types

Tradeoff:

- virtual dispatch and heap allocation cost
- less cache-friendly than a dense ECS-style layout

## 3. Rotation-Only Skeletal Animation

Choice:

- implement the minimum rotation-only armature system described in `NOTES.md`

Why:

- directly satisfies the required forward-kinematics path
- keeps runtime complexity low

Tradeoff:

- no animated bone translation or scaling
- no blending or layered animation yet

## 4. Separate Mesh And Animation Asset Files

Choice:

- keep rigged mesh data in `.meshbin`
- keep action clips in `.animbin`

Why:

- adding an animation does not require re-exporting the mesh
- clips stay reusable for compatible rigs

Tradeoff:

- runtime has to discover and load sidecars
- asset management is slightly more manual than a single monolithic format

## 5. Reusing `SceneGraph` For Collision Broad Phase

Choice:

- use a utility-owned `SceneGraph` for broad-phase collision candidate generation

Why:

- avoids maintaining a second completely separate BVH implementation
- reuses the existing query and rebuild logic already tested in Objective E

Tradeoff:

- the collision broad phase stores AABB-centered spheres, not the exact render transforms
- the same class now serves two different engine roles, which raises the documentation burden

## 6. Triangular Collision Response Table

Choice:

- store one callback entry per unordered type pair

Why:

- matches the assignment note that `X/Y` and `Y/X` are the same storage problem
- removes the duplicated symmetric matrix storage

Tradeoff:

- dispatch needs a little extra bookkeeping to preserve callback argument order

## 7. Deferred Collision Callback Dispatch

Choice:

- collect pending collision events first
- invoke callbacks after traversal

Why:

- callbacks can mutate runtime state safely
- object destruction during one callback does not invalidate traversal

Tradeoff:

- callback effects are deferred until after candidate collection completes
- slightly more bookkeeping than immediate invocation
