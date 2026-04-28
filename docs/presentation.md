---
layout: default
title: Presentation
presentation_deck: true
---

# RTS Engine Presentation

This page is organized as a 15 minute presentation outline for explaining the project, the custom RTS systems, the tool workflow, and the deviations from the original assignment features.

<nav class="presentation-tabs" aria-label="Presentation sections">
  <a href="#talk-plan">Talk Plan</a>
  <a href="#project-thesis">Thesis</a>
  <a href="#project">Project</a>
  <a href="#live-demo-plan">Demo Plan</a>
  <a href="#demo-catalog">Demos</a>
  <a href="#design-strategy">Design Strategy</a>
  <a href="#code-map">Code Map</a>
  <a href="#engine-loop">Engine Loop</a>
  <a href="#backend-engine-systems">Backend</a>
  <a href="#command-flow">Commands</a>
  <a href="#rts-simulation">RTS Simulation</a>
  <a href="#graphics">Graphics</a>
  <a href="#ai">AI</a>
  <a href="#tools">Tools</a>
  <a href="#development-approach">Development</a>
  <a href="#requirements">Requirements</a>
  <a href="#deviations">Deviations</a>
  <a href="#production-challenges">Challenges</a>
  <a href="#custom-vs-external-tools">Custom vs External</a>
  <a href="#closing-summary">Closing</a>
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
| 0:00-0:45 | Project thesis | State the project goal and the main technical claim. |
| 0:45-1:30 | Project overview | Explain the RTS feature set and why it stresses the engine. |
| 1:30-2:30 | Live demo | Show unit commands, harvesting/production, combat, fog, and one alternate scenario. |
| 2:30-3:00 | Demo catalog | Explain why each demo exists and what observation it supports. |
| 3:00-4:15 | Design strategy | Explain ownership boundaries, data flow, and why the RTS layer is split into subsystems. |
| 4:15-5:00 | Code map | Point out where the important systems live in the repository. |
| 5:00-5:45 | Engine workflow | Show how setup, update, simulation, and render submission are separated. |
| 5:45-6:30 | Backend systems | Explain scene graph, BVH, broad-phase filtering, and backend ownership. |
| 6:30-7:15 | Command flow | Explain how player input and AI both become validated world orders. |
| 7:15-8:30 | RTS systems | Explain terrain, orders, pathfinding, combat, economy, buildings, production, fog, and events. |
| 8:30-9:45 | Graphics pipeline | Explain the deferred rendering pass and how animated meshes reach the GPU. |
| 9:45-10:45 | AI workflow | Explain snapshot-driven team AI, command emission, and replay through normal player command APIs. |
| 10:45-11:45 | Tools | Explain Blender exporters, mesh discovery, archetype configuration, demo modes, launcher, and test targets. |
| 11:45-12:45 | Development approach | Explain how features were added, tested, and deployed into demos. |
| 12:45-13:45 | Challenges | Explain the main production problems and how the design addressed them. |
| 13:45-14:30 | Deviations and external tools | Explain what changed from the assignment baseline and what is custom versus external. |
| 14:30-15:00 | Closing | Summarize what the final product proves. |

<details class="speaker-notes" markdown="1">
<summary>Detailed speaking notes</summary>

Use this as the pacing slide. The goal is not to read every bullet on later slides. Treat each section as a checkpoint: explain the idea, point to the diagram, then name the files that prove the feature exists. If time runs short, skip deep code details and keep the graphics and AI diagrams because they are the easiest way to show the engine workflow.

</details>

## Project Thesis

This project extends the assignment engine into a custom RTS engine workflow.

The main technical claim:

> The final product demonstrates that the engine can coordinate many independent systems: unit commands, terrain pathfinding, buildings, economy, production, combat, fog of war, team AI, asset tooling, animation, and deferred rendering.

What the presentation proves:

- the project is not just a one-off demo scene
- the RTS game behavior is built from reusable engine subsystems
- player input and AI both use the same command validation path
- tools and data flow are part of the engine workflow, not separate afterthoughts
- the final implementation intentionally expands the original assignment features into a larger engine design

```text
assignment engine foundation
  |
  +-- object update
  +-- animation
  +-- collision and scene queries
  +-- rendering
  |
  v
custom RTS engine layer
  |
  +-- commands, pathfinding, economy, combat, AI
  +-- tools, archetypes, scenarios, events, snapshots
```

<details class="speaker-notes" markdown="1">
<summary>Detailed speaking notes</summary>

Start with the thesis because it gives the audience a lens for the rest of the talk. The project is not only "I made an RTS." The stronger claim is "I used an RTS to prove that the engine workflow can handle many interacting systems."

For a non-technical audience, explain that RTS games are demanding because many units act at once and every decision depends on the map, resources, enemies, visibility, and time. For a technical audience, explain that the project is about data flow and subsystem boundaries: commands go in, the world updates authoritative state, snapshots and events come out, and rendering consumes the finished result.

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

## Live Demo Plan

The live demo should show the grader the engine features in a controlled order.

Recommended 90 second demo path:

