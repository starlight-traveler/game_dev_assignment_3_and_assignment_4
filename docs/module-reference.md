---
layout: default
title: Module Reference
---

# Module Reference

This page is a short subsystem index for the current codebase.

## `src/main.cpp`

Purpose:

- current Assignment 3 and 4 showcase executable
- mesh and animation sidecar loading
- showcase scene setup
- camera control
- move order and reset controls
- deferred render submission

## `src/Engine.*`

Purpose:

- public API over the internal runtime
- object spawning and lookup
- bounds helpers
- collision registration
- animation control

## `src/Utility.*`

Purpose:

- shared frame delta storage
- active `GameObject` storage
- local-bounds templates
- BVH-backed collision broad phase
- triangular collision callback table
- deferred collision dispatch

## `src/GameObject.*`

Purpose:

- abstract runtime object contract
- position, rotation, velocity, and AABB state
- collision type storage
- animation playback state
- cached skin matrices

## `src/RtsUnit.*`

Purpose:

- concrete engine-managed object type
- RTS-style movement update path

## `src/Quaternion.*`

Purpose:

- custom quaternion type
- axis-angle construction
- Hamilton product
- vector rotation
- matrix conversion
- SLERP support

## `src/SkeletalRig.h`

Purpose:

- rest-pose armature storage
- forward-kinematics skin matrix generation

## `src/AnimationClip.h`

Purpose:

- store keyframes and animated bone ids
- sample animation time points through SLERP

## `src/AnimationLoader.h`

Purpose:

- parse `.animbin` clips into `AnimationClip`

## `src/MeshLoader.cpp`

Purpose:

- parse `.meshbin`
- load optional rig metadata into `Shape`

## `src/SceneGraph.*`

Purpose:

- hierarchical transforms
- BVH rebuilds
- radius and AABB scene queries
- reusable spatial graph for both rendering and collision broad phase

## `src/DeferredRenderer.*`

Purpose:

- G buffer framebuffer ownership
- render buffer resize management
- deferred geometry pass
- deferred lighting pass
- multi light submission

## `src/Renderer3D.*`

Purpose:

- shader and texture ownership
- render queue submission
- optional bone-matrix upload
- forward OpenGL draw execution used by older demos

## `src/shaders/world.vert`

Purpose:

- per-vertex transform
- optional 4-weight GPU skinning

## `src/shaders/deferred_gbuffer.frag`

Purpose:

- write position
- write normal
- write albedo

## `src/shaders/deferred_light.*`

Purpose:

- fullscreen lighting pass
- loop over the deferred light array
- combine the G buffer into the final lit frame

## `blender/meshbin.py`

Purpose:

- export positions, normals, UVs
- export optional `bone_ids[4]` and `bone_weights[4]`
- export rig parent indices and bind heads

## `blender/animbin.py`

Purpose:

- export one Blender action as a compact animation clip

## `tests/ObjectiveBC_test.cpp`

Purpose:

- verify runtime skinning
- verify registered collision callback dispatch
- verify same-type callback dispatch through the triangular table
