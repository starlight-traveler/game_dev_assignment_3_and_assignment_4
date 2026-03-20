---
layout: default
title: Mathematics
---

# Basic Mathematics In The Current Engine

This page focuses on the core mathematics already present in the codebase

## 1. Foundational Math Ideas

Before looking at the engine-specific formulas, there are a few basic mathematical ideas that appear throughout the code

### Vectors

Most of the code uses `glm::vec3` for 3D quantities such as

- position
- velocity
- direction
- light position
- normals

For example, the engine stores object position and velocity like this

```cpp
glm::vec3 position_;
glm::vec3 linear_velocity_;
glm::vec3 angular_velocity_;
```

Source: `src/GameObject.h`

Conceptually, a 3D vector is just an ordered triple

\[
\mathbf{v} = (x, y, z)
\]

### Vector Length

The engine repeatedly uses vector magnitude, especially for angular velocity and distance tests

```cpp
const float speed = glm::length(angular_velocity_);
```

Source: `src/GameObject.cpp:31`

Mathematically

\[
\|\mathbf{v}\| = \sqrt{x^2 + y^2 + z^2}
\]

### Normalization

A normalized vector has length 1 and preserves only direction

```cpp
return glm::normalize(fwd);
```

Source: `src/main.cpp:240`

Mathematically

\[
\hat{v} = \frac{\mathbf{v}}{\|\mathbf{v}\|}
\]

This appears all over the engine because direction calculations are usually cleaner and more stable with unit vectors

### Dot Product

The dot product appears in the BVH query tests, the shader lighting, and movement checks

```cpp
if (glm::dot(delta_xz, delta_xz) <= (max_distance * max_distance)) {
```

Source: `src/SceneGraph.cpp:566`

Mathematically

\[
\mathbf{a} \cdot \mathbf{b} = a_x b_x + a_y b_y + a_z b_z
\]

Important interpretations

- `dot(v, v)` gives squared length
- `dot(n, l)` measures how aligned two directions are

### Cross Product

The cross product is used to build camera basis vectors

```cpp
glm::vec3 right = glm::cross(fwd, world_up);
```

Source: `src/main.cpp:234`

Mathematically, the cross product returns a vector perpendicular to both inputs

This is why it is useful for constructing camera right and up directions

### Matrices

The engine uses `glm::mat4` for transforms

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

### Trigonometry

The camera direction code uses `sin` and `cos` to convert yaw and pitch angles into a direction vector

```cpp
fwd.x = std::cos(pitch) * std::cos(yaw);
fwd.y = std::sin(pitch);
fwd.z = std::cos(pitch) * std::sin(yaw);
```

Source: `src/main.cpp:214-216`

That is standard trig-based direction construction from angles

### Why These Basics Matter

These few tools are enough to explain most of the rest of the engine

- vectors describe movement, direction, and position
- matrices describe transforms
- trig converts angles into directions
- dot and cross products support geometry and lighting
- normalization keeps direction math stable

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

\[
\mathbf{p}_{new} = \mathbf{p}_{old} + \mathbf{v} \Delta t
\]

Where

- \(\mathbf{p}\) is position
- \(\mathbf{v}\) is velocity
- \(\Delta t\) is the frame delta in seconds

This is the simplest explicit Euler step

## 3. Angular Velocity To Quaternion Rotation

The engine does not store orientation as yaw, pitch, and roll in the object layer

It stores orientation as a quaternion

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

\[
\theta = \|\omega\| \Delta t
\]

\[
\hat{a} = \frac{\omega}{\|\omega\|}
\]

Then the engine builds the axis-angle quaternion

\[
q = (\hat{a}_x \sin(\theta/2), \hat{a}_y \sin(\theta/2), \hat{a}_z \sin(\theta/2), \cos(\theta/2))
\]

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

\[
\mathbf{v}' = q \mathbf{v} q^{-1}
\]

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

\[
M = T R
\]

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

\[
W_{child} = W_{parent} L_{child}
\]

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

\[
min = c - (r,r,r)
\]

\[
max = c + (r,r,r)
\]

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

\[
I = I_{ambient} + \max(\mathbf{n} \cdot \mathbf{l}, 0) I_{diffuse}
\]

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