| Demo step | What to show | What to say |
| --- | --- | --- |
| 1 | Start the main RTS demo. | The project runs as a custom C++ RTS demo, not an external engine scene. |
| 2 | Select a small group of units and issue a move command. | Player input becomes a validated `RtsWorld` command. |
| 3 | Issue attack-move toward enemies. | Units can interrupt movement to fight, then continue toward their objective. |
| 4 | Show workers harvesting and returning resources. | Harvest is a long-running order loop: resource node, carry, dropoff, repeat. |
| 5 | Queue production from a building. | Buildings own production queues and rally points. |
| 6 | Show projectiles, towers, or combat events. | Combat is resolved through projectile state and world events. |
| 7 | Point out fog or visibility. | AI and UI can be limited by what a team has explored or can currently see. |
| 8 | Switch to Economy Race or Tool Pipeline if explaining observation demos. | Economy Race isolates harvest/production scaling; Tool Pipeline isolates mesh/animation/render workflow. |
| 9 | Switch to AI battle, siege, pathfinding lab, or stress mode if explaining stress cases. | Scenario modes isolate specific engine features for testing and presentation. |

Short demo flow:

```text
select units
  -> move command
  -> attack-move command
  -> worker harvest loop
  -> production queue
  -> combat/projectiles
  -> economy race or tool pipeline observation demo
```

If time is short, prioritize:

- one player command
- one economy or production action
- one combat moment
- one observation scenario such as Economy Race or Tool Pipeline

<details class="speaker-notes" markdown="1">
<summary>Detailed speaking notes</summary>

Use the demo as evidence, not as the whole presentation. Avoid spending too much time playing. The goal is to show one example of each major engine feature and then connect it back to the code diagrams.

The best phrasing during the demo is "what you are seeing on screen comes from this workflow." For example: the right click does not teleport units. It becomes an order, the world validates it, the pathfinder and steering move units over time, attack-move checks enemies, combat spawns projectiles, and rendering draws the updated snapshots.

Have one fallback path ready. If the demo takes too long, skip building placement and show AI battle or stress mode because those quickly prove the engine can run without constant manual input.

</details>

## Demo Catalog

The demos are not random executables. Each one isolates a specific engine observation so the presentation can show one feature clearly without explaining the entire game every time.

Use `./run_demo.sh` to launch the selector.

| Selector target | Observation goal | What to point out |
| --- | --- | --- |
| `assignment_1` | Assignment 3 and 4 showcase. | Managed objects, animation sidecars, collision callbacks, scene graph culling, and deferred lights. |
| `rts_demo` | Full player-facing RTS demo. | Selection, commands, buildings, economy, production, combat, fog, and AI in one playable scenario. |
| `rts_mass_battle_demo` | Large battle and movement spectacle. | Many units, formation attack-move orders, multiple routes, crowd spacing, and combat events. |
| `pathfinding_lab_demo` | Route planning through constrained terrain. | Maze-like blockers, water/rock gates, queued routes, A* pathfinding, and line-of-sight movement. |
| `building_siege_demo` | Defensive structures and repair pressure. | Towers, repairs, artillery pressure, building health, construction/repair behavior, and combat feedback. |
| `ai_vs_ai_battle_demo` | Autonomous strategy behavior. | Both teams harvest, produce, scout, attack, retreat, and replay AI commands through normal world APIs. |
| `economy_race_demo` | Economy and production scaling without combat noise. | Workers harvest/drop off ore, depots and barracks queue units, farms provide supply, and both teams scale. |
| `rts_stress_test_demo` | High-count simulation load. | 512 units, three lanes, collision/formation pressure, and frame-to-frame order updates. |
| `tool_pipeline_demo` | Asset/tool/render workflow observation. | `.meshbin` discovery, optional `.animbin` loading, `Shape` creation, animation matrices, scene graph submission, and deferred rendering. |
| `doom_demo` | Separate gameplay style on the same engine base. | Reusable mesh, renderer, scene graph, sound, and input systems applied outside the RTS format. |

The two newest observation demos cover specific rubric needs:

```text
Economy Race Demo
  -> workers
  -> resource nodes
  -> dropoff buildings
  -> team ore balances
  -> production queues
  -> supply scaling
```

```text
Tool Pipeline Demo
  -> Blender asset
  -> meshbin / animbin export
  -> MeshDiscovery
  -> MeshLoader / AnimationClip
  -> RenderCommand
  -> DeferredRenderer
```

<details class="speaker-notes" markdown="1">
<summary>Detailed speaking notes</summary>

This slide explains why there are multiple demos instead of one giant demo. A large RTS scenario is impressive, but it can hide the details. The focused demos are observation tools. They let you point at one subsystem and say, "This is the feature isolated."

For Economy Race, emphasize that combat is intentionally removed from the AI profile. That makes the resource loop easier to observe: workers harvest, return to depots, ore changes, production queues fill, supply matters, and both teams scale over time.

For Tool Pipeline, emphasize that it is not an RTS combat test. It is about the asset-to-render path: exported mesh and animation data come from Blender, the runtime discovers and loads those files, the engine creates renderable shapes, animation sidecars produce matrices when available, and the deferred renderer draws the final scene.

