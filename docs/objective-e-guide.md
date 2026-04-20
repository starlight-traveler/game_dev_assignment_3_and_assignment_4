---
layout: default
title: Objective E Guide
---

# Objective E Guide

Objective E is the demonstration objective for Assignment 3

The requirement is not another isolated subsystem

The requirement is a working runtime order where the new systems feed each other correctly

The current implementation uses `assignment_1` as that runtime

## 1 Runtime Meaning Of Objective E

Objective E is satisfied only if the engine runs the assignment work in a real frame loop

The important order is

1. update time
2. update managed objects
3. update animation state
4. run collision tests
5. dispatch collision responses
6. render the latest scene state

That ordering is what turns Objectives A through D into one executable system

## 2 Current Demonstration Executable

The current demonstration executable is `src/main.cpp`

That file was chosen because it already owns

- asset loading
- object creation
- scene setup
- render side scene graph queries
- final render submission

The same executable now draws through the deferred renderer as well so the Assignment 3 runtime and the Assignment 4 draw path use the same scene state

## 3 Startup And Asset Binding

At startup the executable loads `.meshbin` assets through `MeshLoader`

If a mesh has skinning data it also searches for a same name `.animbin` clip

When the clip exists it is attached to the managed object during setup

This keeps the asset binding rule simple

- mesh data comes from `.meshbin`
- animation data comes from `.animbin`

If the runtime receives only one mesh path it duplicates that asset into a small scene arrangement

That keeps the demonstration path scene based instead of object based

## 4 Managed Object And Scene Construction

After assets are loaded the executable creates one managed object per render element with `spawnRtsGameObject`

Each object receives

- transform state
- bounds
- convex hull support data
- optional animation state
- a render side `SceneGraph` node

The setup path also registers the RTS collision callback pair

That means the event system is ready before the first frame begins

## 5 Frame Order In The Current Runtime

The frame loop in `assignment_1` has this shape

```text
frame delta stored in utility
  -> showcase movement targets refreshed when needed
  -> updateActiveGameObjects
  -> per object gameplay update
  -> animation playback and skin matrix refresh
  -> collision broad phase rebuild and query
  -> exact AABB overlap test
  -> convex narrow phase
  -> collision callback dispatch
  -> render side SceneGraph sync
  -> deferred render command submission
  -> geometry pass
  -> lighting pass
```

That is the concrete Objective E path in the current branch

## 6 State Handoff Between Systems

The important architectural detail is that each stage produces data for the next stage

`utility` stores the frame delta

`GameObject` updates use that delta to advance position rotation and animation time

Animation playback produces the current skin matrices

Collision processing reads the updated bounds and convex data

The render side scene graph reads the updated object transforms

The deferred renderer then consumes

- mesh geometry
- model matrices
- camera matrices
- optional skin matrices

So the rendered frame is built from the exact state produced by the simulation and collision stages earlier in the same frame

## 7 Current Scene Behavior

The scene is arranged as a ring of managed objects around the origin

Each object alternates between a home position and a rally position

That autonomous movement does two things for the runtime

- it keeps the object update path active
- it forces repeated interaction through the center of the scene

The collision callback then separates colliding pairs and issues new short move commands away from the contact

That makes the event system part of the visible scene evolution rather than an isolated test hook

## 8 Supporting Test Coverage

The executable is the runtime demonstration

The tests still cover the individual pieces underneath it

`tests/ObjectiveA_test.cpp` covers

- linear and angular integration
- acceleration and impulse helpers
- automatic AABB refresh

`tests/ObjectiveBC_test.cpp` covers

- forward kinematics animation update
- collision callback registration and dispatch

`tests/ObjectiveE_test.cpp` covers

- scene graph parent child behavior
- BVH rebuilds
- radius query behavior
- AABB query behavior
- removal correctness

## 9 Why This Satisfies Objective E

The current branch does show the Assignment 3 systems working together in one runtime

- time is stored once and reused through the engine API
- object state updates produce the latest transforms and bounds
- animation updates produce the latest skin matrices
- broad phase reduces the collision candidate set
- narrow phase confirms real convex intersections
- callbacks mutate live object state after confirmed collisions
- rendering consumes the final state of the same frame

That is the Objective E requirement in runtime form

## 10 Main Files

- `src/main.cpp`
  Runtime setup frame loop scene construction and deferred render submission.
- `src/Engine.cpp`
  Public API used by the executable.
- `src/Utility.cpp`
  Active object storage delta time storage collision broad phase and callback dispatch.
- `src/SceneGraph.cpp`
  Render side spatial graph and query path.
- `src/DeferredRenderer.cpp`
  Final draw path used by the runtime after simulation work is complete.
