---
layout: default
title: Mathematics
---

# Assignment 3 Mathematics

This page collects the main formulas and data reductions used by the implemented Assignment 3 systems.

## 1. Linear Integration

The helper in `src/Integration.h` uses the usual discrete Euler step:

$$
\Delta s = v \Delta t
$$

That same helper is reused for acceleration integration by treating acceleration as a rate of change of velocity:

$$
\Delta v = a \Delta t
$$

In code, that means:

- position update: `position += velocity * dt`
- velocity update: `velocity += acceleration * dt`

## 2. Angular Integration

Angular velocity is stored as a 3-vector whose direction is the axis of rotation and whose magnitude is angular speed in radians per second.

The runtime turns that into one frame-sized quaternion:

$$
\theta = \|\omega\| \Delta t
$$

$$
\hat{u} = \frac{\omega}{\|\omega\|}
$$

$$
q_{\Delta} = \left(\cos\left(\frac{\theta}{2}\right), \hat{u}\sin\left(\frac{\theta}{2}\right)\right)
$$

The current orientation is then updated by quaternion multiplication rather than by storing Euler angles.

## 3. Impulses

The current engine models impulses in the simple assignment-friendly form of direct velocity changes:

$$
v' = v + J
$$

$$
\omega' = \omega + J_{\omega}
$$

There is no mass or inertia tensor in this branch. The impulse API is intentionally kinematic and event-driven.

## 4. AABB Refresh From Rotated Local Bounds

Each `GameObject` stores the eight corners of its local bounds template. After a position or rotation change:

1. rotate each local corner by the current quaternion
2. compute component-wise min and max over the rotated set
3. add the world position

In compact form:

$$
p_i' = R(q) p_i + t
$$