</details>

## Design Strategy

The overall design strategy is to keep the project split into layers that each have one clear job.

Plain-English version:

- `RTSDemo` is the stage manager. It handles the window, camera, mouse clicks, keyboard shortcuts, HUD, and which scenario is running.
- `RtsWorld` is the rulebook. It decides which commands are valid and how the game state changes over time.
- The subsystem classes are specialists. Terrain knows cells, pathfinding knows routes, combat knows projectiles, economy knows resources, and production knows build queues.
- The renderer is the camera crew. It does not decide gameplay. It draws the latest state after the world finishes updating.

Code-level version:

| Layer | Responsibility | Main files |
| --- | --- | --- |
| Demo / presentation | Input, camera, UI, scenario setup, drawing overlays. | `demo/RTSDemo.cpp` |
| RTS rules | Owns units, buildings, events, orders, AI integration, win/loss checks. | `src/RtsWorld.h`, `src/RtsWorld.cpp` |
| Shared RTS data | Order types, archetypes, snapshots, event payloads. | `src/RtsTypes.h` |
| Movement | Terrain grid, building blockers, A* paths, line-of-sight checks. | `src/TerrainGrid.*`, `src/BuildingSystem.*`, `src/RtsPathfinder.*` |
| Combat and economy | Projectile resolution, tower attacks, resources, harvesting, production. | `src/RtsCombat.*`, `src/RtsEconomy.*`, `src/RtsProduction.*` |
| AI | Read-only frame snapshots, team profiles, high-level commands. | `src/RtsAi.*` |
| Rendering | Meshes, shaders, textures, deferred G-buffer, final lighting pass. | `src/DeferredRenderer.*`, `src/Renderer3D.*`, `src/shaders/*` |

The most important design rule is that systems communicate through commands, snapshots, events, and small data records instead of freely reaching into each other's private state.

```text
mutable game state stays inside RtsWorld
  |
  +--> commands request changes
  +--> snapshots expose read-only state
  +--> events describe what happened this frame
  +--> render commands describe what to draw
```

<details class="speaker-notes" markdown="1">
<summary>Detailed speaking notes</summary>

For a general audience, describe this as a separation-of-jobs problem. If every part of the program can edit every other part, debugging becomes difficult because no one knows who changed the state. This project avoids that by putting the rules in `RtsWorld` and making other systems ask for changes through public functions.

For a code audience, emphasize ownership. `RtsWorld` owns the authoritative state for units and buildings. `RTSDemo` can request actions, but it does not directly rewrite a unit's internal order state. `RtsAiSystem` can decide what it wants, but it only returns commands. The renderer can draw snapshots, but it does not decide whether a unit is alive.

The strategy is not about adding more classes for the sake of adding classes. It is about controlling where bugs can happen. If production fails, look in `RtsProduction` and the `RtsWorld` functions that call it. If a unit will not walk around a building, look at terrain, building occupancy, and `RtsPathfinder`. If an AI unit makes a bad decision, inspect the snapshot and command output.

</details>

## Code Map

This slide is the quick guide to where someone should look in the repository while following the presentation.

| Question | Where to look | What that code does |
| --- | --- | --- |
| How does the demo start? | `demo/RTSDemo.cpp` | Creates the RTS scenario, handles input, calls world update, and draws UI overlays. |
| Where are unit and building rules stored? | `src/RtsTypes.h` | Defines order enums, archetype structs, snapshots, combat roles, and event types. |
| Where does the simulation tick happen? | `src/RtsWorld.cpp` | Runs fog, cooldowns, projectiles, towers, AI commands, production, orders, cleanup, and victory checks. |
| How does a player command become behavior? | `RtsWorld::issueOrder`, `RtsWorld::issueFormationOrder`, `RtsWorld::updateActiveOrder` | Validates the request, stores it on units, and advances it each frame. |
| Where is movement planned? | `src/RtsPathfinder.cpp` | Uses terrain and building occupancy to find grid paths and line-of-sight movement. |
| Where is AI decided? | `src/RtsAi.cpp` | Reads `RtsAiFrame` snapshots and emits commands like harvest, attack-move, and production. |
| Where are projectiles and towers resolved? | `src/RtsCombat.cpp`, `RtsWorld::tryAttackUnit`, `RtsWorld::tryAttackBuilding` | Spawns projectiles, resolves hits, applies damage, and emits combat events. |
| Where is deferred rendering implemented? | `src/DeferredRenderer.cpp`, `src/shaders/deferred_*` | Creates the G-buffer, draws geometry, then performs lighting on a fullscreen pass. |

The practical reading order for code is:

```text
RtsTypes.h
  -> RtsWorld.h
  -> RtsWorld::update
  -> issueOrder / issueFormationOrder
  -> updateActiveOrder
  -> RtsAiSystem::update
  -> DeferredRenderer::drawQueue
```

<details class="speaker-notes" markdown="1">
<summary>Detailed speaking notes</summary>

