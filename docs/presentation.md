---
layout: default
title: Presentation
presentation_deck: true
---

# RTS Engine Presentation

This page is organized as a 15 minute presentation outline for explaining the project, the custom RTS systems, the tool workflow, and the deviations from the original assignment features.

<nav class="presentation-tabs" aria-label="Presentation sections">
  <a href="#talk-plan">Talk Plan</a>
  <a href="#project">Project</a>
  <a href="#engine-loop">Engine Loop</a>
  <a href="#rts-simulation">RTS Simulation</a>
  <a href="#graphics">Graphics</a>
  <a href="#ai">AI</a>
  <a href="#tools">Tools</a>
  <a href="#development-approach">Development</a>
  <a href="#requirements">Requirements</a>
  <a href="#deviations">Deviations</a>
</nav>

<div class="presentation-controls" data-presentation-controls aria-label="Presentation controls">
  <button type="button" data-prev-slide>Previous</button>
  <button type="button" data-next-slide>Next</button>
  <button type="button" data-toggle-details>Expand Details</button>
  <button type="button" data-toggle-all>Show All</button>
  <span data-slide-status>Slide mode loading...</span>
</div>

## Talk Plan

| Time | Topic | Main point |
| --- | --- | --- |
| 0:00-1:00 | Project overview | The project is an RTS-focused engine demo built on the assignment engine work. |
| 1:00-3:00 | Engine workflow | Show how setup, update, simulation, and render submission are separated. |
| 3:00-6:00 | RTS systems | Explain terrain, orders, pathfinding, combat, economy, buildings, production, fog, and events. |
| 6:00-8:00 | Graphics pipeline | Explain the deferred rendering pass and how animated meshes reach the GPU. |
| 8:00-10:00 | AI workflow | Explain snapshot-driven team AI, command emission, and replay through normal player command APIs. |
| 10:00-12:00 | Tools | Explain Blender exporters, mesh discovery, archetype configuration, demo modes, and test targets. |
| 12:00-13:00 | Development approach | Explain how features were added, tested, and deployed into demos. |
| 13:00-14:00 | Challenges and deviations | Explain what changed from the base assignment and why. |
| 14:00-15:00 | Wrap-up | Summarize the final engine shape and what the demo proves. |

<details class="speaker-notes" markdown="1">
<summary>Detailed speaking notes</summary>

Use this as the pacing slide. The goal is not to read every bullet on later slides. Treat each section as a checkpoint: explain the idea, point to the diagram, then name the files that prove the feature exists. If time runs short, skip deep code details and keep the graphics and AI diagrams because they are the easiest way to show the engine workflow.

</details>

## Project

The final project is a custom RTS engine layer built on top of the Assignment 3 and Assignment 4 systems.

The base engine still provides object management, animation, collision, scene queries, and rendering. The RTS layer adds the game-specific rules that make the project feel like a strategy game instead of a single-object engine demo.

Key player-facing features:

- grid terrain with elevations and blocked cells
- unit selection and command issuing in `demo/RTSDemo.cpp`
- move, attack-move, patrol, guard, stop, hold, harvest, construct, and repair orders
- formation movement and queued orders
- worker economy and resource dropoff
- building placement, construction, repair, demolition, towers, and production queues
- projectile combat with role counters
- fog of war and visibility-limited AI
- multiple demo scenarios, including mass battle, pathfinding lab, siege, AI battle, and stress test

<details class="speaker-notes" markdown="1">
<summary>Detailed speaking notes</summary>

The project started from the assignment engine requirements, then grew into an RTS because RTS games naturally stress engine architecture. A unit cannot just move in a straight line. It has to understand terrain, buildings, enemy targets, player commands, formation slots, fog of war, and combat state. That gives every subsystem a reason to exist.

When presenting this slide, frame the project as an engine workflow demo instead of only a game demo. The game is the proof that the engine systems can coordinate. The most important custom layer is `RtsWorld`, because it turns lower-level systems into strategy-game rules.

</details>

## Engine Loop

The runtime is split into setup, simulation, and rendering work. The important design choice is that gameplay state is finalized before render commands are submitted.

