---
layout: default
title: Animation Controller Flowchart
---

# Animation Controller Flowchart

This page is the explicit Objective B flowchart artifact for the current animation controller design.

## Asset Flow

```text
[Blender rigged mesh]
        |
        v
[meshbin.py]
  writes bind heads
  parent indices
  bone_ids[4]
  bone_weights[4]
        |
        v
[.meshbin]
        |
        v
[MeshLoader.cpp]
        |
        v
[Shape owns SkeletalRig]

[Blender action]
        |
        v
[animbin.py]
  writes animated bone ids
  timestamps
  quaternions
        |
        v
[.animbin]
        |
        v
[AnimationLoader.h]
        |
        v
[AnimationClip]
```

## Runtime Ownership

```text
[Shape]
  owns rest-pose SkeletalRig
        |
        v
[main.cpp]
  copies rig pointer + clip pointer
  into the managed GameObject
        |
        v
[GameObject]
  owns playback time
  loop flag
  playing flag
  current skin matrices
```

## Per-Frame Playback Flow

```text
[updateActiveGameObjects]
        |
        v
[GameObject::updateAnimation]
  advance clip-local time
        |
        v
[AnimationClip::sampleLocalRotations]
  find keyframe A and B
  compute blend factor
  SLERP each animated bone
        |
        v
[SkeletalRig::buildSkinMatrices]
  forward pass parent -> child
  accumulate parent rotations
  compute child head displacement
  build one matrix per bone
        |
        v
[Renderer3D]
  uploads bone_matrices uniform array
        |
        v
[world.vert]
  bone_weights.x * bone_matrices[id.x]
  bone_weights.y * bone_matrices[id.y]
  bone_weights.z * bone_matrices[id.z]
  bone_weights.w * bone_matrices[id.w]
        |
        v
[skinned vertex position + normal]
```

## User Control Points

The user-facing control points in the current engine are:

- assign the rig with `setSkeletalRigForRenderElement`
- assign the clip with `setAnimationClipForRenderElement`
- start playback with `playAnimationForRenderElement`
- stop playback with `stopAnimationForRenderElement`
- query state with `isAnimationPlayingForRenderElement`

That is the exact place in the engine pipeline where animation can be created, reset, and invoked today.