This is the slide to use if someone asks, "Where is the actual work?" The answer is that the demo file is large because it owns interaction and presentation, but the reusable rules live in `src/`.

Explain that `RtsTypes.h` is the vocabulary. It defines what an order is, what an archetype is, what a snapshot is, and what an event looks like. `RtsWorld` uses that vocabulary to implement the rules. `RTSDemo` uses the same vocabulary to build interface behavior and scenario setup.

Also point out that this structure makes the project easier to explain during grading. You can show a feature in the demo, then jump to the responsible file instead of hunting through unrelated rendering or input code.

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

## Backend Engine Systems

This project has gameplay-facing systems, but it also has backend engine systems that make the demos practical to run and explain. The most important backend example is the scene graph and BVH spatial query path.

Plain-English version:

- The engine keeps track of where objects are in the world.
- Instead of asking "does this object interact with every other object?", the backend first asks "which objects are even near this area?"
- That produces a smaller candidate list.
- Rendering, collision, and gameplay logic then do the more expensive exact work only on those candidates.

Scene graph and BVH responsibilities:

- `SceneGraph` stores scene objects spatially.
- It supports broad spatial queries instead of checking every object against every other object.
- It is used for render-side visibility/culling and collision or broad-phase style queries.
- It lets the engine separate "find possible objects" from "do final detailed work."

```text
all objects
  |
  v
BVH partitions world space
  |
  +-- query visible region
  +-- query AABB overlap region
  |
  v
small candidate set
```

Why BVH matters:

- Without a broad phase, rendering and collision-style checks trend toward checking everything.
- With a BVH, the engine narrows the list before exact collision, render submission, or gameplay response.
- This matters more as the scene grows from a few objects into many RTS units, buildings, projectiles, and debug/demo objects.

Backend update flow:

```text
GameObject transform changes
  |
  v
world bounds refresh
  |
  v
SceneGraph / BVH update
  |
  +--> render visibility query
  |
  +--> collision candidate query
  |
  v
renderer or collision system handles final work
```

Backend ownership:

| Backend area | Owner | Job |
| --- | --- | --- |
| Transform and bounds | `GameObject` | Stores object transform state and world-space bounds. |
| Active engine objects | `Utility.cpp` | Owns active managed objects, updates them, and dispatches collision responses. |
| Spatial partitioning | `SceneGraph.cpp` | Maintains the scene graph/BVH and answers spatial queries. |
| Rendering backend | `DeferredRenderer.cpp` | Owns G-buffer state, geometry pass, lighting pass, and render command execution. |
| RTS simulation backend | `RtsWorld.cpp` | Owns RTS-specific mutable state separately from low-level engine object state. |

The design point is that backend systems reduce the amount of work that high-level systems have to do. Gameplay code can ask for meaningful results without manually scanning every object in the world.

<details class="speaker-notes" markdown="1">
<summary>Detailed speaking notes</summary>

This slide is useful because it proves the project is not only gameplay scripting. A BVH is an engine structure for managing spatial complexity. It does not decide game rules by itself. It helps other systems ask better questions.

For a general audience, compare it to sorting objects into boxes. If you are looking for objects near the camera, you do not inspect the whole world one object at a time. You first inspect the relevant boxes. For a code audience, explain that this is broad-phase filtering: the BVH gives a possible set, and then the renderer or collision logic does the exact work.

Tie it back to the demos. The assignment showcase uses scene queries before render submission. Collision work uses broad-phase candidates before more exact checks. RTS demos benefit from the same backend idea because many units and buildings exist at once.

Code callout:

```text
SceneGraph::createNode
  -> sync_scene_graph_with_objects
  -> SceneGraph::render / queryAabb
  -> candidate ids
  -> render submission or collision exact work
```

Files to mention:

- `src/SceneGraph.cpp`
- `src/SceneGraph.h`
- `src/Utility.cpp`
- `src/main.cpp`

</details>

## Command Flow

Commands are the bridge between input, AI, and simulation. A mouse click, keyboard shortcut, scripted scenario, or AI decision all become the same kind of world request.

```text
human input in RTSDemo
  |
  +-- right click move / attack / harvest / repair
  +-- hotkeys for attack-move, patrol, guard, stop, hold
  +-- building placement or production buttons
  |
  v
RtsWorld public command functions
  |
  +-- issueOrder
  +-- issueFormationOrder
  +-- issueHarvestOrder
  +-- issueRepairOrder
  +-- enqueueProduction
  |
  v
validated UnitState or building queue change
  |
  v
RtsWorld::update advances the result over time
```

AI uses the same path:

```text
RtsAiSystem::update
  |
  v
RtsAiCommand records
  |
  v
RtsWorld command replay switch
  |
  v
same issueOrder / issueFormationOrder / enqueueProduction functions
```

That means the command system is not only an input feature. It is the shared API for every system that wants to change RTS behavior.

<details class="speaker-notes" markdown="1">
<summary>Detailed speaking notes</summary>

