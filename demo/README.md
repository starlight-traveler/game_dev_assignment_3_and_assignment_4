# Gameplay Demos

This folder contains small gameplay demos built with the existing engine systems

## Doom Demo

Current features:
- first-person camera with mouse look
- boxed arena scene
- one enemy that chases and deals contact damage
- hitscan shooting with cooldown
- player and enemy health
- audio hooks for optional gunshot and hit wav files

Controls:
- `W A S D`: move
- `Mouse`: look
- `Left Click` or `Space`: shoot
- `Shift`: move faster
- `Esc`: quit

Build and run from project root:

```bash
cmake -S . -B build
cmake --build build -j
./build/doom_demo
```

Optional audio files searched automatically:
- `audio/gunshot.wav` or `assets/gunshot.wav` or `blender/gunshot.wav`
- `audio/hit.wav` or `assets/hit.wav` or `blender/hit.wav`

## RTS Demo

Current features:
- fixed isometric orthographic camera
- terrain tiles with different movement/build rules
- placed buildings with footprint occupancy and blocking behavior
- reusable A* pathfinding over terrain cost and building blockers
- shader-driven solid-color units and building rendering
- drag-box or click selection for the blue squad
- right-click formation move commands with visible path markers
- unit combat with health bars, target acquisition, cooldown-based attacks, and death cleanup
- build placement mode for farms and depots
- hovered-building highlight plus demolition control
- red enemy units patrolling until they acquire targets
- terrain/building-aware movement plus scene-graph-backed unit selection

Controls:
- `Left Click`: select one unit
- `Left Drag`: box-select units
- `Right Click`: attack hovered enemy, move selected units, or place the active building
- `B`: farm placement mode
- `N`: depot placement mode
- `R`: cancel build mode
- `X`: demolish the hovered building
- `W A S D` or Arrow Keys: pan camera
- `Mouse Wheel`: zoom
- `Esc`: quit

Build and run from project root:

```bash
cmake -S . -B build
cmake --build build -j
./build/rts_demo
```
