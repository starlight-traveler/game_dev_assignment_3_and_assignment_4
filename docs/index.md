---
layout: default
title: Home
---

# Assignment 3 Guide

This site now documents the parts of the repository that matter for Game Dev 3 Assignment 3: collisions, event-driven interactions, kinematic integration, and the animation/controller work that feeds the simulation stage.

## Assignment Status

- Objective A: implemented
- Objective B: implemented
- Objective C: implemented
- Objective D: not implemented in the current branch
- Objective E: documented and demonstrated through the runtime path plus tests

## Recommended Reading Order

1. [Objective A Guide](./objective-a-guide.html)
2. [Objective B Report](./objective-b-report.html)
3. [Animation Controller Flowchart](./animation-controller-flow.html)
4. [Objective C Guide](./objective-c-guide.html)
5. [Collision Pipeline Flowchart](./collision-pipeline-flow.html)
6. [Objective D Status](./objective-d-guide.html)
7. [Objective E Guide](./objective-e-guide.html)
8. [Mathematics](./mathematics.html)
9. [Architecture](./architecture.html)
10. [Runtime Flow](./runtime-flow.html)

## Assignment 3 Runtime Shape

```text
update time
  -> update managed GameObjects
  -> integrate motion / refresh AABBs
  -> update animation playback / skin matrices
  -> synchronize BVH-backed broad phase
  -> exact AABB overlap tests
  -> invoke user collision callbacks
  -> render current frame
```

## Main Code Locations

- `src/Integration.h`
  Objective A helper math for linear and angular integration.
- `src/GameObject.cpp`
  Object state mutation, animation playback, and automatic AABB refresh.
- `src/Utility.cpp`
  Simulation-stage broad phase, triangular callback table, and deferred dispatch.
- `src/SceneGraph.cpp`
  BVH-backed spatial graph used by both render queries and the collision broad phase.
- `tests/ObjectiveA_test.cpp`
- `tests/ObjectiveBC_test.cpp`
- `tests/ObjectiveE_test.cpp`
  Current verification coverage for the Assignment 3 implementation.
