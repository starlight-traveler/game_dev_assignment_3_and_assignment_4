---
layout: default
title: Runtime Flow
---

# Runtime Flow

This page describes the current engine loop at a subsystem level.

## 1. Startup

`src/main.cpp` starts logging, initializes SDL, and calls the user `initialize()` hook.

At this point the runtime has:

- logging
- SDL video
- the engine API
- file-discovery support for mesh assets

## 2. Asset Discovery And Loading

The mesh viewer discovers `.meshbin` assets and loads each one through `MeshLoader`.

If a loaded mesh has skinning data, `main.cpp` also checks for a same-name `.animbin` sidecar and loads it through `AnimationLoader`.

That means one render item can carry:

- a `Shape`
- an optional `SkeletalRig`
- an optional `AnimationClip`
- a `Renderer3D`
- a camera state

## 3. Managed Object Creation

After render items exist, the engine creates one managed `GameObject` per render item through `spawnRtsGameObject`.

The game object becomes the runtime owner of:

- position
- rotation
- velocities
- bounds
- collision type
- animation playback state

## 4. Frame Delta Update

At the top of each frame, `main.cpp` computes the frame delta and stores it in `utility`.

Everything else in the engine reads from that same source of truth.

## 5. Simulation Update

`updateActiveGameObjects()` advances every managed object for the frame.

That update phase now does three things together:

1. object-specific gameplay update
2. animation update and skin-matrix regeneration
3. collision broad phase plus callback dispatch

## 6. Animation Update

If a `GameObject` has both a `SkeletalRig` and an `AnimationClip`, it advances clip time and samples the animation.

The sampled local bone rotations are turned into skin matrices by a forward pass over the rig hierarchy.

Those matrices are cached on the object for rendering later in the frame.

## 7. Collision Broad Phase

After gameplay updates, the utility layer synchronizes object bounds into a utility-owned `SceneGraph`.

The collision broad phase uses:

- one node per object with AABB data
- a BVH rebuild each frame
- `queryAabb` to gather likely overlap candidates

That means candidate generation is spatially pruned instead of scanning every object pair.

## 8. Collision Narrow Phase And Events

Each broad-phase candidate pair is checked with the exact 3D AABB overlap test.

If the pair overlaps and a callback is registered for the type pair, a pending event is stored.

Callbacks are dispatched only after traversal finishes so callbacks can safely mutate or destroy objects.

## 9. Scene Queries For Rendering

Outside the utility-owned collision broad phase, the main executable also maintains its own `SceneGraph` for transform and rendering queries.

That render-side `SceneGraph` is used for:

- transform inheritance
- camera-nearby object queries
- render queue construction

## 10. Render Submission

For each visible object, `main.cpp` builds a `RenderCommand`.

If the object has skinning data, the command also carries the cached bone matrix array so `Renderer3D` can upload it before drawing.

## 11. GPU Draw

`Renderer3D` binds the shader, texture, and mesh state, uploads the per-object uniforms, uploads the optional bone matrices, and issues the OpenGL draw call.

The world vertex shader then performs the 4-weight skinning blend when skinning is enabled.

## 12. Buffer Swap

After all windows are rendered, SDL swaps the OpenGL buffers and the next frame begins.
