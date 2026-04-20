---
layout: default
title: Home
---

# Assignment 3 and 4 Guide

This site documents the current version of the repo as it is meant to be shown for Assignment 3 and the deferred rendering part of Assignment 4

The main thing to look at is `assignment_1`

That executable is the main showcase scene for the current assignment work

## Assignment Status

- Objective A: implemented
- Objective B: implemented
- Objective C: implemented
- Objective D: implemented
- Objective E: implemented in the `assignment_1` showcase path
- Assignment 4 deferred rendering with render buffer management: implemented in the same showcase

## Recommended Reading Order

1. [Objective A Guide](./objective-a-guide.html)
2. [Objective B Report](./objective-b-report.html)
3. [Animation Controller Flowchart](./animation-controller-flow.html)
4. [Objective C Guide](./objective-c-guide.html)
5. [Collision Pipeline Flowchart](./collision-pipeline-flow.html)
6. [Objective D Guide](./objective-d-guide.html)
7. [Objective E Guide](./objective-e-guide.html)
8. [Assignment 4 Guide](./assignment-4-guide.html)
9. [Mathematics](./mathematics.html)
10. [Architecture](./architecture.html)
11. [Runtime Flow](./runtime-flow.html)

## What `assignment_1` Shows

The showcase is built so one short run can explain almost everything that matters

It shows

- several managed objects in one scene
- optional skinned animation when a `.meshbin` has an `.animbin` sidecar
- RTS style move commands and automatic movement
- broad phase and narrow phase collision work
- collision callbacks that cause visible reactions
- a scene graph and BVH query path feeding render submission
- deferred rendering with a G buffer and multiple lights
- a rotating light mode to make the Assignment 4 part easy to see

## Current Showcase Flow

```text
load meshes and optional animation clips
  -> create managed objects
  -> place them in the scene graph
  -> issue showcase movement orders
  -> update object state each frame
  -> refresh animation and skin matrices
  -> rebuild broad phase
  -> run AABB and convex collision checks
  -> dispatch collision callbacks
  -> fill deferred render queue
  -> draw geometry pass
  -> draw lighting pass with N lights
```

## Main Code Locations

- `src/main.cpp`
  The current Assignment 3 and 4 showcase executable.
- `src/DeferredRenderer.cpp`
  Owns the G buffer frame buffers plus the geometry pass and lighting pass.
- `src/Integration.h`
  Objective A helper math for linear and angular integration.
- `src/GameObject.cpp`
  Object state mutation animation playback and automatic AABB refresh.
- `src/Utility.cpp`
  Simulation stage broad phase triangular callback table and deferred dispatch.
- `src/ConvexCollision.h`
  Convex narrow phase used by Objective D.
- `src/SceneGraph.cpp`
  BVH backed spatial graph used by both render queries and the collision broad phase.
- `tests/ObjectiveA_test.cpp`
- `tests/ObjectiveBC_test.cpp`
- `tests/ObjectiveE_test.cpp`
  Current verification coverage for the Assignment 3 implementation.