```text
program start
  |
  v
load assets and register archetypes
  |
  v
create terrain, units, buildings, resources, lights
  |
  v
main loop
  |
  +--> read input
  |
  +--> translate input into RTS commands
  |
  +--> RtsWorld::update(dt)
  |       |
  |       +--> fog, timers, projectiles, towers
  |       +--> AI snapshot and command replay
  |       +--> production queues
  |       +--> unit order state machines
  |       +--> cleanup and win checks
  |
  +--> collect snapshots
  |
  +--> submit render commands
  |
  +--> draw frame
```

That structure makes `RtsWorld` the owner of rules and `RTSDemo` the owner of player input, camera control, UI drawing, and scenario setup.

<details class="speaker-notes" markdown="1">
<summary>Detailed speaking notes</summary>

This slide explains the high-level engine workflow. Input does not directly move units. Input becomes commands, commands are validated by the world, and the world update decides what actually happens. After the world is updated, rendering reads snapshots and final transforms.

That separation is useful because player input, AI, tests, and scripted demo setup can all use the same public command layer. It also makes rendering simpler: the renderer does not need to know why a unit moved, only where it ended up and what mesh state it should draw.

</details>

## RTS Simulation

`RtsWorld` is the central coordinator for the strategy layer. It owns or coordinates terrain, building occupancy, pathfinding, economy, production, AI, combat, fog of war, unit orders, and event reporting.

```text
RtsWorld
  |
  +-- TerrainGrid
  |     +-- elevation
  |     +-- traversable cells
  |
  +-- BuildingSystem
  |     +-- footprint occupancy
  |     +-- movement blockers
  |
  +-- RtsPathfinder
  |     +-- A* in grid space
  |     +-- line-of-sight checks
  |
  +-- RtsEconomySystem
  |     +-- team resources
  |     +-- resource nodes
  |
  +-- RtsProductionSystem
  |     +-- building queues
  |     +-- rally points
  |
  +-- RtsCombatSystem
  |     +-- projectiles
  |     +-- towers
  |
  +-- FogOfWar
  |     +-- explored / visible state
  |
  +-- RtsAiSystem
        +-- team profiles
        +-- high-level command output
```

The unit order pipeline is intentionally data driven. A command stores an `RtsOrderType` plus the target fields needed for that order. The update loop then interprets that active order.

```text
player or AI command
  |
  v
issueOrder / issueFormationOrder
  |
  v
validate order target
  |
  v
replace active order or push queue
  |
  v
each update:
  |
  +--> no active order?
  |       pop next queued order
  |
  +--> active move order?
  |       path or steer toward target
  |
  +--> active attack-move?
  |       focus target -> aggro target -> destination
  |
  +--> active harvest?
          node -> gather -> dropoff -> repeat
```

<details class="speaker-notes" markdown="1">
<summary>Detailed speaking notes</summary>

The RTS simulation is intentionally split into systems that match real gameplay concepts. `TerrainGrid` answers where cells are and whether a unit can stand there. `BuildingSystem` answers which cells are occupied. `RtsPathfinder` plans movement through that grid. Economy and production handle resources and queued units. Combat owns projectiles and towers. Fog of war controls what a team is allowed to know.

The order state machine is the core of unit behavior. Move orders try to reach a point. Attack-move orders interrupt movement to fight, then resume. Harvest orders are long-running loops: go to node, gather, return to dropoff, deposit, and repeat. This makes commands feel like RTS commands rather than one-frame actions.

</details>

## Graphics

The graphics requirement was extended into a two-pass deferred renderer. Instead of drawing and lighting every object in one pass, the engine first stores visible surface data in a G-buffer, then lights the stored pixels.

```text
visible render commands
  |
  v
geometry pass
  |
  +--> world position texture
  +--> world normal texture
  +--> albedo texture
  +--> depth renderbuffer
  |
  v
lighting pass
  |
  +--> fullscreen triangle
  +--> sample G-buffer
  +--> loop over active lights
  |
  v
final lit frame
```

Animated mesh data still flows through the same render command path. The CPU resolves animation playback into skin matrices, then the world vertex shader performs the weighted skinning before the G-buffer is written.

```text
meshbin + animbin
  |
  v
Shape + SkeletalRig + AnimationClip
  |
  v
GameObject animation state
  |
  v
current bone matrices
  |
  v
RenderCommand
  |
  v
world.vert skinning
  |
  v
deferred G-buffer
```

