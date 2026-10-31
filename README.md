![HOTD2RE Night Shift main menu with example online rooms](docs/images/hotd2re-night-shift-main-menu.png)

# HOTD2RE

A native Windows port of **The House of the Dead 2** and **The Typing of the Dead**, with online co-op and modern controls.

**[Download 1.7 for Windows x64](https://github.com/GitFist/hotd2-re/releases/download/v2026.10.31-a/HOTD2RE-1.7-Windows-x64.zip)** · [Release notes](https://github.com/GitFist/hotd2-re/releases/tag/v2026.10.31-a) · **[Optional 2× graphics pack](https://github.com/GitFist/hotd2-re/releases/download/v2026.10.16-r/hotd2re-graphics-2x.zip)**

## Features

- **The Typing of the Dead:** full campaign and all 13 Drill courses.
- **HOTD2 modes:** Arcade, Original, Boss Mode, Horde and Randomizer.
- **Online play:** two-player co-op, shared lobbies and leaderboards.
- **Controls:** keyboard, mouse, touch, light guns and Wii Remotes.
- **Graphics and extras:** modern display options, blood colors, music library and model viewer.

## Screenshots

| Play menu | Settings |
| --- | --- |
| ![Night Shift Play menu](docs/images/hotd2re-play.png) | ![Night Shift settings](docs/images/hotd2re-settings.png) |

**Online Arcade leaderboard**

![Online Arcade leaderboard](docs/images/hotd2re-arcade-leaderboard.png)

The main-menu preview uses example online rooms.

## Getting started

**Requirements:** Windows 10 or 11 (64-bit) and a Direct3D 11-compatible graphics device. Optional Direct3D 12/vendor features require compatible hardware.

1. Download **[HOTD2RE-1.7-Windows-x64.zip](https://github.com/GitFist/hotd2-re/releases/download/v2026.10.31-a/HOTD2RE-1.7-Windows-x64.zip)**.
2. Extract the ZIP to a writable folder.
3. Open the `hotd2re` folder and run **hotd2.exe**.

The complete download includes both games. Keep the `game` and `scenes` folders beside `hotd2.exe`.

The in-game updater keeps your saves and downloads any missing Typing data.

## Online play

Open **Home > Online**, then choose **Create Room**, **Browse Lobby** or **Peer to Peer** to play with someone on the same game version. The host can choose gun modes or **Typing Original** in the shared lobby. Drill is local play.

Choose a username on first launch; eligible scores submit automatically to the online leaderboards, including the Typing table, with offline scores queued until you reconnect. Typing Drill keeps its own course records.

## Controls

Typing gameplay uses the keyboard to enter the displayed words. The controls below cover gunplay and shared menus. At the title or main menu, **Left, Left, Right, Right, Right, Left, Right, Enter** toggles each active player's gameplay score display.

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

## Graphics and display

- Increase **Render Scale** for a sharper picture, or lower it to improve performance; select **Fixed Scale** under **Resolution Mode** if the setting is unavailable.
- While adjusting graphics in a paused game, press **H** to hide the menu and preview the scene, then **H** again to restore it.
- Use **Reset All Graphics** to restore graphics defaults while keeping your controls and gameplay settings.

## Wii Remotes

Open **Options > Game Options > Wii Remotes** or **Pause > Wii Remotes** to connect and configure up to two remotes. Classic menus retain **Options > Wii Remotes**. Use a powered sensor bar with a compatible Bluetooth adapter, or a DolphinBar in Mode 4. See the [setup and calibration guide](docs/WII-REMOTES.md). Native support has automated tests; physical Wii Remote pairing/aiming remains unverified.

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

For sharper rendering, increase **Render Scale** under **Graphics Options → Resolution**. This draws more pixels at runtime and needs no pack or large download; the 2× texture pack replaces artwork separately. Each online player can choose their own graphics settings. Additional setup and removal instructions are included in **INSTALL-2X-GRAPHICS.txt**.

## Development

HOTD2RE uses C++20 and SDL3, with Direct3D 11, SDL GPU and optional native Direct3D 12 rendering. Windows x86 and x64 builds are supported; advanced vendor reconstruction/frame generation requires the x64 build and compatible hardware.

- [Build and test guide](docs/DEVELOPMENT.md)
- [Lobby server setup](server/lobby/README.md)
- [Report a bug](https://github.com/GitFist/hotd2-re/issues)

## License and credits

HOTD2RE is an independent fan project. **The House of the Dead 2** and its original game assets belong to Sega.

Original project code is available under the [MIT license](LICENSE). Game assets and other third-party material retain their respective licenses and copyright notices.

Built with [SDL](https://github.com/libsdl-org/SDL) (zlib license). The comparison tools use [Microsoft Detours](https://github.com/microsoft/Detours) (MIT license).

