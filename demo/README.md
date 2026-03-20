# Doom Demo

This folder contains an initial Doom-style gameplay demo built with the existing engine systems

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
