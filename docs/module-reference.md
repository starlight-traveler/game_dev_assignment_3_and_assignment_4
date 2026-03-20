---
layout: default
title: Module Reference
---

# Module Reference

This page is a subsystem-by-subsystem guide to the current codebase

## Engine API

Files

- `src/Engine.h`
- `src/Engine.cpp`

Purpose

- expose a simple public interface
- hide the `utility` namespace and concrete object storage

Key code

```cpp
void updateActiveGameObjects() {
    utility::updateGameObjects(getDeltaSeconds());
}
```

Source: `src/Engine.cpp:29-31`

Interpretation

The engine API is intentionally thin and mostly forwards work to lower-level systems

## Utility Runtime State

Files

- `src/Utility.h`
- `src/Utility.cpp`

Purpose

- store delta time
- store active game objects
- support add, remove, clear, and lookup

Architectural note

This file is effectively the central shared runtime state for the engine

## Game Objects

Files

- `src/GameObject.h`
- `src/GameObject.cpp`
- `src/RtsUnit.h`
- `src/RtsUnit.cpp`

Purpose

- define the object update contract
- store transform state
- produce model matrices

Key code

```cpp
const glm::mat4 translation = glm::translate(glm::mat4(1.0f), position_);
const glm::mat4 rotation = static_cast<glm::mat4>(rotation_);
return translation * rotation;
```

Source: `src/GameObject.cpp:16-20`

That is the bridge from simulation state to render state

## Quaternion Math

Files

- `src/Quaternion.h`
- `src/Quaternion.cpp`

Purpose

- represent 3D orientation
- compose rotations without Euler-angle accumulation problems
- rotate vectors
- convert orientation to a matrix

Key code

```cpp
const float half_angle = angle_radians * 0.5f;
const float sin_half = glm::sin(half_angle);
const float cos_half = glm::cos(half_angle);
```

Source: `src/Quaternion.cpp:22-31`

and

```cpp
const float x = (w1 * x2) + (x1 * w2) + (y1 * z2) - (z1 * y2);
const float y = (w1 * y2) - (x1 * z2) + (y1 * w2) + (z1 * x2);
const float z = (w1 * z2) + (x1 * y2) - (y1 * x2) + (z1 * w2);
const float w = (w1 * w2) - (x1 * x2) - (y1 * y2) - (z1 * z2);
```

Source: `src/Quaternion.cpp:37-53`

That second block is the Hamilton product

## SceneGraph And BVH

Files

- `src/SceneGraph.h`
- `src/SceneGraph.cpp`

Purpose

- maintain a transform hierarchy
- maintain fast spatial queries through a BVH

Key code for rebuild

```cpp
spatial_object_nodes_.clear();
bvh_nodes_.clear();
for (const SceneNode& node : nodes_) {
    if (!node.active || !node.object_reference.has_value()) {
        continue;
    }
    spatial_object_nodes_.push_back(node.id);
}
buildBvhRecursive(0, spatial_object_nodes_.size());
```

Source: `src/SceneGraph.cpp:262-285`

Key code for hierarchical transforms

```cpp
node.world_transform = parent_world * node.local_transform;
for (SceneNodeId child_id : node.children) {
    updateWorldRecursive(child_id, node.world_transform);
}
```

Source: `src/SceneGraph.cpp:331-346`

This subsystem is the clearest example of the engine combining scene organization and mathematical transforms

## Mesh Discovery And Loading

Files

- `src/MeshDiscovery.h`
- `src/MeshDiscovery.cpp`
- `src/MeshLoader.h`
- `src/MeshLoader.cpp`

Purpose

- discover `.meshbin` assets on disk
- parse binary mesh formats

Key code

```cpp
if (first_word == kMeshHeaderMagic) {
    return load_header_mesh(input, path);
}
return load_legacy_mesh(input, path, first_word);
```

Source: `src/MeshLoader.cpp:134-140`

So the loader supports both

- a legacy fixed-layout format
- a newer header-based attribute format

## Shape

Files

- `src/Shape.h`
- `src/Shape.cpp`

Purpose

- store CPU-side attribute copies
- allocate VAO and VBO
- define vertex attributes for the current mesh layout

Key code

```cpp
glGenBuffers(1, &vbo_);
glGenVertexArrays(1, &vao_);
glBindVertexArray(vao_);
glBindBuffer(GL_ARRAY_BUFFER, vbo_);
glBufferData(GL_ARRAY_BUFFER, sizeof(float) * usable_floats,
             vertexData.data(), GL_STATIC_DRAW);
```

Source: `src/Shape.cpp:50-60`

This is where raw mesh bytes become GPU-ready geometry

## Renderer

Files

- `src/Renderer3D.h`
- `src/Renderer3D.cpp`
- `src/shaders/world.vert`
- `src/shaders/world.frag`

Purpose

- own shader and texture objects
- queue draw commands
- push uniforms and submit geometry

Key code

```cpp
glUniformMatrix4fv(proj_uniform_loc_, 1, GL_FALSE, glm::value_ptr(command.projection));
glUniformMatrix4fv(view_uniform_loc_, 1, GL_FALSE, glm::value_ptr(command.view));
glUniformMatrix4fv(model_uniform_loc_, 1, GL_FALSE, glm::value_ptr(command.model));
glDrawArrays(GL_TRIANGLES, 0, command.shape->getVertexCount());
```

Source: `src/Renderer3D.cpp:72-88`

## SDL Window Management

Files

- `src/SDL_Manager.h`
- `src/SDL_Manager.cpp`

Purpose

- initialize SDL video
- create and destroy windows
- create OpenGL contexts
- swap buffers

Important design note

This project currently uses one OpenGL context per SDL window

## Texture And Shader RAII

Files

- `src/Texture2D.h`
- `src/Texture2D.cpp`
- `src/ShaderProgram.h`
- `src/ShaderProgram.cpp`

Purpose

- compile shader programs
- manage OpenGL texture objects
- keep cleanup local to RAII wrappers

## Audio System

Files

- `src/Sound.h`
- `src/Sound.cpp`
- `src/SoundSystem.h`
- `src/SoundSystem.cpp`

Purpose

- load WAV files
- convert them into a compatible mono signed-8-bit format
- queue sounds for playback
- mix active sounds in the SDL callback

Key code

```cpp
const int left = static_cast<int>(mix_samples[out_index]) + mono_value;
const int right = static_cast<int>(mix_samples[out_index + 1]) + mono_value;
mix_samples[out_index] = clamp_s8(left);
mix_samples[out_index + 1] = clamp_s8(right);
```

Source: `src/SoundSystem.cpp:179-184`

That is the core mixing logic

## Demo Layer

Files

- `demo/DoomDemo.cpp`
- `demo/README.md`

Purpose

- show how the engine systems can be used to build a tiny playable scenario

This is the best current example of how future gameplay code should sit on top of the engine
