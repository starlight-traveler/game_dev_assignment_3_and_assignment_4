---
layout: default
title: Objective C Guide
---

# Objective C Guide

Objective C implementation notes and graphics render pipeline documentation for the current codebase

## Objective C Status

The current codebase has the main Objective C pieces

- a class-based renderer in `Renderer3D`
- mesh loading from `.meshbin`
- GPU mesh upload through `Shape`
- shader program management through `ShaderProgram`
- texture management through `Texture2D`
- MVP-style rendering through `model`, `view`, and `proj` uniforms

The code also supports the extra-credit style flexible mesh layout path because the loader can read a header that describes how many vertex attributes exist and how many components each attribute uses

## Where The Rendering Pipeline Lives

The most important files are

- `src/MeshLoader.cpp`
- `src/Shape.cpp`
- `src/Texture2D.cpp`
- `src/ShaderProgram.cpp`
- `src/Renderer3D.cpp`
- `src/shaders/world.vert`
- `src/shaders/world.frag`

## Step 1. Mesh Data Is Loaded From A `.meshbin` File

The mesh loader supports both a legacy format and a newer header-based format

```cpp
if (first_word == kMeshHeaderMagic) {
    // v2 header-based format supports variable attribute layouts
    return load_header_mesh(input, path);
}

// fallback path handles legacy layout with positions + normals
return load_legacy_mesh(input, path, first_word);
```

Source: `src/MeshLoader.cpp:130-140`

This design addresses the Objective C requirement of either supporting one fixed vertex layout or storing enough header information to describe variable vertex layouts

The current engine supports both

- legacy fixed layout
  positions plus normals
- v2 header layout
  arbitrary attribute counts with per-attribute component counts

This matches the variable-layout extension described in the assignment prompt

## Step 2. The Shape Class Uploads Mesh Data To OpenGL

Once a mesh is parsed, `Shape` turns it into GPU-ready geometry

```cpp
glGenBuffers(1, &vbo_);
glGenVertexArrays(1, &vao_);
glBindVertexArray(vao_);
glBindBuffer(GL_ARRAY_BUFFER, vbo_);
glBufferData(GL_ARRAY_BUFFER, sizeof(float) * usable_floats,
             vertexData.data(), GL_STATIC_DRAW);
```

Source: `src/Shape.cpp:50-60`

This is the basic VBO and VAO setup path

1. request a vertex buffer object
2. request a vertex array object
3. bind the VAO so later attribute state is stored into it
4. bind the VBO to `GL_ARRAY_BUFFER`
5. stream the packed float data into GPU memory

After that, the class configures vertex attributes based on the mesh header

```cpp
glEnableVertexAttribArray(static_cast<GLuint>(attribute_index));
glVertexAttribPointer(static_cast<GLuint>(attribute_index),
                      static_cast<GLint>(component_count),
                      GL_FLOAT,
                      GL_FALSE,
                      0,
                      reinterpret_cast<void*>(offset_bytes));
```

Source: `src/Shape.cpp:71-84`

That is what connects

- attribute location `0` to positions
- attribute location `1` to normals
- attribute location `2` to UVs when present

So the `Shape` class is the main bridge from file data to an OpenGL-ready mesh

## Step 3. Texture2D Manages Texture Buffers

Objective C specifically calls out texture buffers, texture parameters, and bitmap loading

The current engine does exactly that in `Texture2D`

```cpp
SDL_Surface* surface = SDL_LoadBMP(path.c_str());
SDL_Surface* rgba_surface = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
glBindTexture(GL_TEXTURE_2D, texture_id_);
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, rgba_surface->w, rgba_surface->h,
             0, GL_RGBA, GL_UNSIGNED_BYTE, rgba_surface->pixels);
applySamplerState();
```

Source: `src/Texture2D.cpp:27-61`

That corresponds closely to the assignment's texture workflow

