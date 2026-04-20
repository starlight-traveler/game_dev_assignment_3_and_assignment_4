---
layout: default
title: Objective D Guide
---

# Objective D Guide

Objective D is the convex narrow phase part of Assignment 3

This is the stage where the engine stops relying on broad bounds alone and answers the real collision question for convex shapes

## 1 What Objective D Is Asking For

Broad phase is the cheap maybe stage

It tells the engine which pairs are worth checking

Objective D is the exact check after that

The engine needs to answer two things

- are these two convex shapes intersecting
- if they are not intersecting how far apart are they and in what direction

That second part matters because it makes the result useful for more than a yes or no test

## 2 Where The Shape Data Comes From

Imported meshes already keep local vertex positions inside `Shape`

That means the engine already has a simple set of points that describe the mesh in local space

Objective D reuses those points as convex support data

The data path is

- `Shape::localSupportPoints()` exposes the stored local positions
- `main.cpp` sends those points into the engine for each render element
- `GameObject` stores that local convex set
- `GameObject::supportPointWorld()` answers support queries in world space

If an object does not have explicit mesh support points the engine still has a fallback

It can use the corners of the local bounds box

So bounded objects still have a convex shape to test even when they were not imported from a richer mesh

## 3 What A Support Point Is

A support point is just the farthest point of a shape in a chosen direction

That is the whole idea

If the algorithm asks for the farthest point to the right the object returns the point on the hull that sticks out the most to the right

That sounds small but it is exactly the operation that GJK needs

It means the engine does not have to compare every triangle against every other triangle

It only needs a way to answer that one farthest point query over and over

## 4 What GJK Is Doing

The narrow phase is implemented in `src/ConvexCollision.h`

The algorithm used is GJK

In plain language the loop is

1. pick a search direction
2. ask both shapes for support points
3. build a point in Minkowski difference space
4. keep a small simplex of the best points so far
5. move the search toward the point closest to the origin
6. if the simplex encloses the origin the shapes intersect
7. if progress stops the closest points and separation are returned

The nice thing about GJK here is that it works well with the support point model the engine already has

It is also a good fit for convex imported meshes and convex box fallbacks

## 5 What The Engine Returns

The public query shape is

```cpp
ConvexCollisionQueryResult queryConvexCollisionBetweenRenderElements(
    std::uint32_t first_render_element,
    std::uint32_t second_render_element);
```

The result reports

- whether convex support data existed
- whether the two convex shapes intersect
- the current separation distance when they do not intersect
- the separating axis in world space
- witness points on the first and second shapes

The witness points are the points on each shape that were closest during the final query state

That makes the result more useful for debugging and for future response work

## 6 Where Objective D Sits In The Runtime

The collision stack runs in this order

1. broad-phase BVH candidate generation
2. exact AABB overlap check
3. GJK convex narrow phase
4. callback dispatch for confirmed intersections

The exact AABB step still matters

It is cheaper than GJK so it filters out pairs before the expensive convex work starts

That means Objective D is part of the real runtime path now and not just a helper function sitting off to the side

## 7 Main Files

- `src/ConvexCollision.h`
  Header only GJK narrow phase plus closest point and separation output.
- `src/GameObject.h`
- `src/GameObject.cpp`
  Store local convex support data and answer world space support queries.
- `src/Shape.h`
- `src/Shape.cpp`
  Expose imported mesh points for support queries.
- `src/Utility.cpp`
  Calls the narrow phase after broad phase pruning and before callback dispatch.
- `src/Engine.h`
- `src/Engine.cpp`
  Expose the direct query function to the rest of the engine.

## 8 What You Can Point To In The Showcase

When units meet in `assignment_1` the engine is not only using big broad bounds

It is doing

- broad phase to find likely pairs
- exact AABB overlap
- convex narrow phase to confirm the pair
- collision callback dispatch after the pair is confirmed

So Objective D is visible in the demo through the way the units react when they actually meet in the middle of the scene
