# Prototype — hidden HOTD4 chapter-one adaptation

Build Q (2026-10-15). User requested a hidden hotkey-only mode and explicitly chose HOTD2 assets.
Press F8 on the title or main mode-selection screen. No visible menu entry is added.
The hotkey is ignored in connected network rooms, original-exactness configurations, and during a running game.

## Play

- Hold left mouse / the gun trigger for automatic fire (30 rounds, one shot every five simulation frames).
- Right mouse / offscreen reload / Backspace reloads in 28 frames. G throws a grenade (three initially).
- Shoot the Upstairs or Downstairs half of the route screen, or use Left / Right. Ten-second timeout chooses Upstairs.
- Shoot marked supply panels for a grenade and life. Five initial lives; enemy proximity attacks cost lives.
- Hold fire or use a grenade to escape a grab. The boss exposes its head in timed windows; sufficient damage cancels its attack.
- ESC uses the existing pause menu, including Return to Main Menu. Enter confirms.
- Victory and defeat show results; a fresh click/shot or Enter dismisses after three seconds.

The mode is a local, early playable adaptation. Its authored rooms represent the classified archive, corridors,
security checkpoint, stairwell, upstairs offices/balcony or downstairs maintenance/service passage, elevator hall,
floor collapse and Justice arena. It has 82 regular enemies upstairs or 84 downstairs, then the boss.
Industrial materials, skeletons, walk animation, muzzle flash, explosions, music, sounds and typeface come from HOTD2.
The four-armed boss is assembled from an enlarged HOTD2 brute. It uses an exposed-head attack window in place of
HOTD4's tongue model. Layout, scene choreography and encounters are simplified, not a shot-for-shot or geometric
reproduction of HOTD4. Floor changes use authored fades; straight corridors use rail movement.

Reference for the chapter's broad sequence and route/boss beats:
[Shadi_Potter's firsthand HOTD4 walkthrough](https://gamefaqs.gamespot.com/arcade/929112-the-house-of-the-dead-4/faqs/42249).
No HOTD4 game data was imported. Ordinary campaigns, inventory and save records are not used for this chapter.

## Implementation

`world_prototype.cpp` owns its state and explicit input, camera, encounters and results lifecycle. The front-end
dispatch enters it only through the reserved F8 bit (0x400); G uses 0x800. Both fit the native input envelope.
The original event VM is not run in Prototype. Native skeleton animation and rendering are reused.
Leaving the mode destroys its state and restores character-light handling. Existing stage starts reset the state.

`scene/prototype_model.h` resolves reserved model IDs 0x7000..0x7030 into authored building blocks using
descriptor-index references to HOTD2 `st6_01.bin` textures. They never access the original model-ID table.
No new data package is required. `World::InspectPrototype` exposes read-only state for full-route replay tests.
`HOTD2_PLAY_KEYS` now also supports isolated local headless captures, using the same keyboard mapping as live play.

## Verification

`prototype_test game build/prototype-q/route` covers both complete routes, boss defeat, pause/resume, three-second
results, return/restart, grenade edge handling and exhaustion, losing all lives, exactness-mode isolation and the
connected-room hotkey guard. The generated replay scripts feed the real D3D11 client for visual checks.
Headless renders checked the archive, fork, upper office, lower maintenance, transitions, boss and results.
Existing selection, options, network-room/replay and pause-menu checks are also run for the release.
Artifacts and logs are under ignored `build/prototype-q/`. This does not certify Prototype online play.

Release installed at `build/src/port/Release/hotd2.exe`, SHA-256
`fd66a17e2daad353980254b478fab9a1d0279cae7d1567c07e96cefcaeff1249`.
Prior P executable was renamed to `hotd2-p-before-prototype-20261015.exe`; no running game was terminated.
Settings hash was unchanged during installation. `build/dist-prototype-q-20261015/` contains a 2.1 MB
update ZIP and a complete 443.7 MB package. ZIP CRCs and the complete package's 2,047 listed hashes were checked;
the installed and packaged executable bytes agree. The update ZIP includes `README-PROTOTYPE.txt`.