1. load bitmap data from disk with `SDL_LoadBMP`
2. convert it into a known RGBA layout
3. request an OpenGL texture id if one does not exist yet
4. bind that id to `GL_TEXTURE_2D`
5. upload texels with `glTexImage2D`
6. apply wrap and filter settings
7. generate mipmaps
8. unbind the texture

Sampler state is handled here

```cpp
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
glGenerateMipmap(GL_TEXTURE_2D);
```

Source: `src/Texture2D.cpp:121-131`

The engine supports the texture topics described in the prompt

- wrapping behavior
- texture filtering
- mipmap generation
- BMP loading through SDL

## Step 4. Renderer3D Is The Class-Based Rendering System

Objective C asked that rendering be organized as a class rather than loose functions

The engine does that with `Renderer3D`

```cpp
class Renderer3D {
public:
    bool initialize(const std::string& vertex_shader_path,
                    const std::string& fragment_shader_path,
                    const std::string& texture_path);
    void beginFrame(int viewport_width, int viewport_height,
                    float clear_r, float clear_g, float clear_b) const;
    void enqueue(const RenderCommand& command);
    void drawQueue();
```

Source: `src/Renderer3D.h:34-72`

This is a sensible assignment-level design because the renderer owns and protects

- the shader program
- the texture object
- cached uniform locations
- the queued draw commands

That matches the prompt's reasoning that rendering has state to manage and should be encapsulated inside a class

## Step 5. The Render Queue Separates Submission From Drawing

Instead of drawing immediately at every call site, the engine first builds `RenderCommand` values

```cpp
struct RenderCommand {
    const Shape* shape;
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 light_position;
    bool use_mesh_uv;
};
```

Source: `src/Renderer3D.h:20-31`

Then code in `main.cpp` or `DoomDemo.cpp` submits commands with `enqueue`, and `Renderer3D::drawQueue()` executes them later

That gives the engine a cleaner render pipeline

1. gameplay and scene code decide what should be drawn
2. they package the draw state into commands
3. the renderer executes those commands in one place

This separation leaves room for later batching or sorting without rewriting gameplay code

## Step 6. Frame Setup Happens In `beginFrame`

Before any world geometry is drawn, the renderer sets the viewport and clear state

```cpp
if (viewport_width > 0 && viewport_height > 0) {
    glViewport(0, 0, viewport_width, viewport_height);
}
glEnable(GL_DEPTH_TEST);
glClearColor(clear_r, clear_g, clear_b, 1.0f);
glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
```

Source: `src/Renderer3D.cpp:40-47`

This is the first stage of the render pipeline each frame

- set framebuffer dimensions
- enable depth testing
- define the background color
- clear the color and depth buffers

Without depth testing, triangles would render in submission order rather than by depth

## Step 7. Shader And Texture Binding Happen Once Before The Draw Loop

At the start of `drawQueue`, the renderer binds the shader and texture

```cpp
glUseProgram(shader_.programId());
texture_.bind(GL_TEXTURE0);
if (texture_uniform_loc_ >= 0) {
    glUniform1i(texture_uniform_loc_, 0);
}
```

Source: `src/Renderer3D.cpp:63-67`

This is directly related to the assignment's discussion of texture units and `sampler2D`

- `glActiveTexture(GL_TEXTURE0)` happens inside `Texture2D::bind`
- the texture is bound onto texture unit 0
- the shader's `base_tex` uniform is told to sample unit 0 by `glUniform1i(..., 0)`

That is exactly the OpenGL state-machine flow described in the prompt

## Step 8. Each Draw Command Streams Per-Object Uniforms

Inside the loop, the renderer binds the shape VAO and uploads the matrices and lighting state

```cpp
glBindVertexArray(command.shape->getVAO());
glUniformMatrix4fv(proj_uniform_loc_, 1, GL_FALSE, glm::value_ptr(command.projection));
glUniformMatrix4fv(view_uniform_loc_, 1, GL_FALSE, glm::value_ptr(command.view));
glUniformMatrix4fv(model_uniform_loc_, 1, GL_FALSE, glm::value_ptr(command.model));
glUniform3fv(light_uniform_loc_, 1, glm::value_ptr(command.light_position));
glDrawArrays(GL_TRIANGLES, 0, command.shape->getVertexCount());
```

