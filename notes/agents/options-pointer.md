# Clickable and shootable Options (2026-10-15, build O)

The native Options screen now supports pointer navigation under `WorldConfig::portFeatures`. The original path
remains unchanged: the 2,101-frame Options capture still matches its sprites, camera, tasks and RNG.

- Aim at a functional row to highlight it. A stationary pointer preserves keyboard navigation; moving or pressing
  a device button restores pointer selection. Blank-space shots cannot activate the previous selection.
- Click/shoot a value to advance it. The small original chapter-arrow sprites provide previous/next targets;
  right-click/reload over a value reverses it. Keyboard arrows still work, and Enter advances numeric settings.
- Shoot either player's sight preview to change that player's crosshair, regardless of which device fires.
- Sound-test arrows select a recording; shooting the row's name plays it. Leaving the row retains the original
  sound-stop behaviour.
- Display Settings, Default and Exit activate by click/shot. Exit and keyboard Escape/Back save settings and return
  to the main menu. The unimplemented legacy Sight Speed/calibration and controller screens stay disabled.
- Display Settings and pause menus use the same aligned text-row bands and preserve keyboard focus. Their value
  rows also have shootable arrows. Window Scale is disabled during fullscreen. Yes/No confirmation targets align
  with their visible labels.

Implementation: `world_modes_options.cpp` maps pointer actions onto existing handler input edges, consuming the
trigger's synthesized Start edge so a click changes one value once. `menu_pointer.h` shares visible-font hit bands
and pointer focus tracking; `MenuAdjustArrows` in `menu_text.cpp` reuses sprite 0xAFF. This does not alter gameplay,
asset files, the lobby service or the user's stored settings during installation.

Verification:

- `options_pointer_test game build/options-pointer-o/test-save.bin`: 33 hover positions, both devices/sights,
  value wrapping/clamps, one action per shot, sounds, display requests, disabled/blank hits, keyboard focus,
  Default, Exit, Escape and native save persistence.
- `pause_menu_test`: 107 checks, including display/pause letter bottoms, arrows, keyboard/pointer switching and
  disabled fullscreen window scale.
- `menu_pointer_test game`: all 66 main-menu positions still pass.
- `display_map_test`: 15/15 mappings.
- `frontend_test game build/scene_capture_modes_options.txt --script build/script_modes_options.txt`: 2,101 Options
  frames match; 188 draw matrices match across the capture.
- Headless renders of Options, sound tests, Display Settings and Pause were visually inspected in
  `build/options-pointer-o/`. Run scripts from its isolated `runtime` folder because changing volumes writes settings.

Build O is `2026 10 15 O` / `2026101502`. Both online players should update. Local and friend packages are recorded
in `build/dist-options-o-20261015/verification.json`; the small update preserves graphics packs, saves and settings.
