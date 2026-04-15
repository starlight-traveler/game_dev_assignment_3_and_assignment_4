---
layout: default
title: Architecture
---

# Engine Architecture

This page describes the current engine as a set of connected subsystems.

## 1. Public Engine API

`src/Engine.h` and `src/Engine.cpp` expose the gameplay-facing API.

That API is intentionally narrow. It forwards into lower-level runtime storage for:

- delta time
- object creation and lookup
- bounds
- collision type registration
- collision callback registration
- skeletal rig assignment
- animation clip assignment
- animation playback

## 2. Utility Runtime State

`src/Utility.cpp` is the central runtime state layer.

It owns:

- active `GameObject` instances
- per-render-element bounds templates
- the collision response table
- the utility-owned collision broad phase

Architecturally, this means the object update loop, animation playback, and collision dispatch all happen in one place.

## 3. Object System

`GameObject` is the abstract base type for managed objects.

It owns:

- world transform state
- velocities
- local bounds templates and world AABBs
- collision type ids
- animation playback state
- current skin matrices

`RtsUnit` is the concrete managed object used by the current engine-facing object API.

## 4. Animation System

The animation stack has four major layers:

- `blender/meshbin.py`
  Export skinned meshes and rig metadata.
- `blender/animbin.py`
  Export action clips.
- `AnimationClip`
  Store timestamps plus bone quaternions and sample them through SLERP.
- `SkeletalRig`
  Build skin matrices by forward kinematics.

The runtime ownership split is important:

- `Shape` owns rest-pose mesh data and the `SkeletalRig`
- `GameObject` owns playback state and current skin matrices
- `Renderer3D` only consumes the final matrix array

## 5. Spatial Systems

There are now two uses of `SceneGraph` in the codebase.

### Render-Side Scene Graph

The main executable and demos use `SceneGraph` for:

- transform inheritance
- BVH-backed view culling
- world-space scene queries

### Collision Broad Phase

The utility layer also owns a separate `SceneGraph` instance used strictly for collision broad phase.

That collision graph is synchronized from `GameObject` AABBs each frame and queried through `queryAabb` before exact overlap checks run.

## 6. Collision Dispatch

The collision pipeline has two distinct parts:

1. spatial candidate generation through the utility-owned BVH
2. event routing through a triangular type-pair table

The response table stores one callback per unordered type pair and preserves the original registration order for callback arguments.

## 7. Rendering

Rendering is split across:

- `MeshLoader`
- `Shape`
- `ShaderProgram`
- `Texture2D`
- `Renderer3D`

For skinned meshes, `Renderer3D` uploads the current bone matrix array and `src/shaders/world.vert` performs the 4-weight skinning blend on the GPU.

## 8. Executables

The repository currently exposes three important entry points:

- `src/main.cpp`
  Multi-window mesh viewer and engine loop
- `demo/RTSDemo.cpp`
  RTS demo
- `demo/DoomDemo.cpp`
  Separate gameplay demo built on the same base systems
