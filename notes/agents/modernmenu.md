# Modern main menu (port only: play.exe / hotd2.exe)

Agent "modernmenu". A modernized mode select (game mode 4) behind `WorldConfig::portFeatures` + `portModernMenu`;
the exactness tests never see it (feature off there, frontend_test identical before / after). No original code is
involved: nothing to add to symbols.csv. Originally a setting (`menu=modern|classic` in hotd2re_settings.ini and a
"Main Menu" row of the display settings page); both were removed on 2026-10-06 at the user's request - play.exe always
turns the modern menu on (`DisplaySettings::modernMenu`, `kMenu` and the `menu=` key below no longer exist).

## Current layout and cursor correction (2026-10-15, build N)

The current list has eleven entries: the original seven, Scene Select, Horde Mode, Randomizer, Exit. Labels start at
x 24 / y 148, advance 27 pixels per row, and use scale 1.25. The font textures are **16 x 32**, not 16 x 24:
capital-letter ink sits around texture rows 12..32. The old hit bands started two pixels above the texture origin,
so the lower half of a displayed label selected the next option and the bottom of Exit could not be selected.
`ModernMenuRowAt` now centres each non-overlapping band on the visible capital-letter body (texture row 22), moving
the bands down 16 game pixels without moving the labels or changing the graphics pack.

Stationary aim leaves keyboard selection alone; moving within the same row restores hover selection, and a click
explicitly selects the pointed-at label. Clicking outside the labels cannot activate a stale selection. Enter still
confirms the keyboard selection. The original menu path remains unchanged.

Regression: `menu_pointer_test game` checks 66 points derived from the rendered label positions, corresponding
highlights, Exit's bottom, clicks outside the list, mixed mouse/keyboard confirmation and original-menu behaviour.
`display_map_test` checks 15 window/image mappings. The `menus_net` frontend capture still matches all 188 draw
matrices. Before/after screenshots and build logs are in `build/menu-cursor-n/`.

The sections below describe the original nine-entry implementation and its historical settings.

