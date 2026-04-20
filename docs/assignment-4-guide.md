---
layout: default
title: Assignment 4 Guide
---

# Assignment 4 Deferred Rendering Guide

This page explains the Assignment 4 part that was added to `assignment_1`

The short version is simple

Assignment 3 is the simulation side

Assignment 4 is the way that scene is drawn

## 1 What Deferred Rendering Means Here

In a forward renderer the object is drawn and lit at the same time

That is fine when the light count is small

Once the light count grows the same mesh work starts getting repeated more than it needs to

Deferred rendering splits that into two passes

First the engine stores surface data in buffers

Then it does the lighting work in a second pass

That is what this repo now does in `src/DeferredRenderer.cpp`

## 2 What Render Buffer Management Means

The phrase render buffer management sounds bigger than it is

In this project it means the renderer now owns and resizes the buffers that hold the intermediate frame data

The main pieces are

- one framebuffer object for the G buffer
- one texture for world position
- one texture for world normal
- one texture for albedo color
- one depth renderbuffer

Those buffers are created in `DeferredRenderer::ensureFrameBuffers`

If the window size changes they are recreated at the new size

That is the management part

The engine is not just drawing straight to the screen anymore

It is drawing into a set of controlled intermediate buffers first

## 3 Geometry Pass

The geometry pass draws the meshes once

It does not try to finish the lighting yet

It just writes useful surface information into the G buffer

`src/shaders/deferred_gbuffer.frag` writes

- position
- normal
- albedo

That means after the geometry pass the engine has a screen sized record of what the visible surfaces are

## 4 Lighting Pass

The lighting pass is the second half

Instead of redrawing every mesh for every light the engine draws one fullscreen triangle

The fragment shader samples the three G buffer textures and loops over the light array

That work lives in

- `src/shaders/deferred_light.vert`
- `src/shaders/deferred_light.frag`

So the lighting cost is now tied to the screen pixels that were visible instead of being tied to every mesh draw call over and over

## 5 Why This Helps With N Lights

This is the real reason deferred rendering was worth adding

The mesh data is written once in the geometry pass

After that the lighting shader can walk over a list of lights

So adding more lights does not force another full geometry pass for each light

That is what the assignment means when it talks about demonstrating N light sources

The scene still has to do more lighting work as lights are added

But it avoids repeating the whole mesh draw for every light

## 6 How The Showcase Demonstrates It

`assignment_1` uses the deferred path for the main scene

The demo sets up six colored lights every frame

There are two easy things to show in a video

- the scene is already being lit by several lights at once
- the yellow button switches to a more obvious rotating light mode

That rotating mode is useful because the light movement makes it clear that the lighting is dynamic and not baked into the textures

## 7 How It Fits Together With Assignment 3

The nice part of the current showcase is that the rendering work is not isolated from the gameplay work

The same scene also shows

- movement
- animation
- collision checks
- collision responses

So the final video can show both assignments in one run

That matters because it makes the engine feel like one system instead of two unrelated class exercises

## 8 Main Files

- `src/main.cpp`
  Builds the showcase scene and submits draw commands into the deferred renderer.
- `src/DeferredRenderer.h`
- `src/DeferredRenderer.cpp`
  Own the G buffer resources and the two pass draw flow.
- `src/shaders/deferred_gbuffer.frag`
  Writes position normal and albedo.
- `src/shaders/deferred_light.vert`
- `src/shaders/deferred_light.frag`
  Read the G buffer and apply the light array.

## 9 What To Say In A Short Demo

If you need a short explanation while the app is running this is the simple version

The first pass fills render buffers with surface data

The second pass lights that data with several dynamic lights

That is why the scene can show many lights without redrawing the whole mesh set for each one