For non-programmers, describe a command as an instruction card. The player or AI fills out the card: "move these units here" or "train this unit at this building." `RtsWorld` reads the card, checks whether it is valid, and stores it if the unit or building can actually do it.

For programmers, the key types are `RtsOrder`, `RtsOrderType`, and `RtsAiCommand`. `RtsOrder` is stored on units as active or queued behavior. `RtsAiCommand` is temporary output from the AI. The AI command is converted back into normal `RtsWorld` calls so the AI does not get special privileges.

The command flow also keeps frame updates clean. A right click does not simulate an entire attack. It stores intent. The world update then advances movement, target checks, cooldowns, projectile spawns, and completion over many frames.

Code callout:

```text
RTSDemo input handling
  -> RtsWorld::issueOrder
  -> RtsWorld::issueFormationOrder
  -> UnitState::active_order / UnitState::order_queue
  -> RtsWorld::startNextQueuedOrder
  -> RtsWorld::updateActiveOrder
```

Important files:

- `demo/RTSDemo.cpp`
- `src/RtsWorld.cpp`
- `src/RtsWorld.h`
- `src/RtsTypes.h`

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

Inside `RtsWorld::update`, the simulation order matters because later systems depend on earlier results.

| Update step | Why it happens there |
| --- | --- |
| Clear last-frame events | Events describe the current frame only, so old events cannot leak into new UI or test reads. |
| Refresh fog of war | AI and UI need current visibility before choosing actions or drawing state. |
| Decay timers | Attack cooldowns, harvest cooldowns, and hit feedback timers all tick independently of orders. |
| Update projectiles | Existing shots may kill units or destroy buildings before new orders run. |
| Clean destroyed entities | Dead units and destroyed buildings should not receive new AI or order logic. |
| Update towers | Towers choose targets from the surviving world state. |
| Run AI | AI receives snapshots and returns high-level commands. |
| Replay AI commands | AI decisions are converted into the same command helpers used by players. |
| Update production | Queued units advance and may spawn near rally points. |
| Advance unit orders | Units move, attack, harvest, construct, repair, patrol, or guard. |
| Cleanup and win checks | End-of-frame state is made consistent before the next frame. |

Key state records:

- `UnitState` is the private mutable runtime record for one unit.
- `OwnedBuildingState` tracks team ownership, construction, health, and victory relevance for buildings.
- `RtsWorldUnitSnapshot` and `RtsWorldBuildingSnapshot` expose read-only state to UI, AI, renderer helpers, and tests.
- `RtsEvent` reports what happened this frame, such as projectile hits, resources deposited, production completed, or units dying.

<details class="speaker-notes" markdown="1">
<summary>Detailed speaking notes</summary>

The RTS simulation is intentionally split into systems that match real gameplay concepts. `TerrainGrid` answers where cells are and whether a unit can stand there. `BuildingSystem` answers which cells are occupied. `RtsPathfinder` plans movement through that grid. Economy and production handle resources and queued units. Combat owns projectiles and towers. Fog of war controls what a team is allowed to know.

The order state machine is the core of unit behavior. Move orders try to reach a point. Attack-move orders interrupt movement to fight, then resume. Harvest orders are long-running loops: go to node, gather, return to dropoff, deposit, and repeat. This makes commands feel like RTS commands rather than one-frame actions.

Point out that this is why `RtsWorld::update` is the best single function to explain the engine. It shows the entire simulation pipeline in order. Even if the audience does not know C++, they can understand the idea that the engine repeatedly refreshes visibility, resolves existing combat, accepts new decisions, advances queues, moves units, and cleans up results.

Economy and production code callout:

```text
worker harvest loop
  -> RtsWorld::handleHarvestOrder
  -> RtsEconomySystem::harvestResourceNode
  -> UnitState::carried_resource_amount
  -> RtsWorld::addTeamResourceAmount on dropoff
  -> RtsEventType::resources_deposited
```

```text
building production queue
  -> RtsWorld::enqueueProduction
  -> RtsProductionSystem::enqueueProduction
  -> RtsProductionSystem::update
  -> RtsWorld::spawnProducedUnit
  -> RtsEventType::production_completed
```

Good files to open if asked:

- `src/RtsWorld.cpp`
- `src/RtsEconomy.cpp`
- `src/RtsProduction.cpp`
- `src/RtsTypes.h`

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

Code-level graphics responsibilities:

| Code path | Job |
| --- | --- |
| `DeferredRenderer::initialize` | Loads geometry and lighting shaders, loads a texture or fallback checker, and caches uniform locations. |
| `DeferredRenderer::beginFrame` | Records the viewport size, ensures the G-buffer exists, binds it, and clears it for the geometry pass. |
| `DeferredRenderer::enqueue` | Collects one `RenderCommand` per visible object without drawing immediately. |
| `DeferredRenderer::drawQueue` | Draws queued geometry into the G-buffer, then draws the lighting pass to the default framebuffer. |
| `ensureFrameBuffers` | Allocates or reallocates position, normal, albedo, and depth attachments when the window size changes. |
| `src/shaders/world.vert` | Applies model/view/projection transforms and optional GPU skinning. |
| `src/shaders/deferred_gbuffer.frag` | Writes world-space surface data into the G-buffer. |
| `src/shaders/deferred_light.frag` | Reads the G-buffer and computes ambient plus per-light diffuse contribution. |