Background (2026-10-06): with the asset pack the tiles come from `tools/menu_background.py` (a stage-1 screenshot, frame
2424 of build\script_auto_st1.txt rendered with `--no-hud` at 2560x1920: the first two zombies in the street; the title
screen's logo art and the SEGA marks composited where the original picture has them; a left gradient for the list). The
in-painting of section 3 is then skipped (an override disables it) and only applies without the pack.

Code: `src/game/world_front.cpp` TaskMenu (the list, the pointer selection; constants `kModern*`), `src/game/menu_text.*`
(`MenuTextScaled` / `MenuTextAdvance`: the options-screen glyphs at a raw position and a uniform scale; `MenuText` itself
is unchanged in effect, refactored over the same glyph loop), `src/game/world.h` (`WorldConfig::portModernMenu`,
`World::SetPortModernMenu`), `src/port/render/front_screens.*` (the background cleanup), `src/port/play/display_menu.*`
(`DisplaySettings::modernMenu`, row `kMenu` "Main Menu: Modern / Classic" between Render Scale and Back, Back moved to
cell row 12), `src/port/play/pause_menu.*` (the `menu=` line), `src/port/tools/play.cpp` (passes the setting into
WorldConfig and FrontScreens for interactive runs and with `HOTD2_PLAY_MENU=1`; headless + HOTD2_PLAY_MENU reads only
the `menu=` key of the ini in the working directory), `tests/pause_menu_test.cpp`.

## 1. The list

Nine entries in the cursor's order (Arcade mode, Original mode, Training mode, Boss mode, Rankings, Options, Network,
Scene Select, Exit), left-aligned at x 24 under the HOUSE OF THE DEAD 2 logo (which ends at y ~135), rows 33 px apart
from y 148 (Exit at 412; the 24 x 1.4 = 34 px cell ends at 446), options-screen font (`MenuText` glyphs 0x0056AED0,
texture 0x15E) at scale 1.4 (cap height ~25 px = the original labels'; advance 22.4 px, so "Original mode" ends at
x 315). Unselected grey (flags 0x40, tint 0.6), selected white; flags |= 0x700 (the bitmap screens are TL quads at z 0).
The original's highlight label tiles (slots 6..29) are never shown; the logo strip sprite 0xAB1, "Presented by SEGA"
(x >= 480, y >= 390) and the copyright line (x >= 410, y >= 440) stay as they are and are not overlapped. Up / Down
(the original's masks 0x005978C8 / D0, cursor sound 0x3E16A9), the confirm masks 0x005978D8, credits, the confirm paths,
the Scene Select page and Exit are unchanged.

## 2. Pointer selection

The start player's device first, then the other: a `WorldInput` of kind 1 (gun / mouse; scripted input counts) whose aim
(x / y in 640x480 pixels) is over a row - the row's 33 px band from 2 px above its cell, over the text width plus one
cell on each side (`ModernMenuRowAt`) - moves the cursor there with the cursor sound. Only on a *change* of the row under
the aim (`MenuTask::pointerRow`; the first frame just records it): a resting aim never overrides the keyboard, and the
scripted captures' centre aim (320, 240) is right of the list anyway. The trigger confirms through the original's mask.

## 3. Background cleanup (`FrontScreens::CleanMenuBackground`)

The eight grey labels are painted into hod2title00..11. With the modern menu on, the first use of a tile in-paints them
out once, in memory (nothing on disk; an asset-pack override of a tile switches it off):
- compose the 640x512 background from the six tiles (tiles 10 / 11 are the left 128 px of 256-wide bitmaps);
- glyph body = pixels of the bright label tiles (arcade00.. at x 0xE0 + 0x40 k, y {0x20, 0x50, 0x90, 0xB0, 0xF0, 0x120,
  0x150, 0x180}) that differ from the background by > 28 levels (any channel; exit_00 / exit_02 are identical to the
  background: no body). The near-black 3 px halo around the glyphs is the same in both versions, so it is taken from the
  background: within 3 px of the body and luminance < 40. Union dilated by 2 px (the halo's soft edge). ~36k pixels.
- fill: Jacobi averaging of the masked pixels from their known 8-neighbours (a filled pixel counts as known for the next
  iteration) until the largest change is < 0.25 levels (~135 iterations, within the mask's bounding box), then 0.5 x the
  high-frequency detail (bg - 3x3 mean) mirrored across the nearest boundary in each of the four directions (weights
  1 / d^2) on top, so the fill is not entirely flat. ~0.3 s once.
- Result (build\shots_menu_modern\modern_p1_890.png): no readable ghost of any label. The fill is smooth where the labels
  crossed strong edges: soft smudges remain where "Original mode" / "Training mode" crossed the big zombie's raised arm
  against the clouds and where the labels crossed the dark building edges; the clouds themselves and the street are
  clean, the torso shows faint soft patches at the old "Rankings" / "Options" positions (the list's own text covers
  part of that area). Tried and rejected: pure threshold masks (the halo stayed as dark outlines = readable ghosts), a
  per-pixel patch search (blocky), directional line interpolation (streaks); smooth + weak mirrored detail looked best.

## 4. Checks

- frontend_test (worktree binaries, main's captures) identical line for line before / after the change: play1full
  (+ script_p1_start), menus_net, modes_train3, modes_boss1, menus2_weapon12, modes_options (MenuText's refactor:
  the options screen's sprites exact); modes_train2 and modes_rank after the change: every compared line exact.
- `python tools/render_compare.py` 9/9; `pause_menu_test` 62/62 (new: the Main Menu row, the menu= round trip);
  `display_map_test` 15/15.
- Headless (`HOTD2_PLAY_MENU=1`, 1280x960), `build\shots_menu_modern\`: `modern_p1_890.png` (script_p1_start, cursor
  on Arcade; frame 900 is the confirm frame: the screens are freed there, as in the original), `modern_ss_900.png`
  (script_scene_select_st3: cursor on Scene Select), `modern_ss_1080.png` (the page on Chapter 3), `modern_ss_1700.png`
  (the stage-3 boat ride: the page still works), `classic_ss_900.png` (menu=classic: the original labels + the small
  Scene Select text), `pointer_800.png` / `pointer_850.png` / `pointer_950.png` (`build\script_menu_pointer.txt`: the
  aim moves onto Boss mode at 800 and Options at 850, the cursor follows; trigger at 900 opens the Options screen).
- Not verified at runtime: the interactive pointer path with a real gun / mouse (the scripted path uses the same
  WorldInput), changing the Main Menu row while the mode select is on screen (it is only reachable from the pause menu
  and the Options screen; the mode select re-reads the flag every frame), two guns aiming at different rows (the start
  player's device wins).