Source: `src/Renderer3D.cpp:70-88`

This is the core draw-call stage

- bind the mesh layout and buffer state
- upload the projection matrix
- upload the view matrix
- upload the model matrix
- upload the light position
- issue `glDrawArrays`

## Step 9. The Vertex Shader Performs The MVP Transformation

Objective C asked for model, view, and projection transforms to be incorporated into the scene shader

The current vertex shader does that

```glsl
uniform mat4 proj;
uniform mat4 view;
uniform mat4 model;
...
vec4 world_pos = model * vec4(pos, 1.0);
gl_Position = proj * view * world_pos;
```

Source: `src/shaders/world.vert:6-22`

That means the pipeline is

1. local mesh position `pos`
2. transformed to world space with `model`
3. transformed to camera space with `view`
4. transformed to clip space with `proj`

This is the exact MVP structure the assignment asked for

The same shader also computes a transformed normal and chooses UV coordinates

```glsl
mat3 normal_mat = mat3(transpose(inverse(model)));
normal = normalize(normal_mat * norm);
if (use_mesh_uv != 0) {
    uv = uv_in;
} else {
    uv = pos.xz * 0.35 + vec2(0.5, 0.5);
}
```

Source: `src/shaders/world.vert:14-21`

So the renderer can support both

- meshes with explicit UV data
- meshes that fall back to generated planar UVs

## Step 10. The Fragment Shader Samples The Texture And Computes Lighting

The fragment shader combines texture sampling with Lambert-style lighting

```glsl
uniform vec3 light_pos;
uniform sampler2D base_tex;
...
vec3 light_dir = normalize(light_pos - frag_pos);
float ndotl = max(dot(n, light_dir), 0.0);
vec3 tex = texture(base_tex, uv).rgb;
vec3 ambient = 0.22 * tex;
vec3 diffuse = ndotl * tex;
color = vec4(ambient + diffuse, 1.0);
```

Source: `src/shaders/world.frag:7-18`

This is where the texture buffer actually becomes visible on screen

- the vertex shader passes down UVs
- the fragment shader samples `base_tex`
- the sampled texel is multiplied by a simple lighting model

## Step 11. The Full Graphics Render Pipeline In This Engine

Putting it all together, the current rendering pipeline is

1. discover and load mesh assets from `.meshbin`
2. parse the vertex layout and raw float payload
3. build a `Shape` that uploads the mesh into a VAO and VBO
4. load and compile shaders into a `ShaderProgram`
5. load a BMP texture into `Texture2D`
6. begin the frame with viewport, depth-test, and clear state
7. gather visible objects and build `RenderCommand` values
8. bind shader program and texture unit
9. upload `proj`, `view`, `model`, and lighting uniforms per draw
10. execute `glDrawArrays`
11. let the shaders perform MVP transformation, texture sampling, and lighting
12. swap the SDL window buffers

## Step 12. How This Matches The Objective C Prompt

The prompt emphasized several design ideas, and the current engine lines up with them well

- arbitrary mesh loading
  supported through `.meshbin` parsing
- vertex attribute management
  handled by `Shape` through header-driven attribute setup
- class-based rendering
  handled by `Renderer3D`
- texture buffer management
  handled by `Texture2D`
- shader texture uniform binding
  handled by `base_tex` and `glUniform1i`
- MVP transformation
  handled in `world.vert`

## Step 13. Limits Of The Current Design

This is a solid assignment-level renderer, but it is still intentionally simple

Current limitations

- one main world renderer class
- one primary shader pair for world geometry
- one texture owned by each `Renderer3D`
- no material system yet
- no index buffers yet
- no batching or state sorting yet
- no transparent render pass yet

Those are reasonable next-step extensions for an RTS-style engine, but the current design is already coherent and clearly structured