$$
\text{aabb}_{min} = \min_i(p_i')
$$

$$
\text{aabb}_{max} = \max_i(p_i')
$$

That is how the engine keeps world-space AABBs aligned to the global axes even when the object itself rotates.

## 5. Broad-Phase AABB Overlap

The exact overlap test used after BVH candidate generation is the Assignment 3 AABB test:

$$
A_{min}.x \le B_{max}.x
$$

$$
A_{min}.y \le B_{max}.y
$$

$$
A_{min}.z \le B_{max}.z
$$

$$
A_{max}.x \ge B_{min}.x
$$

$$
A_{max}.y \ge B_{min}.y
$$

$$
A_{max}.z \ge B_{min}.z
$$

All six comparisons must hold.

## 6. BVH Broad-Phase Approximation

The collision broad phase converts each world AABB into a center and scalar radius before synchronizing into the `SceneGraph`:

$$
c = \frac{min + max}{2}
$$

$$
r = \frac{\|max - min\|}{2}
$$

This is not the final collision decision. It is only the bound used by the BVH to prune candidate pairs before the exact AABB test runs.

## 7. Triangular Collision Table Indexing

The collision callback table stores one entry per unordered type pair instead of a full duplicated matrix.

For:

$$
low = \min(a, b), \quad high = \max(a, b)
$$

the stored index is:

$$
index = \frac{high(high + 1)}{2} + low
$$

That reduces storage from a full `N x N` matrix to the lower triangle while still supporting same-type diagonal entries.

## 8. SLERP For Animation Playback

The animation controller samples between two keyframes with quaternion SLERP rather than component-wise linear interpolation.

Conceptually:

$$
q(t) = \text{slerp}(q_A, q_B, \alpha)
$$

where:

$$
\alpha = \frac{t - t_A}{t_B - t_A}
$$

This preserves rotational interpolation on the unit quaternion sphere and avoids the worst artifacts of naive lerp plus renormalization.

## 9. Forward Kinematics

For each bone, the runtime carries forward parent rotation and parent displacement through the hierarchy.

In simplified form:

$$
q_{world,i} = q_{world,parent(i)} q_{local,i}
$$

$$
d_i = d_{parent(i)} + q_{world,i} \cdot offset_i
$$

Those results are then assembled into one matrix per bone for the vertex shader.

## 10. Why These Pieces Fit Together

Assignment 3 is mostly about feeding later stages with coherent state:

- integration updates transforms
- transform changes update AABBs
- AABBs feed the BVH-backed broad phase
- broad phase reduces candidate pairs
- exact overlap tests decide whether to dispatch callbacks
- animation math updates the skin matrices that rendering consumes that same frame

These matrices represent

- translation
- rotation
- scale
- model transforms
- view transforms
- projection transforms

For example

```cpp
const glm::mat4 translation = glm::translate(glm::mat4(1.0f), position_);
```

Source: `src/GameObject.cpp:17`

The `glm::mat4(1.0f)` part is the identity matrix

$$
I =
\begin{bmatrix}
1 & 0 & 0 & 0 \\
0 & 1 & 0 & 0 \\
0 & 0 & 1 & 0 \\
0 & 0 & 0 & 1
\end{bmatrix}
$$

In practical terms, the identity matrix means "do nothing"

That makes it the natural starting point for building transforms

### Matrix Multiplication Order

The order of multiplication matters

In this engine

$$
M = T R
$$

means rotate in local space, then translate into world space

Later in the scene graph

$$
W_{child} = W_{parent} L_{child}
$$

means the child's local transform is interpreted relative to the parent's world transform

This is one of the most important ideas in the codebase because the exact same local transform can produce a different world result depending on its parent chain

### Homogeneous Coordinates

The engine uses 4x4 matrices for 3D transforms because translation cannot be represented cleanly with a 3x3 rotation-only matrix

So 3D points are treated as if they had an extra coordinate

$$
(x, y, z) \rightarrow (x, y, z, 1)
$$

That extra `1` is what allows matrix multiplication to include translation along with rotation and projection

### Trigonometry

The camera direction code uses `sin` and `cos` to convert yaw and pitch angles into a direction vector

```cpp
fwd.x = std::cos(pitch) * std::cos(yaw);
fwd.y = std::sin(pitch);
fwd.z = std::cos(pitch) * std::sin(yaw);
```

Source: `src/main.cpp:214-216`

That is standard trig-based direction construction from angles

### Degrees Versus Radians

The engine mostly uses radians for rotation math

For example

```cpp
glm::radians(60.0f)
```

Source: `src/main.cpp:198`

This converts degrees into radians before building the projection matrix

The basic conversion is

$$
\text{radians} = \text{degrees} \cdot \frac{\pi}{180}
$$

This matters because C++ math functions and most graphics libraries expect radians, not degrees

### Why Basic Trig Shows Up In A Game Engine

Trig is how the engine turns angular descriptions into spatial directions

- yaw and pitch become a camera forward vector
- axis-angle rotations become quaternion components through `sin` and `cos`
- perspective projection depends on field of view, which is an angle

So even though the engine often works with vectors and matrices directly, trig is still underneath several major systems

### Why These Basics Matter

These few tools are enough to explain most of the rest of the engine

- vectors describe movement, direction, and position
- matrices describe transforms
- trig converts angles into directions
- dot and cross products support geometry and lighting
- normalization keeps direction math stable
- squared distance checks make repeated spatial queries cheaper

## 2. Delta Time And Euler Integration

The most repeated pattern in the engine is frame-rate-independent integration

### Position Update

```cpp
void GameObject::integrateVelocity(float delta_seconds) {
    position_ += linear_velocity_ * delta_seconds;
}
```

Source: `src/GameObject.cpp:26-28`

Mathematically this is

$$
\mathbf{p}_{new} = \mathbf{p}_{old} + \mathbf{v} \Delta t
$$

Where

- $\mathbf{p}$ is position
- $\mathbf{v}$ is velocity
- $\Delta t$ is the frame delta in seconds

This is the simplest explicit Euler step

The key idea is that velocity is measured in units per second

So if an object moves at `(2, 0, 0)` and the frame lasts `0.5` seconds, then the position change is

$$
(2, 0, 0) \cdot 0.5 = (1, 0, 0)
$$

That is why multiplying by delta time makes motion frame-rate independent

If the engine skipped delta time and just added velocity directly every frame, then faster frame rates would make objects move farther per second

## 3. Angular Velocity To Quaternion Rotation

The engine does not store orientation as yaw, pitch, and roll in the object layer

It stores orientation as a quaternion

This is useful because quaternions avoid some of the instability and axis-order problems that come with Euler-angle accumulation

The key update code is

```cpp
const float speed = glm::length(angular_velocity_);
const glm::vec3 axis = angular_velocity_ / speed;
const float angle = speed * delta_seconds;
const Quaternion delta_rotation(axis, angle);
rotation_ = delta_rotation * rotation_;
rotation_.normalize();
```

Source: `src/GameObject.cpp:30-40`

This means

1. compute the magnitude of the angular velocity vector
2. use the normalized direction as the rotation axis
3. use magnitude times time as the rotation angle
4. build a quaternion for that small frame rotation
5. compose it with the current orientation

Mathematically

$$
\theta = \|\omega\| \Delta t
$$

$$
\hat{a} = \frac{\omega}{\|\omega\|}
$$

Then the engine builds the axis-angle quaternion

$$
q = (\hat{a}_x \sin(\theta/2), \hat{a}_y \sin(\theta/2), \hat{a}_z \sin(\theta/2), \cos(\theta/2))
$$

You can see that directly in the quaternion constructor

```cpp
const float half_angle = angle_radians * 0.5f;
const float sin_half = glm::sin(half_angle);
const float cos_half = glm::cos(half_angle);
quaternion_.x = normalized_axis.x * sin_half;
quaternion_.y = normalized_axis.y * sin_half;
quaternion_.z = normalized_axis.z * sin_half;
quaternion_.w = cos_half;
```

Source: `src/Quaternion.cpp:22-31`

## 4. Quaternion Composition

The engine multiplies quaternions with the Hamilton product

```cpp
const float x = (w1 * x2) + (x1 * w2) + (y1 * z2) - (z1 * y2);
const float y = (w1 * y2) - (x1 * z2) + (y1 * w2) + (z1 * x2);
const float z = (w1 * z2) + (x1 * y2) - (y1 * x2) + (z1 * w2);
const float w = (w1 * w2) - (x1 * x2) - (y1 * y2) - (z1 * z2);
```

Source: `src/Quaternion.cpp:48-53`

This is what allows small frame rotations to accumulate into a stable orientation

## 5. Quaternion Vector Rotation

A vector is rotated using the standard quaternion formula

```cpp
const Quaternion point(rhs.x, rhs.y, rhs.z, 0.0f);
const Quaternion rotated = (*this) * point * conjugate();
```

Source: `src/Quaternion.cpp:56-59`

Mathematically that is

$$
\mathbf{v}' = q \mathbf{v} q^{-1}
$$

where the vector is treated as a pure quaternion with zero scalar part

## 6. Model Matrix Construction

Objects become renderable by converting simulation state into a matrix

```cpp
const glm::mat4 translation = glm::translate(glm::mat4(1.0f), position_);
const glm::mat4 rotation = static_cast<glm::mat4>(rotation_);
return translation * rotation;
```

Source: `src/GameObject.cpp:16-20`

That means the engine currently uses

$$
M = T R
$$

There is no scale in the generic `GameObject` model yet

## 7. Camera Direction Mathematics

The main executable uses yaw and pitch to derive a forward vector

```cpp
fwd.x = std::cos(pitch) * std::cos(yaw);
fwd.y = std::sin(pitch);
fwd.z = std::cos(pitch) * std::sin(yaw);
```

Source: `src/main.cpp:214-216`

This is spherical-coordinate style camera math

The right vector is then computed with a cross product

```cpp
glm::vec3 right = glm::cross(fwd, world_up);
```

Source: `src/main.cpp:234`

And the final view matrix comes from

```cpp
return glm::lookAt(camera.position, camera.position + fwd, up);
```

Source: `src/main.cpp:257`

## 8. Perspective Projection

Projection is handled with GLM

```cpp
const float aspect = static_cast<float>(w) / static_cast<float>(h);
return glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);
```

Source: `src/main.cpp:195-198`

The important mathematical idea is that the engine maps a 3D frustum into clip space using

- field of view
- aspect ratio
- near plane
- far plane

## 9. Hierarchical Transform Mathematics

The scene graph propagates transforms down the parent-child tree

```cpp
node.world_transform = parent_world * node.local_transform;
for (SceneNodeId child_id : node.children) {
    updateWorldRecursive(child_id, node.world_transform);
}
```

Source: `src/SceneGraph.cpp:341-346`

Mathematically

$$
W_{child} = W_{parent} L_{child}
$$

This is the exact reason child nodes inherit movement from parent nodes

## 10. BVH Bounding Mathematics

The BVH uses a simple bounding sphere per object and expands it into an AABB

```cpp
const glm::vec3 center = matrix_translation(transform);
const glm::vec3 extent(std::max(radius, 0.0f));
return std::make_pair(center - extent, center + extent);
```

Source: `src/SceneGraph.cpp:22-25`

So each object gets bounds

$$
min = c - (r,r,r)
$$

$$
max = c + (r,r,r)
$$

This is not a tight oriented bound, but it is fast and simple

## 11. BVH Split Mathematics

When building the BVH, the engine chooses a split axis from centroid spread

```cpp
const auto centroid_bounds = computeCentroidBounds(start, end);
const glm::vec3 extent = centroid_bounds.second - centroid_bounds.first;
```

Source: `src/SceneGraph.cpp:487-488`

Then it partitions objects around the median centroid

```cpp
const std::size_t mid = start + ((end - start) / 2);
std::nth_element(...);
```

Source: `src/SceneGraph.cpp:505-512`

That is a standard balanced-BVH heuristic

## 12. BVH Query Mathematics

For radius queries, the engine first measures the squared XZ distance from the query point to the node AABB

```cpp
if (squared_distance_to_aabb_xz(center, bvh_node.min_bounds, bvh_node.max_bounds) >
    (radius * radius)) {
    return;
}
```

Source: `src/SceneGraph.cpp:543-546`

This is broad-phase pruning

If the circle cannot reach the node box, the entire subtree is rejected

At the leaf level, object overlap is checked with

```cpp
const float max_distance = radius + node.bounding_radius;
if (glm::dot(delta_xz, delta_xz) <= (max_distance * max_distance)) {
    out_objects.push_back(node.object_reference.value());
}
```

Source: `src/SceneGraph.cpp:564-568`

That is circle-against-expanded-circle logic in the XZ plane

## 13. Normal Transformation In The Vertex Shader

The vertex shader computes a normal matrix

```glsl
mat3 normal_mat = mat3(transpose(inverse(model)));
normal = normalize(normal_mat * norm);
```

Source: `src/shaders/world.vert:17-18`

This is the correct way to transform normals when model transforms may distort basis vectors

## 14. Lighting Mathematics In The Fragment Shader

The fragment shader uses a simple Lambert diffuse term

```glsl
vec3 n = normalize(normal);
vec3 light_dir = normalize(light_pos - frag_pos);
float ndotl = max(dot(n, light_dir), 0.0);
vec3 ambient = 0.22 * tex;
vec3 diffuse = ndotl * tex;
```

Source: `src/shaders/world.frag:13-18`

Mathematically this is

$$
I = I_{ambient} + \max(\mathbf{n} \cdot \mathbf{l}, 0) I_{diffuse}
$$

This is simple but completely appropriate for the current assignment

## 15. Audio Mixing Mathematics

The audio mixer sums mono samples into left and right output channels

```cpp
const int left = static_cast<int>(mix_samples[out_index]) + mono_value;
const int right = static_cast<int>(mix_samples[out_index + 1]) + mono_value;
mix_samples[out_index] = clamp_s8(left);
mix_samples[out_index + 1] = clamp_s8(right);
```

Source: `src/SoundSystem.cpp:181-184`

This is sample addition with clipping

The clipping function is

```cpp
return static_cast<Sint8>(std::max(-128, std::min(127, value)));
```

Source: `src/SoundSystem.cpp:12-14`

## Mathematics Summary

The key mathematics to remember for this engine are

- Euler integration for movement
- axis-angle to quaternion conversion
- Hamilton product for rotation composition
- model, view, and projection matrices
- parent-child transform multiplication
- BVH bounding and overlap tests
- Lambert diffuse lighting
- sample summation and clamping for audio
