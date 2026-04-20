---
layout: default
title: Runtime Flow
---

# Runtime Flow

This page describes the current `assignment_1` loop at a subsystem level

The current executable is a single scene showcase for Assignment 3 and Assignment 4

## 1. Startup

`src/main.cpp` starts logging, initializes SDL, and calls the user `initialize()` hook.

At this point the runtime has:

- logging
- SDL video
- the engine API
- mesh discovery support
- one showcase window
- the deferred renderer

## 2. Asset Discovery And Loading

The showcase discovers `.meshbin` assets and loads them through `MeshLoader`

If a loaded mesh has skinning data `main.cpp` also checks for a same name `.animbin` sidecar and loads it through `AnimationLoader`

If the user passes only one mesh path the app duplicates that asset into a small scene so the assignment still looks like a real scene when it runs

That means one render item can carry

- a `Shape`
- an optional `SkeletalRig`
- an optional `AnimationClip`
- object placement data
- a move speed

The renderer itself is owned once by the showcase window

## 3. Managed Object Creation

After render items exist the engine creates one managed `GameObject` per render item through `spawnRtsGameObject`

The game object becomes the runtime owner of

- position
- rotation
- velocities
- bounds
- collision type
- animation playback state

The setup path also registers the showcase collision callback and creates one `SceneGraph` node per object

## 4. Initial Showcase Setup

Before the first frame the executable

- places the objects around the center of the scene
- fits the camera to that layout
- assigns their first movement targets

That is why the scene starts in a readable state instead of with everything piled up at the origin

## 5. Frame Delta Update

At the top of each frame, `main.cpp` computes the frame delta and stores it in `utility`.

Everything else in the engine reads from that same source of truth.

## 6. Showcase Movement Update

Before the engine update runs `main.cpp` checks whether any showcase object has reached its current target

If it has the executable issues the next move command

That keeps the scene alive and keeps collisions happening without the user having to constantly interact

## 7. Simulation Update

`updateActiveGameObjects()` advances every managed object for the frame

That update phase now does three things together:

1. object-specific gameplay update
2. animation update and skin-matrix regeneration
3. collision broad phase plus callback dispatch

## 8. Animation Update

If a `GameObject` has both a `SkeletalRig` and an `AnimationClip`, it advances clip time and samples the animation.

The sampled local bone rotations are turned into skin matrices by a forward pass over the rig hierarchy.

Those matrices are cached on the object for rendering later in the frame.

## 9. Collision Broad Phase

After gameplay updates, the utility layer synchronizes object bounds into a utility-owned `SceneGraph`.

The collision broad phase uses:

- one node per object with AABB data
- a BVH rebuild each frame
- `queryAabb` to gather likely overlap candidates

That means candidate generation is spatially pruned instead of scanning every object pair.

## 10. Collision Narrow Phase And Events

Each broad-phase candidate pair is checked with the exact 3D AABB overlap test.

If the pair survives that check the convex narrow phase runs

If the pair overlaps and a callback is registered for the type pair a pending event is stored

Callbacks are dispatched only after traversal finishes so callbacks can safely mutate or destroy objects

In the showcase that callback pushes the units apart and sends them away again

## 11. Render Side Scene Sync

Outside the utility owned collision broad phase the main executable also maintains its own `SceneGraph` for transform and rendering queries

That render side `SceneGraph` is used for

- transform inheritance
- camera-nearby object queries
- render queue construction

## 12. Render Submission

For each visible object `main.cpp` builds a `RenderCommand`

If the object has skinning data the command also carries the cached bone matrix array so the vertex shader can skin the mesh before drawing

## 13. Deferred Geometry Pass

`DeferredRenderer` first draws the scene into the G buffer

That pass writes

- position
- normal
- albedo

The world vertex shader still performs the 4 weight skinning blend when skinning is enabled

## 14. Deferred Lighting Pass

After the geometry pass the renderer draws one fullscreen triangle and applies the light array in screen space

That is the Assignment 4 part of the runtime

The current showcase uses six lights and can switch into a rotating light mode

## 15. Buffer Swap

After the frame is finished SDL swaps the OpenGL buffers and the next frame begins