This is one of the main engine workflow points for the presentation: simulation and animation produce transforms and matrices; the renderer consumes those finished results.

<details class="speaker-notes" markdown="1">
<summary>Detailed speaking notes</summary>

The important graphics idea is that deferred rendering separates surface capture from lighting. The geometry pass writes position, normal, albedo, and depth into render targets. The lighting pass reads those textures and computes light contribution per pixel.

For the assignment, this shows render buffer management because the engine must create, bind, reuse, and resize the G-buffer attachments. For the RTS demo, it is useful because many units can share the same lighting path, and lights can be adjusted without redrawing all geometry for every light.

The animation workflow still fits into this path. CPU-side animation builds bone matrices. The vertex shader applies skinning before writing the deferred surface data, so skinned meshes and static meshes both end up in the same G-buffer.

</details>

## AI

The AI system does not mutate the world directly. It receives copied snapshots, makes team-level decisions, and emits high-level commands. `RtsWorld` then replays those commands through the same helper functions used by player input.

```text
RtsWorld::update
  |
  v
build RtsAiFrame
  |
  +-- unit snapshots
  +-- building snapshots
  +-- production snapshots
  +-- resource node snapshots
  +-- visibility callbacks
  |
  v
RtsAiSystem::update
  |
  +-- auto harvest
  +-- auto produce
  +-- scout when no target is visible
  +-- attack visible or remembered targets
  +-- retreat low health units
  |
  v
RtsAiCommand list
  |
  v
RtsWorld command replay
  |
  +-- issueHarvestOrder
  +-- issueFormationOrder
  +-- enqueueProduction
```

That separation is important because it keeps AI behavior testable and prevents the AI from bypassing game rules such as visibility, production cost, supply, and order validation.

<details class="speaker-notes" markdown="1">
<summary>Detailed speaking notes</summary>

The AI is built like a player that thinks periodically. It does not hold raw pointers into the world and it does not edit units directly. It receives `RtsAiFrame`, which is a collection of snapshots and visibility callbacks. Then it emits `RtsAiCommand` records such as harvest, move, attack-move, or enqueue production.

This avoids a common engine problem where AI gets a private shortcut around normal game rules. In this project, AI commands still go through `issueHarvestOrder`, `issueFormationOrder`, and `enqueueProduction`, so the same validation path applies to both human and computer control.

The AI also demonstrates software development tradeoffs. It is not trying to be a perfect strategy-game opponent. It is a useful engine feature because it drives harvesting, production, scouting, attacks, and retreat behavior without manual input.

</details>

## Tools

Custom and project-specific tools created or integrated for the workflow:

| Tool or workflow | Purpose | Files |
| --- | --- | --- |
| Blender mesh exporter | Converts Blender geometry into the engine mesh format. | `blender/meshbin.py` |
| Blender animation exporter | Converts Blender action data into clip data. | `blender/animbin.py` |
| Mesh discovery | Finds mesh assets and optional animation sidecars for demos. | `src/MeshDiscovery.*` |
| Archetype registration | Defines unit and building templates from code-level data. | `src/RtsTypes.h`, `demo/RTSDemo.cpp` |
| RTS world snapshots | Gives UI, renderer, tests, and AI read-only views of world state. | `src/RtsWorld.*` |
| Demo scenario modes | Runs focused demonstrations for RTS mechanics and stress cases. | `demo/RTSDemo.cpp` |
| CMake test targets | Verifies assignment objectives and RTS world behavior. | `CMakeLists.txt`, `tests/*.cpp` |

The workflow for adding content is:

```text
author asset or gameplay data
  |
  +--> Blender asset
  |       export .meshbin / .animbin
  |
  +--> RTS archetype
          register stats, cost, vision, role, production rules
  |
  v
place in scenario setup
  |
  v
exercise through demo mode or tests
```

<details class="speaker-notes" markdown="1">
<summary>Detailed speaking notes</summary>

The tool workflow matters because it explains how content reaches the engine. Blender is used for authoring, but the runtime is custom. The exporter scripts convert authored geometry and animation into the binary formats the engine can load. `MeshDiscovery` helps the demo find meshes and optional animation sidecars.