The design goal is that render submission is data-oriented. The renderer receives a queue of commands with mesh, transform, camera, texture, skinning, and light information. It does not own the game rules that produced those values.

<details class="speaker-notes" markdown="1">
<summary>Detailed speaking notes</summary>

The important graphics idea is that deferred rendering separates surface capture from lighting. The geometry pass writes position, normal, albedo, and depth into render targets. The lighting pass reads those textures and computes light contribution per pixel.

For the assignment, this shows render buffer management because the engine must create, bind, reuse, and resize the G-buffer attachments. For the RTS demo, it is useful because many units can share the same lighting path, and lights can be adjusted without redrawing all geometry for every light.

The animation workflow still fits into this path. CPU-side animation builds bone matrices. The vertex shader applies skinning before writing the deferred surface data, so skinned meshes and static meshes both end up in the same G-buffer.

A useful explanation for everyone is: the first graphics pass records what the camera sees, and the second pass decides how light affects it. That is different from a forward renderer where every object is drawn with lighting immediately. This project uses the deferred approach because it demonstrates render-buffer management and makes the light workflow easier to explain.

Deferred renderer code callout:

```text
DeferredRenderer::beginFrame
  -> ensureFrameBuffers
  -> bind G-buffer framebuffer
  -> geometry pass writes position / normal / albedo
  -> DeferredRenderer::drawQueue
  -> lighting pass samples G-buffer
  -> fullscreen triangle to default framebuffer
```

Tool pipeline render callout:

```text
discover_meshbins
  -> load_mesh_from_meshbin
  -> load_animation_for_mesh
  -> Shape / AnimationClip
  -> RenderItem
  -> SceneGraph node
  -> DeferredRenderer::enqueue
  -> DeferredRenderer::drawQueue
```

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

The AI is controlled by `RtsAiProfile`, which acts like a behavior configuration for each team.

| AI concern | How it is handled |
| --- | --- |
| Workers | Idle workers can receive harvest commands when `auto_harvest` is enabled. |
| Production | Empty production buildings can queue units from a priority list. |
| Scouting | If no enemy is visible, the AI can move toward candidate scouting points. |
| Attacking | Combat units are grouped into attack-move commands toward visible or remembered targets. |
| Retreat | Low-health units can move back toward the team anchor instead of staying in combat. |
| Visibility | Enemy units, buildings, and resources are filtered through fog-of-war callbacks. |
| Reissue spam | Recent group commands are remembered briefly so the AI does not repeat identical orders every think step. |

The AI is deliberately "director style" instead of "unit brain style." Individual units still run their own order state machine inside `RtsWorld`; the AI mostly decides which high-level commands a team should issue next.

<details class="speaker-notes" markdown="1">
<summary>Detailed speaking notes</summary>

The AI is built like a player that thinks periodically. It does not hold raw pointers into the world and it does not edit units directly. It receives `RtsAiFrame`, which is a collection of snapshots and visibility callbacks. Then it emits `RtsAiCommand` records such as harvest, move, attack-move, or enqueue production.

This avoids a common engine problem where AI gets a private shortcut around normal game rules. In this project, AI commands still go through `issueHarvestOrder`, `issueFormationOrder`, and `enqueueProduction`, so the same validation path applies to both human and computer control.

The AI also demonstrates software development tradeoffs. It is not trying to be a perfect strategy-game opponent. It is a useful engine feature because it drives harvesting, production, scouting, attacks, and retreat behavior without manual input.

The phrase "director style AI" is helpful in the presentation. It means the AI is not calculating every footstep for every unit. It decides team-level intent: collect resources, train units, scout, attack, or retreat. Once it issues that intent, the normal movement, pathfinding, and combat code takes over.

AI code callout:

```text
RtsWorld::update
  -> build RtsAiFrame
  -> RtsAiSystem::update
  -> std::vector<RtsAiCommand>
  -> switch command.type
  -> issueHarvestOrder / issueFormationOrder / enqueueProduction
```

Important types:

- `RtsAiProfile`
- `RtsAiFrame`
- `RtsAiCommand`
- `RtsWorldUnitSnapshot`
- `RtsWorldBuildingSnapshot`
- `RtsWorldProductionSnapshot`

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
| Demo selector | Launches showcase, RTS variants, observation demos, and tool pipeline demo from one terminal menu. | `run_demo.sh` |
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

The most important tool boundary is that Blender is an authoring tool, not the runtime engine. The custom runtime still loads the exported data, owns the simulation, and renders the frame.

Practical workflow examples:

| Goal | Workflow |
| --- | --- |
| Add a static mesh | Author in Blender, export `.meshbin`, let mesh discovery find it, then instantiate a `Shape` in a demo. |
| Add an animated mesh | Export `.meshbin` and matching `.animbin`, load the rig and clip, then submit skin matrices through render commands. |
| Add a new unit type | Add or register an `RtsUnitArchetype` with health, speed, cost, production time, vision, role, and combat stats. |
| Add a new building type | Register an `RtsBuildingArchetype` with footprint, cost, supply, production options, tower stats, or dropoff rules. |
| Add a scenario | Use `RTSDemo.cpp` setup code to place terrain, blockers, units, buildings, resources, and starting orders. |
| Observe economy scaling | Launch `economy_race_demo` to watch harvest, dropoff, production queues, and supply without combat pressure. |
| Observe asset workflow | Launch `tool_pipeline_demo` to watch exported mesh/animation assets become render commands. |
| Verify behavior | Run focused tests or use scenario modes that isolate pathfinding, battle, siege, AI, or stress behavior. |

<details class="speaker-notes" markdown="1">
<summary>Detailed speaking notes</summary>

The tool workflow matters because it explains how content reaches the engine. Blender is used for authoring, but the runtime is custom. The exporter scripts convert authored geometry and animation into the binary formats the engine can load. `MeshDiscovery` helps the demo find meshes and optional animation sidecars.

The other major tool is the code-level archetype system. Units and buildings are not all hard-coded as one-off objects. Their movement, health, cost, vision, production, combat role, and construction rules are template data copied into runtime state when the unit or building is created.

For an audience that does not know asset pipelines, explain that Blender is where the art is made, the exporter is the translator, and the C++ engine is where the translated asset becomes part of the game. For a programming audience, explain that this keeps authoring format concerns out of the runtime renderer.

The launcher script is also a small tool. It makes the demos easier to present because you do not need to remember every executable name. That matters in a timed presentation: the selector turns the demo set into a usable presentation workflow.

Tool-pipeline code path to mention:

```text
src/main.cpp
  -> build_default_showcase_mesh_paths or discover_meshbins
  -> load_showcase_item
  -> load_mesh_from_meshbin
  -> load_animation_for_mesh
  -> spawnRtsGameObject
  -> sync_render_item_animation_state
  -> scene_graph.createNode
  -> render_showcase_window
```

This is the clearest code path for explaining how exported assets become visible runtime objects.

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

The design pattern repeated across the project is:

```text
data definition
  -> validation
  -> private mutable runtime state
  -> read-only snapshot
  -> event output for UI/tests
```

Examples:

| Feature | Data definition | Runtime state | Public read |
| --- | --- | --- | --- |
| Units | `RtsUnitArchetype` | `UnitState` | `RtsWorldUnitSnapshot` |
| Buildings | `RtsBuildingArchetype` | `OwnedBuildingState` plus `BuildingInstance` | `RtsWorldBuildingSnapshot` |
| Production | producible unit list and costs | queue entries inside `RtsProductionSystem` | `RtsWorldProductionSnapshot` |
| AI | `RtsAiProfile` | per-team AI memory | emitted `RtsAiCommand` list |
| Combat | attack stats and projectile speed | active projectiles and tower cooldowns | `RtsEvent` and projectile snapshots |

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

## Production Challenges

The hardest part of producing the final product was not one isolated algorithm. The hard part was making many systems cooperate without losing track of ownership.

| Challenge | Why it was hard | Project response |
| --- | --- | --- |
| Many systems needed the same state | Units, AI, UI, combat, fog, and rendering all care about unit and building state. | Keep authoritative mutable state in `RtsWorld`; expose snapshots for reading. |
| Player and AI commands could diverge | AI could become unfair or buggy if it bypassed normal rules. | AI emits commands, then `RtsWorld` replays them through the same helpers used by player input. |
| Pathfinding had to respect terrain and buildings | Units should not walk through blocked terrain or placed structures. | `RtsPathfinder` reads `TerrainGrid` and `BuildingSystem` together. |
| RTS orders last across many frames | Harvest, patrol, guard, attack-move, construction, and repair are not one-frame actions. | Units store active and queued `RtsOrder` data and advance through `updateActiveOrder`. |
| Combat needed visible feedback and clean state changes | Damage, death, projectiles, and tower attacks can happen while units are moving. | `RtsCombatSystem` owns projectile/tower logic while `RtsWorld` applies damage and emits events. |
| Rendering should not control simulation | It is tempting to let demo drawing code decide game behavior because it already sees everything. | Render from snapshots and render commands after simulation finishes. |
| Large demo file pressure | UI, camera, scenario setup, hotkeys, and overlays create a lot of demo-side code. | Keep core reusable rules in `src/` and treat `RTSDemo.cpp` as the presentation layer. |
| Explaining scope in 15 minutes | The project has more systems than can be shown deeply in one talk. | Use slides, diagrams, a short demo path, and speaker notes to move from overview to code detail. |

The main debugging strategy was to isolate features:

```text
build feature in subsystem
  |
  v
test or demo it in isolation
  |
  v
connect it to commands / snapshots / events
  |
  v
combine it into the full RTS demo
```

Examples:

- pathfinding can be shown in the pathfinding lab scenario
- AI can be shown in AI-vs-AI mode
- mass behavior can be shown in battle or stress scenarios
- production and economy can be shown through building queues and worker harvest loops
- rendering can be explained through the G-buffer diagram independently of gameplay

