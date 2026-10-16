# Development

## Requirements

- Git
- Visual Studio 2022 with **Desktop development with C++** and a Windows SDK
- CMake 3.24 or newer
- Python for test scripts and asset tools; icon generation uses Pillow and pefile

The supported target is **32-bit Windows**. Cross-platform and 64-bit builds are experimental.

## Build

```powershell
git clone --recurse-submodules https://github.com/GitFist/hotd2-re.git
cd hotd2-re
cmake -S . -B build -A Win32
cmake --build build --config Release --target hotd2 play --parallel
```

The game executable is written to `build/src/port/Release/hotd2.exe`. The `play` target provides the command-line client used by development tools.

CMake downloads a pinned SDL3 release. Microsoft Detours is included as a submodule for the original-game comparison tools. For an existing checkout with missing submodules, run:

```powershell
git submodule update --init --recursive
```

For an offline SDL3 build, add `-DFETCHCONTENT_SOURCE_DIR_SDL3=<path>` to the configure command, pointing to an extracted copy of the pinned SDL3 source.

## Run

Copy the `game` folder from the [Windows release](https://github.com/GitFist/hotd2-re/releases/latest) into the repository root. Its layout should include:

```text
game/
  Hod2.exe
  cam/
  coli/
  evt/
  mot/
  pol/
  Sound/
  tex/
```

The native runtime loads game assets and lookup tables from this directory. `Hod2.exe` supplies table data and is not executed.

Run from the repository root so the `scenes` directory is available:

```powershell
.\build\src\port\Release\hotd2.exe game
```

## Tests

Run the lobby service tests with Python:

```powershell
python tests/lobby_service_test.py
```

Build and run the menu and mode tests:

```powershell
cmake --build build --config Release --target pause_menu_test network_room_test randomizer_test prototype_test --parallel
.\build\Release\pause_menu_test.exe
.\build\Release\network_room_test.exe game
.\build\Release\randomizer_test.exe game
.\build\Release\prototype_test.exe game
```

Tests that load levels require the game data described above. Some comparison tests also require separately captured reference data.

## Source layout

| Directory | Contents |
| --- | --- |
| `src/game` | Shared game simulation |
| `src/port` | Native client, rendering, input, audio, and networking |
| `src/hook` | Original-game comparison tools |
| `scenes` | Scene definitions |
| `server/lobby` | Room discovery and gameplay relay |
| `tests` | Automated checks |
| `tools` | Asset, packaging, and development utilities |
| `notes` | Technical research |

Optional Ghidra scripts use the `GHIDRA_HOME` environment variable to locate a local Ghidra installation.

## Hosting online play

See the [lobby server guide](../server/lobby/README.md) for Docker deployment and service tests.

To connect a client to a different server, use `lobby=host:port` in `hotd2re_settings.ini`, set the `HOTD2_LOBBY` environment variable, or pass `--lobby host:port`. The default service port is UDP 27020.
