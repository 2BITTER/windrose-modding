# BetterBuildingTools (BBT)

Precision building tools for [Windrose](https://store.steampowered.com/app/2156950/Windrose/) — a UE4SS mod that adds fine rotation, copy object, copy angle, undo, placement freedom, a build status HUD, and a drill-down settings menu. Every feature is toggleable and persists between sessions.

> **Beta** — actively developed. If something breaks, [report it on Nexus](https://www.nexusmods.com/windrose/mods/11).

## Features

| Feature | Description |
|---------|-------------|
| **Fine Rotation** | 1, 5, and 10 degree steps added to the game's rotation cycle |
| **Copy Object** | Look at a placed block and grab its type instantly (Shift+C) |
| **Copy Angle** | Copy a placed block's exact rotation to your preview (Alt+C) |
| **Undo** | Remove your last placed block (Shift+Z), stack of 10-50 |
| **Build Status HUD** | Live overlay showing rotation, snap, target angle, undo count |
| **Settings Menu** | F2 drill-down menu with sections, guide, and UI scaling |
| **Placement Freedom** | Furnace/Kiln Under Roof, No Stability Req |
| **No Build Cost** | Cheat toggle — build without resources |
| **UI Scaling** | PgUp/PgDn to scale the settings menu (70-150%) |

## Requirements

- [UE4SS v3.x](https://github.com/UE4SS-RE/RE-UE4SS) — required for DLL loading and Lua scripting
- Windrose (Steam)
- Solo play only (multiplayer untested)

## Installation

### From Release
1. Install UE4SS
2. Extract the `BetterBuildingTools` folder into `Windrose/R5/Binaries/Win64/ue4ss/Mods/`
3. Launch the game

### From Source
The C++ DLL builds against the [UE4SS SDK](https://docs.ue4ss.com/dev/guides/creating-a-c++-mod.html).

```
# Build (requires CMake + MSVC)
cmake --build <build_dir> --config Game__Shipping__Win64 --target BuildingUndo

# Deploy
copy build/BuildingUndo/Game__Shipping__Win64/BuildingUndo.dll → Mods/BetterBuildingTools/dlls/main.dll
```

The Lua script (`Scripts/main.lua`) does not need compilation — UE4SS loads it directly.

## File Structure

```
BetterBuildingTools/
├── src/
│   └── dllmain.cpp          # C++ DLL — hooks, config, undo, placement freedom
├── Scripts/
│   └── main.lua             # Lua — BStat HUD, settings menu, guide, keybinds
├── config.example.txt        # Default config (copy to config.txt)
├── enabled.txt               # Must contain "1" for UE4SS to load the mod
├── build_release.ps1         # Release packaging script
└── docs/
    └── ...                   # Modding resources (coming soon)
```

## How It Works

BBT is split into two layers:

- **C++ DLL** (`dllmain.cpp`) — hooks game functions for rotation, copy, undo, and placement freedom. Reads/writes game memory through UE4SS reflection. Runs a game-thread dispatcher for tick-based operations. Exposes config and build status to Lua through bridge functions.

- **Lua script** (`main.lua`) — builds the entire UI using UE4SS's UMG widget API (no Blueprint, no editor). Handles the BStat HUD, drill-down settings menu, guide pages, keybind registration, and UI scaling. Communicates with the DLL through exposed global functions (`BBT_GetConfig`, `BBT_SetConfig`, `BBT_GetBuildStatus`, etc).

## Configuration

All settings are in `config.txt` (auto-generated on first run). The F2 settings menu modifies this file in real time. Keybind changes require a game restart.

## License

Source-available for learning and reference. See [LICENSE](LICENSE) for details.
