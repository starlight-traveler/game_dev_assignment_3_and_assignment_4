---
layout: default
title: Objective B Report
---

# Objective B Report

Objective B in the current codebase is the armature and animation pipeline, not the older polymorphism-only writeup that was previously on this page.

## Status

The current implementation covers the main Objective B requirements:

- skinned mesh export with up to 4 bone weights per vertex
- armature import into the engine
- separate animation clip export and import
- runtime keyframe playback with quaternion SLERP
- forward-kinematics skin matrix generation
- GPU skinning in the world vertex shader

The implemented scope is the minimum rotation-only armature system described in `NOTES.md`. Bone translation and bone scaling animation are intentionally not part of the runtime.

## Main Files

- `blender/meshbin.py`
  Exports optional skeletal rig metadata plus `bone_ids[4]` and `bone_weights[4]`.
- `blender/animbin.py`
  Exports one action as a compact animation clip with timestamps and quaternions.
- `src/MeshLoader.cpp`
  Parses the skinned mesh format and builds a `SkeletalRig`.
- `src/AnimationLoader.h`
  Parses `.animbin` into `AnimationClip`.
- `src/SkeletalRig.h`
  Builds runtime skin matrices from sampled local rotations.
- `src/AnimationClip.h`
  Samples between keyframes with `Quaternion::slerp`.
- `src/GameObject.cpp`
  Stores animation playback state and updates skin matrices each frame.
- `src/shaders/world.vert`
  Applies the 4-weight skinning matrix blend on the GPU.

## Asset Pipeline

The mesh exporter preserves Blender armature order so the engine can keep one consistent bone index space across:

- mesh vertex weights
- rig hierarchy data
- animation bone ids
- the shader uniform array

The animation exporter writes:

- total armature bone count
- animated bone count
- the animated bone id list
- one timestamp per keyframe
- one quaternion per animated bone per keyframe

That layout is loaded directly into `AnimationClip`, which makes per-frame sampling simple and keeps animation clips mesh-agnostic as long as the target rig shares the same bone index layout.

## Runtime Animation Controller

At runtime the flow is:

1. `main.cpp` loads a mesh.
2. If the mesh has skinning data, it looks for a same-name `.animbin` sidecar.
3. The loaded `SkeletalRig` and `AnimationClip` are assigned to the managed `GameObject`.
4. `playAnimationForRenderElement()` starts playback.
5. Each frame, `GameObject::updateAnimation()` advances clip time, samples local bone rotations, and asks `SkeletalRig` for skin matrices.
6. `Renderer3D` uploads those matrices to the shader.
7. `world.vert` blends up to 4 bone matrices per vertex.

## Objective B Flowchart

The required flowchart deliverable lives on its own page:

- [Animation Controller Flowchart](./animation-controller-flow.html)

## Verification

The current tests for Objective B runtime animation live in `tests/ObjectiveBC_test.cpp`.

They verify:

- sampled animation state changes over time
- the child bone reaches the expected rotated positions
- playback stops cleanly at the end of a non-looping clip

The mesh viewer path in `src/main.cpp` also exercises the real asset pipeline by loading `.meshbin` plus optional `.animbin` sidecars together.
