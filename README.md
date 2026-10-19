![HOTD2RE main menu](docs/images/hotd2re-header.png)

# HOTD2RE

A native C++20 rebuild of **The House of the Dead 2** for Windows, featuring online co-op, additional game modes, and modern controls and display options.

**[Download the game](https://github.com/GitFist/hotd2-re/releases/latest)** · **[Optional 2× graphics pack](https://github.com/GitFist/hotd2-re/releases/download/v2026.10.16-r/hotd2re-graphics-2x.zip)**

## Features

- **Game modes:** Arcade, Original, Training, Boss, Scene Select, and Horde.
- **Randomizer:** randomized enemies, enemy counts, weapons, items, voice lines, and character appearances.
- **Online co-op:** two-player rooms, public lobbies, private room codes, and reconnect support.
- **Flexible controls:** mouse, touchpad, touch, in-game light-gun setup for two players, and native Wii Remote setup with calibration and button mapping. Menus support clicking and shooting.
- **Display options:** fullscreen and windowed play, aspect ratio settings, render scaling, CRT filtering, and optional 2× textures.
- Chapter results and an experimental Prototype chapter, available with **F8**.

## Getting started

**Requirements:** Windows 10 or 11 and a Direct3D 11-compatible graphics device.

1. Download **hotd2re-multiplayer-complete.zip** from the [latest release](https://github.com/GitFist/hotd2-re/releases/latest).
2. Extract the ZIP to a writable folder.
3. Open the `hotd2re` folder and run **hotd2.exe**.

The download includes the required game data. Keep the `game` and `scenes` folders beside `hotd2.exe`. GitHub's **Source code** archives are for building the project.

## Online play

Both players need the same game version.

1. Open **Network** from the main menu.
2. Choose **Create Room** to host, or **Browse Lobby** to join a room.
3. For a private room, enter its code when prompted. **Peer to Peer** also supports joining an unlisted room by code.
4. Once both players are connected, the host selects a mode and starts the game.

Use **Pause → Return to Lobby** to choose another mode together. After a disconnect, select **Network → Rejoin Game** to return to an active match while the other player continues playing.

Connections use a direct route when available, with an automatic server relay fallback.

## Controls

| Action | Default control |
| --- | --- |
| Aim | Mouse or light gun |
| Fire / select | Left click or gun trigger |
| Reload | Right click or gun reload button |
| Confirm / start | Enter |
| Skip ending credits | Left click, gun trigger, or Enter |
| Pause / resume | Esc |
| Navigate menus | Arrow keys, mouse, or gun aim |
| Back | Esc or Backspace |
| Quit | Main menu **Exit** or close the window |

Open **Options > Light Guns** or **Pause > Light Guns** to switch between mouse/touchpad input and light guns, assign Player 1 and Player 2, remap buttons, calibrate aim, and test both guns. See the [light-gun setup guide](docs/LIGHT-GUNS.md).

## Wii Remotes

Open **Options > Wii Remotes** or **Pause > Wii Remotes** to connect and configure up to two remotes. Use a powered sensor bar with a compatible Bluetooth adapter, or a DolphinBar in Mode 4. See the [setup and calibration guide](docs/WII-REMOTES.md). Native support is new and has not yet been tested with physical Wii Remotes.

## Optional 2× graphics

The [2× graphics pack](https://github.com/GitFist/hotd2-re/releases/download/v2026.10.16-r/hotd2re-graphics-2x.zip) upgrades textures and menu graphics. Install it after the base game.

1. Close the game and extract **hotd2re-graphics-2x.zip**.
2. Copy its contents into the `hotd2re` folder, alongside `hotd2.exe`.
3. Run **Play with 2x Graphics.cmd**.

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

To use the pack when launching `hotd2.exe` directly, add `assets=remaster-2x` to `hotd2re_settings.ini`. Remove that setting to return to the default graphics.

For sharper rendering, increase **Render Scale** under **Pause → Display**. Each online player can choose their own graphics settings. Additional setup and removal instructions are included in **INSTALL-2X-GRAPHICS.txt**.

## Development

HOTD2RE uses C++20, SDL3, and Direct3D 11. The supported build target is 32-bit Windows.

- [Build and test guide](docs/DEVELOPMENT.md)
- [Lobby server setup](server/lobby/README.md)
- [Report a bug](https://github.com/GitFist/hotd2-re/issues)

## License and credits

HOTD2RE is an independent fan project. **The House of the Dead 2** and its original game assets belong to Sega.

Original project code is available under the [MIT license](LICENSE). Game assets and other third-party material retain their respective licenses and copyright notices.

Built with [SDL](https://github.com/libsdl-org/SDL) (zlib license). The comparison tools use [Microsoft Detours](https://github.com/microsoft/Detours) (MIT license).

