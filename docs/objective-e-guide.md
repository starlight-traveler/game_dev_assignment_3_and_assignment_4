---
layout: default
title: Objective E Guide
---

# Objective E Guide

Objective E is the demonstration objective for Assignment 3

This is the part where the project has to feel like one engine runtime and not a pile of separate features

The current answer to Objective E is `assignment_1`

That executable now shows the Assignment 3 systems and the Assignment 4 deferred renderer in one scene

## 1 What Objective E Really Means

Objective E is not asking for one more isolated class or helper function

It is asking whether the systems actually work together in the order a game frame needs

That means

1. time updates
2. objects update
3. animation updates
4. collision work runs
5. collision responses fire
6. rendering uses the latest state

That full chain is what `assignment_1` now shows

## 2 Why `assignment_1` Is The Demo To Use

It is the easiest executable to explain because the important systems are all visible on screen

You can see

- several engine managed objects in one scene
- optional animation playback from `.animbin`
- move commands
- collision reactions
- scene graph driven render submission
- deferred lighting with several lights

That makes it the right place to show Objective E

## 3 Startup And Asset Loading

The executable starts by loading `.meshbin` files

If a mesh has skinning data it also checks for a same name `.animbin` file

If that clip exists it gets attached automatically

That is why `Untitled.meshbin` and `Untitled.animbin` work together in the showcase

There is also a useful fallback in the current version

If you pass only one mesh path the app duplicates that asset into a small scene

That way the assignment still looks like a scene and not one object sitting by itself

## 4 Scene Setup

After loading the assets the executable creates one managed object per render element with `spawnRtsGameObject`

Each object then gets

- local bounds
- convex hull support data
- optional rig and animation clip
- a scene graph node

The executable also registers a collision callback for the RTS unit collision type

That callback does real runtime work

It separates the objects and sends them away from each other after a confirmed collision

So the reactions you see in the showcase are part of the engine event path

## 5 Runtime Order In The Demo

```text
frame delta stored
  -> showcase movement orders updated when needed
  -> updateActiveGameObjects
  -> per object gameplay update
  -> animation playback and skin matrix refresh
  -> collision broad phase rebuild and query
  -> exact AABB overlap test
  -> convex narrow phase
  -> collision callback dispatch
  -> render side scene graph sync
  -> deferred render command submission
  -> geometry pass
  -> lighting pass
```

That is the full Objective E story

The systems are running in one real frame loop

## 6 What The Showcase Lets You Point To

The current scene was built to make the important parts obvious

You can point out

- units moving through the same space
- animation playing on skinned assets
- collisions happening when the units meet
- visible reactions after those collisions
- camera movement around the scene
- the yellow button making the light motion more obvious
- the green button resetting the scene to the intended demo state

The right click rally command is especially useful in a short video because it forces the units back into the middle of the map and makes the collision path easy to show

## 7 Where The Objective E Work Lives

- `src/main.cpp`
  Showcase setup controls scene layout move orders and render submission.
- `src/Engine.cpp`
  Public API used by the showcase to talk to the runtime.
- `src/Utility.cpp`
  Per frame object update animation update collision broad phase and callback dispatch.
- `src/SceneGraph.cpp`
  Render side spatial queries and transform propagation.
- `src/DeferredRenderer.cpp`
  The Assignment 4 draw path that renders the same scene after the Assignment 3 update work is done.
- `tests/ObjectiveA_test.cpp`
  Objective A verification.
- `tests/ObjectiveBC_test.cpp`
  Animation and collision callback verification.
- `tests/ObjectiveE_test.cpp`
  Scene graph and BVH query verification.

## 8 Why This Satisfies Objective E

The current branch does show the Assignment 3 systems working together

- time is stored once and reused through the engine API
- object state changes update transforms and bounds
- animation updates produce the current skin matrices
- broad phase reduces the expensive collision work
- narrow phase confirms real convex intersections
- callbacks mutate live gameplay state
- rendering consumes the latest transforms and animation state

That is what Objective E was asking for

It is also why `assignment_1` is the executable you should open when you need to show the assignment working
