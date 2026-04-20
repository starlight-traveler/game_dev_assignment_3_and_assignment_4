---
layout: default
title: Objective D Guide
---

# Objective D Guide

Objective D is the convex narrow phase requirement in Assignment 3

Broad phase answers maybe

Objective D answers the exact convex question after that

## 1 Role In The Collision Stack

The current collision stack runs in this order

1. broad phase candidate generation through the BVH
2. exact AABB overlap test
3. convex narrow phase
4. collision callback dispatch

That ordering matters

The broad phase and the AABB overlap test remove obvious misses before the expensive convex work begins

## 2 Support Function Mathematics

The narrow phase uses support queries

For a convex set \(A\) and a direction \(\mathbf{d}\) the support function is

$$
\text{support}_A(\mathbf{d}) = \arg \max_{\mathbf{a} \in A} (\mathbf{a} \cdot \mathbf{d})
$$

This means the farthest point of the shape in a chosen direction

The GJK query works on the Minkowski difference \(A - B\)

Its support function is

$$
\text{support}_{A-B}(\mathbf{d}) =
\text{support}_A(\mathbf{d}) -
\text{support}_B(-\mathbf{d})
$$

That is the key operation used by `src/ConvexCollision.h`

## 3 Where The Support Data Comes From

Imported meshes already keep local vertex positions inside `Shape`

Those points are reused directly as convex support data

The data path is

- `Shape::localSupportPoints()` exposes the stored mesh positions
- `main.cpp` sends those points into the engine for each render element
- `GameObject` stores that local convex set
- `GameObject::supportPointWorld()` answers support queries in world space

If explicit mesh support data does not exist the engine falls back to the corners of the local bounds box

So bounded objects still participate in the narrow phase even without imported mesh hull data

## 4 World Space Support Queries

The stored support set lives in object local space

The query direction arrives in world space

So the engine first rotates the direction into local space then finds the best local point then transforms that point back to world space

Conceptually the steps are

$$
\mathbf{d}_{local} = q^{-1} \mathbf{d}_{world}
$$

$$
\mathbf{p}_{local} = \arg \max_{\mathbf{v} \in H} (\mathbf{v} \cdot \mathbf{d}_{local})
$$

$$
\mathbf{p}_{world} = \mathbf{x} + q \mathbf{p}_{local}
$$

Where

- \(H\) is the local convex support set
- \(q\) is object rotation
- \(\mathbf{x}\) is object position

That is the mathematical shape of `GameObject::supportPointWorld()`

## 5 GJK Search Structure

The algorithm used in `src/ConvexCollision.h` is GJK

It keeps a small simplex in Minkowski difference space

The simplex may contain

- one point
- a line segment
- a triangle
- a tetrahedron

Each iteration does three jobs

1. ask for a new support point along the current search direction
2. compute the point on the simplex closest to the origin
3. update the search direction toward that closest point

If the simplex encloses the origin the shapes intersect

If progress stops before that the closest points and separation data are returned

## 6 Returned Quantities

The public query result is

```cpp
ConvexCollisionQueryResult queryConvexCollisionBetweenRenderElements(
    std::uint32_t first_render_element,
    std::uint32_t second_render_element);
```

The result contains

- whether convex support data existed
- whether the shapes intersect
- separation distance
- separating axis
- witness point on the first shape
- witness point on the second shape

When the shapes do not intersect the separation distance is computed from the witness points

$$
d = \|\mathbf{p}_2 - \mathbf{p}_1\|
$$

The separating axis is the normalized direction from the first witness point to the second

Those extra values are what make the result more useful than a plain yes or no collision flag

## 7 Runtime Placement

Objective D is part of the real engine path now

`src/Utility.cpp` runs the convex narrow phase only after a pair survives

- spatial candidate generation
- exact AABB overlap

That keeps the expensive work focused on the pairs that are already likely to matter

It also means the event system only dispatches callbacks for pairs that passed the full collision stack

## 8 Main Files

- `src/ConvexCollision.h`
  Header only GJK implementation plus separation and witness output.
- `src/GameObject.h`
- `src/GameObject.cpp`
  Store local convex hull data and answer world space support queries.
- `src/Shape.h`
- `src/Shape.cpp`
  Expose imported mesh points as support data.
- `src/Utility.cpp`
  Calls the narrow phase during runtime collision processing.
- `src/Engine.h`
- `src/Engine.cpp`
  Expose the direct convex query API.
