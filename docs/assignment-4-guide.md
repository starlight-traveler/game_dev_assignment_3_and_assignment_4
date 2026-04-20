---
layout: default
title: Assignment 4 Guide
---

# Assignment 4 Deferred Rendering Guide

This page documents the deferred rendering path that now draws `assignment_1`

Assignment 3 updates the scene state

Assignment 4 draws that state through a buffered two pass pipeline

## 1 Render Model

In a forward renderer geometry and lighting are evaluated together

That keeps the pipeline simple but it repeats lighting work while each mesh is being drawn

Deferred rendering changes the order

1. draw visible geometry once and store surface data
2. light the stored surface data in a second pass

The implementation for this repository lives in `src/DeferredRenderer.cpp`

## 2 G Buffer Layout

The renderer owns one framebuffer object that stores the intermediate scene state

The current G buffer contains

- world position
- world normal
- albedo color
- depth

In code the attachments are

- `g_position_tex_`
- `g_normal_tex_`
- `g_albedo_tex_`
- `depth_rbo_`

That is the render buffer management part of the assignment

The engine is now responsible for creating these buffers using them for rendering and recreating them when the drawable size changes

## 3 Buffer Lifetime And Resize Rules

`DeferredRenderer::ensureFrameBuffers` checks the current drawable width and height before each frame

If the existing textures already match the window size they are reused

If the size changed the old buffers are destroyed and a new set is allocated

That keeps the offscreen buffers consistent with the current window

Without that resize path the geometry pass and lighting pass would read and write different sized targets

## 4 Geometry Pass

The geometry pass writes surface attributes into the G buffer

The input to this pass is the same render state that the rest of the engine already produces

- mesh geometry from `Shape`
- model matrix from the managed object state
- view and projection matrices from the camera
- optional skin matrices from animation playback

`src/shaders/world.vert` performs the world transform and optional skinning

`src/shaders/deferred_gbuffer.frag` writes the three stored outputs

The fragment outputs are conceptually

$$
G_p = p_{world}
$$

$$
G_n = \hat n_{world}
$$

$$
G_a = \text{albedo}
$$

So after the geometry pass the renderer has a screen sized description of the visible surfaces

## 5 Lighting Pass Mathematics

The second pass draws one fullscreen triangle

For each screen pixel the lighting shader reads back the stored position normal and albedo and evaluates the light array

The current fragment shader in `src/shaders/deferred_light.frag` follows this shape

Ambient term

$$
L_{ambient} = k_a A
$$

Per light

$$
\mathbf{l}_i = \mathbf{x}_i - \mathbf{p}
$$

$$
d_i = \|\mathbf{l}_i\|
$$

$$
\hat{\mathbf{l}}_i = \frac{\mathbf{l}_i}{d_i}
$$

$$
\text{diffuse}_i = \max(\hat{\mathbf{n}} \cdot \hat{\mathbf{l}}_i, 0)
$$

$$
\text{falloff}_i = 1 - \frac{d_i}{r_i}
$$

$$
\text{attenuation}_i = \text{falloff}_i^2 I_i
$$

Final color

$$
L = L_{ambient} + \sum_i A \odot C_i \cdot \text{diffuse}_i \cdot \text{attenuation}_i
$$

Where

- $A$ is albedo
- $\mathbf{p}$ is world position
- $\hat{\mathbf{n}}$ is the normalized world normal
- $\mathbf{x}_i$ is light position
- $r_i$ is light radius
- $I_i$ is light intensity
- $C_i$ is light color

Lights outside their radius contribute nothing

That keeps the work bounded and makes the radius parameter visually meaningful

## 6 Why This Scales Better For N Lights

The geometry pass is independent of the light count

Visible meshes are transformed and written into the G buffer once

After that the lighting pass loops over the light array

That changes the cost profile

The engine still performs more lighting work as lights are added

But it avoids redrawing the full visible geometry set once per light

That is the practical value of deferred rendering for the assignment requirement

## 7 Connection To The Assignment 3 Runtime

The deferred renderer is not a separate demo layer

It consumes the same runtime state produced by the Assignment 3 systems

The data flow is

1. `updateActiveGameObjects` advances managed objects
2. animation playback refreshes skin matrices
3. the render side `SceneGraph` produces the visible object ids
4. `main.cpp` builds one `RenderCommand` per visible object
5. `DeferredRenderer` executes the geometry pass and lighting pass

So the rendering path sits after the simulation path and uses its final transforms bounds and animation output

## 8 Current Light Configuration

`src/main.cpp` builds six deferred lights each frame

The light data includes

- position
- color
- radius
- intensity

Two screen modes currently exist

- `ScreenMode::showcase`
- `ScreenMode::rotating_light`

Both modes use the same deferred pipeline

The second mode changes the orbit speed radius and intensity so the lighting motion is easier to read

## 9 Main Files

- `src/main.cpp`
  Builds the scene and submits visible objects into the deferred renderer.
- `src/DeferredRenderer.h`
- `src/DeferredRenderer.cpp`
  Own the G buffer attachments the geometry pass and the lighting pass.
- `src/shaders/world.vert`
  Performs transforms and optional 4 weight GPU skinning.
- `src/shaders/deferred_gbuffer.frag`
  Writes position normal and albedo.
- `src/shaders/deferred_light.vert`
- `src/shaders/deferred_light.frag`
  Execute the fullscreen lighting pass.