<details class="speaker-notes" markdown="1">
<summary>Detailed speaking notes</summary>

This slide directly answers the "challenges in producing your final product" requirement. Do not only say that the project was difficult. Be specific: the challenge was integration. Every system had to coordinate with other systems while still keeping its own job.

One useful example is AI. If the AI directly edited unit positions or resources, it would be hard to tell whether the game rules still worked. By forcing AI through the command layer, the project keeps behavior consistent. Another useful example is rendering. The renderer should draw the current world, but it should not decide who is alive, what a unit is attacking, or whether a building can be placed.

For grading, this slide also shows that you understand tradeoffs. `RTSDemo.cpp` is large, but the reusable simulation code is separated into `src/`. The project chose practical separation instead of pretending every part could be perfectly abstracted.

</details>

## Custom vs External Tools

The runtime engine and RTS rules are custom C++. External software is used only where it makes sense for authoring or platform support.

| Area | Custom project work | External or standard support |
| --- | --- | --- |
| RTS simulation | `RtsWorld`, unit orders, buildings, economy, production, fog, combat events. | No external RTS/gameplay engine. |
| AI | Snapshot-driven `RtsAiSystem`, team profiles, command emission. | No external AI framework. |
| Pathfinding | Grid A* and line-of-sight checks in `RtsPathfinder`. | Standard C++ containers and math support. |
| Rendering | `DeferredRenderer`, render commands, G-buffer management, shader pipeline. | OpenGL and GLSL provide graphics API/shader execution. |
| Window/input/audio base | Demo-level integration and controls. | SDL-style platform layer already used by the engine. |
| Asset authoring | Runtime mesh/animation loading and sidecar discovery. | Blender is used to create source art and animation. |
| Asset conversion | `meshbin.py` and `animbin.py` exporter scripts. | Python and Blender scripting environment. |
| Build and verification | CMake targets and focused C++ tests. | CMake/compiler/test runner infrastructure. |
| Documentation site | Custom pages, diagrams, presentation deck script. | GitHub Pages/Jekyll hosting. |

The asset pipeline boundary is:

```text
Blender
  |
  +-- artist/authored mesh and animation data
  |
  v
custom exporter scripts
  |
  +-- .meshbin
  +-- .animbin
  |
  v
custom C++ runtime
  |
  +-- MeshLoader
  +-- AnimationClip
  +-- SkeletalRig
  +-- RenderCommand
```

The key point: Blender helps produce content, but it does not run the game. The game loop, RTS rules, AI decisions, command system, rendering path, and scenario behavior are part of this project.

<details class="speaker-notes" markdown="1">
<summary>Detailed speaking notes</summary>

This slide answers the prompt's external-tools requirement. Be explicit that there is no external RTS engine doing the hard work. The custom engine owns the runtime behavior. External tools are used for normal engineering reasons: Blender for asset authoring, OpenGL for graphics API access, CMake for building, and GitHub Pages/Jekyll for documentation hosting.

The distinction matters because using Blender does not mean the project uses Blender as a game engine. Blender is upstream of the runtime. It produces files that the custom C++ code loads and renders.

If asked what you personally built, emphasize the RTS layer, command flow, AI workflow, asset conversion path, scenario setup, and presentation documentation.

</details>

## Closing Summary

The final product proves that the engine can support a complete RTS-style workflow rather than only isolated assignment features.

Final takeaways:

- The project uses the assignment engine foundation as a base and extends it into a strategy-game system.
- `RtsWorld` is the central rules layer for units, buildings, economy, combat, fog, production, AI integration, and events.
- Player input, AI, and scripted scenarios all use command-style APIs, which keeps behavior consistent.
- Rendering is separated from simulation through snapshots and render commands.
- Deferred rendering demonstrates render-buffer management and a clearer multi-light workflow.
- Blender is used as an external authoring tool, but the runtime engine and RTS behavior are custom.
- Focused demos isolate the main observations: pathfinding, battle load, siege, AI autonomy, economy scaling, and tool pipeline.
- The documentation site now includes diagrams, a timed presentation outline, slide navigation, and detailed speaking notes.

One-sentence close:

> I built an RTS-focused engine extension that demonstrates how custom tools, simulation systems, AI, rendering, and documentation can fit into one coherent engine workflow.

```text
custom tools
  + simulation systems
  + command workflow
  + AI decisions
  + rendering pipeline
  + demo scenarios
  |
  v
coherent RTS engine presentation
```

<details class="speaker-notes" markdown="1">
<summary>Detailed speaking notes</summary>

Use this slide to end cleanly. Do not introduce a new feature here. Summarize what the project proves and connect back to the assignment language: project explanation, challenges, tools, engine workflow, diagrams, development approach, deviations, and external tools.

If there is extra time, mention future work: better UI polish, more data-driven scenario files, more advanced group pathfinding, improved AI strategy, or stronger profiling. Keep it framed as future work, not as missing core requirements.

</details>
