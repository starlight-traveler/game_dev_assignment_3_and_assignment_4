---
layout: default
title: Objective D Status
---

# Objective D Status

Objective D in the Assignment 3 handout is the narrow-phase convex collision algorithm requirement: GJK or MPR plus useful distance output.

## Current Repository Status

This repository does not currently contain an implemented Objective D path.

I checked the current tree for `GJK`, `MPR`, `Minkowski`, `simplex`, and related support-point code in `src/` and `tests/`, and there is no real narrow-phase convex collision implementation present yet.

The current collision stack stops at:

- BVH-backed broad-phase candidate generation
- exact AABB overlap testing
- event-driven callback dispatch for overlapping type pairs

That means the current engine satisfies the broad-phase and event-routing parts of Assignment 3, but not the convex polytope narrow-phase requirement yet.

## What Objective D Would Need

To satisfy Objective D later, the engine still needs:

- a convex support-point query on `Shape`
- a simplex structure for iterative search
- either GJK or MPR iterations
- a boolean collision result for transformed convex hulls
- the extra distance/separation output required by the handout
- a clear place in the pipeline after broad-phase pruning where the narrow-phase runs

## Why This Page Exists

The rest of the documentation site now focuses on the implemented Assignment 3 features. This page exists to make the current status explicit instead of implying that narrow-phase convex collision already exists in this branch.
