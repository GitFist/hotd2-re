![HOTD2RE main menu](docs/images/hotd2re-header.png)

# hotd2-re

A native C++20 rebuild of **The House of the Dead 2** for Windows, with online two-player play, modern input and display options, and additional game modes.

The native executable runs the rewritten game simulation and rendering code. The complete Windows release bundles the graphics, sounds, levels, and data it needs. **Players do not need a separate game installation, CD, or file-copying step.**

## Download and play

1. Open [Releases](https://github.com/GitFist/hotd2-re/releases/latest) and download **hotd2re-multiplayer-complete.zip** under Assets.
2. Extract the entire ZIP to a writable folder.
3. Open `hotd2re` and double-click **hotd2.exe**.

Keep the bundled `game` and `scenes` folders beside `hotd2.exe`. Both online players should use the same release. Use the complete ZIP, rather than GitHub's automatically generated **Source code** archives, to play.

The source repository contains code, tools, tests, and documentation. Bundled game data is delivered in the release download; it is not part of the MIT-licensed source tree.

This is an independent fan project, not an official Sega release. It is a work in progress; behavior and compatibility may change.

## Optional 2× graphics

Download **[hotd2re-graphics-2x.zip](https://github.com/GitFist/hotd2-re/releases/download/v2026.10.16-r/hotd2re-graphics-2x.zip)** from the release's Assets list. This separate add-on contains **2× textures and menu graphics only**, with no 4× textures or audio upgrade. Install the complete game first; the graphics ZIP cannot run by itself.

1. Close the game and extract the graphics ZIP.
2. Copy its contents into the `hotd2re` folder containing `hotd2.exe`.
3. Double-click **Play with 2x Graphics.cmd**.

The folders should look like this:

```text
hotd2re/
  hotd2.exe
  game/
  scenes/
  Play with 2x Graphics.cmd
  remaster-2x/
    tex/
    bmp/
```

Do not put the add-on inside `game/` or nest it inside another remaster folder. The launcher selects the upgrade for that launch only; it does not replace your settings or saves. Open `hotd2.exe` normally to use your usual graphics, which are original graphics with the complete download's default settings.

The images have twice the original width and height; level geometry stays the same. For more visible detail, optionally increase **Render Scale** in **Pause > Display** and use a larger window or fullscreen. Online players can choose different graphics packs while using the same game build.

To enable the pack for ordinary launches too, close the game and add or replace `assets=remaster-2x` in `hotd2re_settings.ini`. Remove that line to turn it off. A `HOTD2_ASSET_DIR` environment variable takes priority over this setting.

To uninstall, close the game and delete `remaster-2x` and the graphics launcher; also remove the INI line if you added it. Keep `game`, `scenes`, and your saves. The ZIP includes these instructions as `INSTALL-2X-GRAPHICS.txt` and a file checksum manifest; a ZIP SHA-256 checksum is attached to the release.

## Features

- Arcade, Original, Training, Boss, Scene Select, and Horde modes.
- Randomizer based on Original mode: varied ordinary enemy types and counts, weapons and items, voice lines, and scene character appearances while preserving their scripted roles.
- Two-player online rooms: public browser, private rooms, short join codes, host-selected modes, direct connections with relay fallback, and rejoining after a disconnect while the remaining player continues.
- Mouse, touchpad, touch, and Windows raw-input support for separate light guns/mice; clickable and shootable menus.
- Pause menu, volume controls, fullscreen/windowed display, aspect ratio, render scale, and optional CRT filtering.
- Chapter result screens, skippable after three seconds.
- An experimental hidden Prototype chapter, opened with F8, built using the original game's assets.

## Build on Windows

Install Git, Visual Studio 2022 with **Desktop development with C++** and a Windows SDK, and CMake 3.24 or newer. Python is optional for the core build and is used by tools and tests. Python icon generation uses Pillow; extracting the original icon also uses pefile.

```powershell
git clone --recurse-submodules https://github.com/GitFist/hotd2-re.git
cd hotd2-re
cmake -S . -B build -A Win32
cmake --build build --config Release --target hotd2 play --parallel
```

The supported target is **32-bit Windows**. CMake fetches a pinned SDL3 source release. Microsoft Detours is a pinned submodule used by the separate original-game hook tooling. If you cloned without submodules, run `git submodule update --init --recursive` before configuring.

For an offline build with an extracted SDL3 source release, add `-DFETCHCONTENT_SOURCE_DIR_SDL3=<path>` to the configure command. Cross-platform and 64-bit options are experimental.

## Run a development build

The instructions below are only for building from source. The complete release above already contains its required data. The native runtime reads the bundled `Hod2.exe` as a data source for lookup tables; it does not launch it.

For a development checkout, copy the original PC installation into `game/`, preserving its directory layout:

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

From the repository root:

```powershell
.\build\src\port\Release\hotd2.exe game
```

Keep the working directory at the repository root so `scenes/` can be found. The port reads the game files locally; the original executable is used as data. Game files, saves, personal settings, optional remaster assets, and build output are excluded from Git.

## Controls

| Action | Default control |
| --- | --- |
| Aim / fire | Mouse / left click or light-gun trigger |
| Reload | Right click or gun reload button |
| Confirm / start | Enter |
| Pause / resume | Esc |
| Menu navigation | Arrow keys, mouse, or gun aim |
| Back | Esc / Backspace |
| Quit | Main menu Exit or close the window |

If a touchpad does not register clicks, set `input=mouse` in the generated `hotd2re_settings.ini`.

## Online play

Open **Network** to create or browse a room. The host chooses the game mode after the other player joins. Pause > Return to Lobby takes both players back to their shared room. Both players need the same game build. Keep `hotd2re_rejoin.bin` private; it is the ticket used by Network > Rejoin Game.

The lobby/relay service is included under [`server/lobby`](server/lobby/README.md). Its address can be overridden with `lobby=host:port` in settings, `HOTD2_LOBBY`, or `--lobby host:port`. A configured lobby must be available for room discovery; relayed matches also depend on that service. Hosting your own service requires allowing its UDP port, default 27020. Service availability is not guaranteed by this source release.

## Tests and developer tools

Build individual test targets with CMake. Some tests need original game files or locally captured reference data; those fixtures are not bundled.

```powershell
python tests/lobby_service_test.py
cmake --build build --config Release --target pause_menu_test network_room_test randomizer_test prototype_test --parallel
.\build\Release\pause_menu_test.exe
.\build\Release\network_room_test.exe game
.\build\Release\randomizer_test.exe game
.\build\Release\prototype_test.exe game
```

`src/game` contains the shared simulation, `src/port` the native client, `src/hook` optional comparison tooling for the original executable, and `notes` technical research. Set `GHIDRA_HOME` to your local Ghidra installation before using the optional Ghidra scripts. Historical notes may reference local captures or earlier builds that are not included.

`tools/make_dist.ps1` creates a port-only package after building the client. It requires recipients to supply their own game files. Other local packaging/verification helpers may depend on private extraction manifests and are not necessary to build or run the port.

`python tools/make_graphics_dist.py --source remaster --out build/dist-graphics-2x` packages an existing local remaster asset set as the optional 2× add-on (Python 3.10+ and Pillow). It validates image dimensions, makes 2× copies of larger menu tiles, and verifies the ZIP against SHA-256 file hashes. The output directory must be new. Source assets and generated packages remain outside Git.

Release builds omit SDL's local source filenames from assertion messages. Both complete-game and graphics packaging run `tools/check_release_privacy.py` to reject personal home-directory paths, credential patterns, debug files, saves, and rejoin tickets. Run it directly on an executable or ZIP before publishing other packages. Use `--forbid` for additional private identifiers without committing those values. This safeguard does not replace reviewing package contents. Its regression checks are `python tests/release_privacy_test.py`.

## License and credits

The project's original code is available under the [MIT license](LICENSE). This license does not apply to Sega's game, assets, executable, trademarks, or other third-party material. Bundled game data is separate from the MIT-licensed source.

- [SDL](https://github.com/libsdl-org/SDL): zlib license; fetched by CMake.
- [Microsoft Detours](https://github.com/microsoft/Detours): MIT license; retained in its submodule.

Third-party components retain their own copyright and license notices.

