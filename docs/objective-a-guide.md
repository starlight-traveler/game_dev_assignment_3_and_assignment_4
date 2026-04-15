---
layout: default
title: Objective A Guide
---

# Objective A Guide

Objective A in the current repository is the kinematics and bounds foundation for the later collision work.

## Status

The implemented Objective A pieces are:

- linear integration helpers in `src/Integration.h`
- angular integration through axis-angle quaternion construction in `src/Integration.h`
- engine-facing integration and impulse API in `src/Engine.h` and `src/Engine.cpp`
- `GameObject` state mutation for position, rotation, linear velocity, and angular velocity in `src/GameObject.cpp`
- automatic world-space AABB refresh whenever transform state changes

## Main Files

- `src/Integration.h`
  Defines the raw helper functions `integrateLinear(float, vec3)` and `integrateAngular(float, vec3)`.
- `src/GameObject.h`
- `src/GameObject.cpp`
  Apply those helpers to object state and refresh cached AABBs.
- `src/Engine.h`
- `src/Engine.cpp`
  Expose the assignment-facing API that mutates managed objects by render id.
- `tests/ObjectiveA_test.cpp`
  Verifies both the helper math and the object-facing API behavior.

## Linear And Angular Integration

The linear helper is the expected discrete Euler step:

$$
\Delta s = v \Delta t
$$

The angular helper converts angular velocity into one axis-angle rotation for the frame:

$$
\theta = \|\omega\| \Delta t
$$

The unit axis is `omega / |omega|`, and the result is stored as a quaternion. In the runtime that quaternion is left-multiplied into the object's current orientation so rotation accumulates over time without using Euler-angle state.

## Object-Facing API

The public API mirrors the assignment spec closely:

- `integrateLinear(render_element, delta_time)`
- `integrateAngular(render_element, delta_time)`
- `integrateAcceleration(render_element, delta_time, acceleration)`
- `integrateAngularAcceleration(render_element, delta_time, acceleration)`
- `applyLinearImpulse(render_element, impulse)`
- `applyAngularImpulse(render_element, impulse)`

The important design choice is that the helpers are available both as raw math functions and as engine-level functions that mutate managed `GameObject` instances directly. That keeps the math reusable but still gives the end user the convenient assignment-facing API.

## Automatic Bounding-Volume Updates

Objective A also required bounds to stay current after motion. The current implementation stores eight local-space AABB corners on each `GameObject`.

Whenever `setPosition()` or `setRotation()` runs:

1. each cached local corner is rotated by the current quaternion
2. the rotated set is scanned for new component-wise min and max
3. the object position is added
4. the result is stored as the current world-space AABB

That happens automatically inside `GameObject::refreshAabb()`, so integrations and manual transform edits both keep the broad-phase input valid.

## Verification

`tests/ObjectiveA_test.cpp` covers the main Objective A behaviors:

- helper integration returns the expected linear displacement
- angular integration rotates the x-axis into the expected direction
- acceleration updates velocity
- impulses apply direct velocity deltas
- position integration updates the model transform
- angular integration updates the orientation
- AABB min and max refresh correctly after translation and rotation

That test is the assignment-facing proof that the engine can update kinematic state and keep bounds coherent for later collision stages.
