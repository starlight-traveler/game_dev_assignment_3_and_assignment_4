---
layout: default
title: Runtime Flow
---

# Runtime Flow

This page explains exactly how one run of the current engine proceeds

## Step 1. Process Startup

`src/main.cpp` starts logging, builds the SDL singleton, and calls the user hook

```cpp
quill::Backend::start();
quill::Frontend::create_or_get_logger("sdl", console_sink);
sdl_ptr = &SDL_Manager::sdl();
initialize();
```

Source: `src/main.cpp:879-899`

At this point

- logging works
- SDL video is initialized
- OpenGL can be created later
- the engine is ready to load assets

## Step 2. Audio Device Setup

The main executable immediately initializes the audio system

```cpp
SoundSystem sound_system;
int ui_click_sound_index = -1;
```

Source: `src/main.cpp:901-903`

If a WAV file is found, it is preloaded into the sound library so later UI clicks can trigger it instantly

## Step 3. Spatial System Setup

The scene structure is created once up front

```cpp
SceneGraph scene_graph;
scene_graph.setMaxLeafObjects(3);
```

Source: `src/main.cpp:913-915`

That means the transform tree and BVH storage exist before any objects are inserted

## Step 4. Mesh Discovery

The engine checks for a folder argument and searches for `.meshbin` files

```cpp
const std::string folder_arg = (argc > 1 && argv && argv[1]) ? argv[1] : "";
const std::vector<std::string> mesh_paths = discover_meshbins(folder_arg, kDefaultMeshPath);
```

Source: `src/main.cpp:917-919`

This is what lets the same executable work as a generic mesh viewer

## Step 5. Window Planning And Creation

The engine computes a layout so windows do not overlap badly

```cpp
const WindowPlan plan = build_window_plan(mesh_paths.size(), kWindowWidth, kWindowHeight);
```

Source: `src/main.cpp:926-930`

Then it creates one SDL plus OpenGL window per mesh

## Step 6. Per-Window Render Resource Setup

For each window, the engine creates a `RenderItem`

Each `RenderItem` owns

- the SDL window id
- mesh path
- `Shape`
- `Renderer3D`
- camera state
- a small display mode enum

The actual construction happens in `make_render_item_for_window`

```cpp
item.window_id = window ? SDL_GetWindowID(window) : 0;
item.mesh = std::move(mesh);
item.renderer = std::move(renderer);
```

Source: `src/main.cpp:812-867`

## Step 7. Game Object Creation

After render items exist, the engine creates one `RtsUnit` per render item

```cpp
if (spawnRtsGameObject(render_items[i].window_id, spawn_position,
                       linear_velocity, angular_velocity)) {
    scene_graph.createNode(scene_graph.rootNodeId(),
                           render_items[i].window_id,
                           getModelForRenderElement(render_items[i].window_id),
                           1.5f);
}
```

Source: `src/main.cpp:958-972`

This is a very important point

The engine keeps **two connected representations**

- the `GameObject` pool stores simulation state
- the `SceneGraph` stores transform hierarchy and spatial organization

## Step 8. Frame Begin

At the start of every frame, the main loop computes delta time and stores it in the utility layer

```cpp
const std::uint64_t delta_time_ms = static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::milliseconds>(frame_delta).count());
const float delta_seconds = std::chrono::duration<float>(frame_delta).count();
utility::setFrameDelta(delta_time_ms, delta_seconds);
```

Source: `src/main.cpp:993-997`

Then the API reads that same delta through `getDeltaSeconds()`

## Step 9. Event Handling

The main executable uses SDL events for

- quit
- resize
- UI click buttons

The Doom demo additionally handles

- mouse look
- shooting
- gameplay quit keys

So the engine currently mixes immediate state polling and SDL event dispatch depending on the system

## Step 10. Camera Update

The main executable only moves the camera for the focused window

```cpp
SDL_Window* focused = SDL_GetKeyboardFocus();
RenderItem* item = find_render_item(render_items, focused_window_id);
update_camera_from_input(item->camera, dt_seconds);
```

Source: `src/main.cpp:476-492`

This matters because the engine supports multiple mesh windows at the same time

## Step 11. Game Object Update

Simulation advances here

```cpp
updateActiveGameObjects();
```

Source: `src/main.cpp:1064`

That call goes through

- `Engine.cpp`
- `Utility.cpp`
- the virtual `GameObject::update`
- then down into `RtsUnit::update`

## Step 12. Synchronize Simulation To SceneGraph

This is one of the most important phases in the engine

```cpp
scene_graph.setLocalTransformByObject(item.window_id,
                                      getModelForRenderElement(item.window_id));
scene_graph.updateWorldTransforms();
scene_graph.rebuildSpatialIndex();
```

Source: `src/main.cpp:500-510`

Step by step

1. pull each model matrix from the object pool
2. write that matrix into the matching scene node
3. propagate world transforms through the tree
4. rebuild the BVH from current object bounds

## Step 13. Render Queue Construction

When a window renders, it first asks the scene graph which objects are near the camera

```cpp
std::vector<std::uint32_t> render_queue{};
scene_graph.render(render_queue, item.camera.position, 40.0f);
```

Source: `src/main.cpp:744-746`

That call is just a convenience wrapper around `queryRadius`

The real work is in `SceneGraph::queryRadiusRecursive`

## Step 14. Draw Command Submission

For each visible object, the renderer gets a complete command packet

```cpp
command.shape = item.mesh.get();
command.model = model;
command.view = view;
command.projection = proj;
command.light_position = light_pos;
item.renderer->enqueue(command);
item.renderer->drawQueue();
```

Source: `src/main.cpp:760-768`

The engine is therefore using a very small render-command model rather than issuing raw draw calls all over the main loop

## Step 15. Buffer Swap

After rendering, SDL presents every window

```cpp
sdl.updateWindows();
```

Source: `src/main.cpp:1070-1071`

Inside `SDL_Manager`, each window is made current and swapped in order

## Step 16. Shutdown

On exit, the engine explicitly destroys render items before SDL is torn down

```cpp
while (!render_items.empty()) {
    destroy_render_item_for_window(sdl, render_items, scene_graph,
                                   render_items.back().window_id);
}
clearActiveGameObjects();
```

Source: `src/main.cpp:1082-1088`

This matters because OpenGL objects should be deleted while their contexts still exist

## Runtime Flow Summary

The current engine loop is best summarized as

```text
frame time
 -> input and events
 -> object simulation
 -> model matrix extraction
 -> scene graph world update
 -> BVH rebuild
 -> render queue generation
 -> OpenGL draw
 -> buffer swap
```