The other major tool is the code-level archetype system. Units and buildings are not all hard-coded as one-off objects. Their movement, health, cost, vision, production, combat role, and construction rules are template data copied into runtime state when the unit or building is created.

</details>

## Development Approach

The RTS features were developed as engine subsystems first, then connected to the demo layer. That kept the project from depending on one hard-coded presentation scene.

```text
define subsystem API
  |
  v
add focused C++ behavior
  |
  v
expose read-only snapshots
  |
  v
write tests or a focused demo scenario
  |
  v
connect to player input, AI, or rendering
  |
  v
use events and snapshots for UI feedback
```

The main software development choices were:

- use snapshots for AI, UI, and renderer-facing reads instead of exposing mutable world internals
- replay AI output through player command helpers so AI does not bypass validation
- keep unit and building definitions as archetypes so new content can be added without changing every system
- make each major behavior visible in a focused demo mode before combining it with the full RTS demo
- keep tests around the world layer because it is the most rules-heavy part of the project

<details class="speaker-notes" markdown="1">
<summary>Detailed speaking notes</summary>

The development approach was incremental. First, build a small subsystem API. Second, make it work in isolation. Third, expose snapshots so other layers can observe it without owning it. Fourth, add tests or a focused demo scenario. Finally, connect it into the full RTS demo.

This approach helped with debugging because each feature had a smaller proof point. For example, pathfinding can be demonstrated in a pathfinding lab, AI can be demonstrated in an AI-vs-AI mode, and battle performance can be demonstrated in a stress test.

</details>

## Requirements

The engine requirements are covered in two layers.

Assignment engine features:

- simulation update loop and object management
- animation playback and skeletal skinning
- collision broad phase and narrow phase work
- scene graph and spatial queries
- deferred rendering with render-buffer management
- math explanations and flow charts in the documentation site

RTS project features:

- terrain grid and A* pathfinding
- unit orders and formation movement
- combat with projectiles, towers, and damage events
- resources, harvesting, dropoff, supply, and production
- building placement, construction, repair, and blockers
- fog of war
- autonomous AI using the same command layer as the player

<details class="speaker-notes" markdown="1">
<summary>Detailed speaking notes</summary>

Use this slide to connect the project back to grading requirements. The assignment engine features are still present: update loop, object state, animation, collision, scene graph queries, and deferred rendering. The RTS layer is the custom project extension that turns those engine pieces into gameplay.

The strongest point to make is that the project did not replace the assignment with an external engine. The runtime systems are custom C++, while Blender is used only as an asset authoring and export tool.

</details>

## Deviations

The project deviates from a narrow assignment demo by turning the engine pieces into a larger strategy-game workflow.

| Original feature area | Project-specific deviation |
| --- | --- |
| Basic object movement | RTS unit orders, command queues, pathfinding, formation offsets, and steering. |
| Basic collision or overlap events | Terrain and building blockers plus unit spacing and combat ranges. |
| Basic animation showcase | Exported mesh and animation workflow with optional sidecar clips. |
| Basic rendering path | Deferred G-buffer renderer with resize-aware framebuffer management. |
| Single demo scene | Multiple RTS scenarios for pathfinding, battle, siege, AI, and stress testing. |
| Manual-only behavior | Team AI that harvests, produces, scouts, attacks, and retreats from snapshots. |
| External engine tooling | Blender is used as an asset authoring tool, but the runtime engine and RTS rules are custom C++. |

The biggest production challenge was keeping systems separated enough that they could grow without becoming a single untestable demo file. The current split keeps the world rules in `RtsWorld`, visual and input presentation in `RTSDemo`, reusable rendering in `DeferredRenderer`, and content conversion in the Blender exporter scripts.

<details class="speaker-notes" markdown="1">
<summary>Detailed speaking notes</summary>

The main deviation is scope. A base assignment demo can show a controlled scene with a few objects. This project has to handle many units and buildings that continuously change state. That forced additional systems: formation movement, resource loops, production queues, fog-aware AI, and scenario modes.

The main challenge was avoiding a single file where input, AI, combat, rendering, and economy all directly modify each other. The final structure is not perfect, but it has clear ownership: `RtsWorld` owns rules, `RTSDemo` owns interaction and scenario presentation, `DeferredRenderer` owns the graphics pass, and the Blender scripts own asset conversion.

</details>
