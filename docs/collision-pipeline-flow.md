---
layout: default
title: Collision Pipeline Flowchart
---

# Collision Pipeline Flowchart

This page is the explicit Objective C flowchart artifact for the current collision and spatial-graph design.

## Per-Frame Collision Flow

```text
[updateActiveGameObjects]
        |
        v
[for each GameObject]
  update gameplay state
  refresh AABB through position/rotation changes
        |
        v
[sync_collision_broad_phase in Utility.cpp]
  convert each AABB to:
    center = (min + max) / 2
    radius = half diagonal length
        |
        v
[SceneGraph broad phase]
  create/update object nodes
  updateWorldTransforms
  rebuildSpatialIndex
        |
        v
[for each collidable object]
  queryAabb on the BVH using the object's XZ bounds
        |
        v
[candidate object ids]
        |
        v
[full 3D AABB overlap test]
        |
        +---- no overlap ----> [discard candidate]
        |
        v
[triangular response table lookup]
        |
        +---- no callback ----> [discard candidate]
        |
        v
[append PendingCollisionEvent]
        |
        v
[dispatch phase]
  re-fetch live objects by render id
  invoke callback in registration order
```

## Table Storage Flow

```text
[registerCollisionResponse(type_a, type_b, callback)]
        |
        v
[normalize to unordered pair]
  low  = min(type_a, type_b)
  high = max(type_a, type_b)
        |
        v
[triangular index]
  index = high * (high + 1) / 2 + low
        |
        v
[store one entry]
  callback
  registered_type_a
  registered_type_b
```

## Why Dispatch Is Deferred

```text
[collect candidate pairs first]
        |
        v
[store PendingCollisionEvent]
        |
        v
[run callbacks after traversal]
```

Deferred dispatch keeps traversal stable when callbacks:

- destroy objects
- change collision types
- apply impulses
- modify other runtime state
