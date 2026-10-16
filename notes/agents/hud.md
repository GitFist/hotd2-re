# Gameplay 2D layer: HUD and in-game texts (stage-3 port agent notes)

Port: `src/game/hud.{h,cpp}` (library `game_hud`), test `tests/hud_test.cpp`. Symbols: `notes/agents/hud_symbols.csv`.
Every layout is a function that returns the `Spr_Draw` definitions (`game::SpriteDef`) of one original drawing
function in the original's submission order. The caller fills `game::HudState` / `HudPlayer`; each field is
documented in `hud.h` with the address it mirrors. Arithmetic follows the 24-bit x87 precision model (every
operation rounded to float), as in `shoot_camera.cpp`: with double intermediates the GAME OVER zoom-out drifted by
1e-9 at its end.

## How the layouts were found
Each captured `S` line carries the return address of `Spr_Draw` (and of `Spr_DrawSimple`). Grouping the play
captures by caller gives the functions below (`hud_test` classifies S lines the same way).

| caller(s) | function | what |
|---|---|---|
| 0x00416AE7 | Hud_UpdateCrosshair 0x004169C0 | crosshair |
| 0x00417A31 / 0x00417E02 / 0x0041805A / 0x004180EA | Hud_DrawAmmo 0x004177D0 | bullets (letterbox 2 / 1), RELOAD, hint |
| 0x00417566 / 0x00417676 / 0x0041751C | Hud_DrawLives 0x004174A0 | label + torches, cutscene skip prompt |
| 0x004068F1 / 0x004069E4 / 0x00406A6B | 0x004068A0 / 0x00406920 (via 0x00406CE0) | credit line ("PRESS START" 0x8FF, "CREDIT", digits) |
| 0x00406851 | Hud_DrawPressStart 0x004067D0 | PRESS START BUTTON (title / demo / tutorial) |
| 0x00460621 / 0x00460658 | Continue_ScreenTask 0x00460530 | CONTINUE + countdown digit |
| 0x0041CF48 | Spr_FlushRetained 0x0041CF30 | per-player CONTINUE (registered by 0x00416880 / 0x004168C0) |
| 0x00499F7D, ids 0x43A-0x43D | SprFx_Task 0x00499CE0 spawned by GameOver_Task 0x00460CD0 | GAME OVER fade + blood splats |
| 0x00499F7D, ids 0x1FB-0x22A | ChapterCard_Draw 0x00436AD0 (task 0x00434100) | chapter title card |
| 0x00461358 | RouteMap_Task 0x00461180 | route-map background (300 tiles) after game over |
| 0x0043699A | Hud_DrawText 0x00436850 from Subtitle_Task 0x00435AA0 | cutscene subtitles |
| 0x004960E4.. | Title_DrawLogo 0x004960A0 | title (already ported) |
| 0x00496A84 | task 0x00496890 | AB1 at (1,1) depth 100 between START and the game |
| 0x0049AD97 / ABE0 / AA3F / AC88+ACFB / ADE7 | tutorial tasks 0x0049AD70 / AB80 / AA00 / AC40 / ADC0 | how-to-play screen (mode 9) |

No score is drawn during play in this version (score only feeds the results / rank code).

## Layouts (all exact; see hud.h for every constant's address)
- **Crosshair** (0x004169C0): mode 6, lives > 0, `0x009C8E00`, and (input kind != 1 and gun record valid) or gun
  device mode 5/6 (`0x0041E260`). Definition player+0xE4: id `0x00579F58[p*4 + 0x009C9F60[p*0x7C]]`, position =
  `Input_GetGunPos` ints (device mode 5/6; 65535 when off screen) or (aimX + 320, 240 âˆ’ aimY); depth 1, flags 0xA
  (static fields from `Game_InitPlayers` 0x0040A920).
- **Ammo** (0x004177D0, only when `0x009C8E00`): letterbox 2: bullets 0xA74 at 24 + 24 i (P1) / 592 âˆ’ 24 i (P2),
  y 364; letterbox 1: everything scaled by (40 âˆ’ zoom)Â·0.0125 + 1, spacing (dx+24)Â·scale, zoom = `0x007C2120[p]`
  += 1 per frame of state 1, reset otherwise. Special weapons (`0x009CA08C == 1`): icon `0x004ECA20[type]`
  {id, dx, dy}; âˆž (0x65, 0x63) or two digits when ammo â‰¥ 7. RELOAD 0xA27 at (24 | 432, 260) s 1.5Ã—1 while
  empty, letterbox 1/2, `0x009C8E00`, and timer player+0x20 % 60 < 45; hint 0xA29 (gun) / 0xA28 at (16 | 424 | 384,
  325) from timer 120. Then timer++ (601 â†’ 120). Blink source: player+0x20 (zeroed by the trigger code).
- **Lives** (0x004174A0): letterbox 2: label 0x5B9 (28,412) / 0x5BA (584,412), torches
  `((0x009A32A0 + 4 i) / 3) % 7` + 0x6C2 / 0x6CA at 60 + 32 i / 548 âˆ’ 32 i; compact form (torch, 0x65, digit) in
  special-weapon mode with > 5 lives. Letterbox 4 (cutscene): skip prompt 0x5B8 (32 | 416, 422) s 1.5Ã—1 depth 0.98
  while `0x009A32A0 % 60 < 45` and not `0x009A5900 & 0x10`. Animation source: `0x009A32A0` (++ in Game_Update).
- **Player_Task order** (state 5): crosshair; unless mode 5/9 and lives != 0: ammo (if can shoot), lives
  (`HudPlayerTask`). State 9 task 0x004145F0 â†’ credit line if `0x009A5900 & 2` and mode 6; state 4 task
  0x00414200 (init, draws nothing) â†’ 0x00414280 (per-player CONTINUE when `0x009C90A4 != 4`, credit line).
- **Credit line** (0x00406CE0): hidden in mode 6 with `0x009CA08C` 2..3. Origin `0x00577620[p]` = (50 | 392, 412).
  Message `0x004C4B10[Credit_GetMessage]`: 0x5BB "insert coin" (+24), 0x5BC, 0x8FF "press start" (âˆ’8, scale 1.4);
  "CREDIT" 0x22E at (+32, +15) + digits 0x59.. at +114 (tens, if non-zero) / +126; "FREE PLAY" 0x332. Blink:
  `(0x005A4D44 >> 5) % 3 != 2`, or the 6-phase alternation with the "n COINS" line when coins per credit > 1.
- **PRESS START** (0x004067D0): `0x004C4B90[mode]` (modes 3, 5, 9, 10, 11), `0x009A5C44 > 3` (mode frame counter),
  `0x005A4D44 & 0x20 == 0`.
- **CONTINUE** (0x00460530, `0x009C90A4 == 4`): 0x22C (128, 200), digit 0x4F + (`0x009A2BB8` >> 12) at (482, 188);
  count 0x9FFF (reset when credits change), a button press (P1 `0x009C9028 & 4`, P2 `& 0x40000`) below 0x8000
  jumps to the end of the second, âˆ’0x2D per frame after drawing, â‰¤ 0 â†’ mode 7.
- **Per-player CONTINUE** (0x00416880 / 0x004168C0): 0x22C at (48 | 368, 170) and digit at +212, 160, scale
  0.6Ã—0.8, flags | 0x700, via the retained list (0x0041C760 head insertion, drawn by 0x0041CF30 at frame end: digit
  first).
- **Sprite effects** (SprFx 0x00499C60 / 0x00499CE0): draw, zoom step (modes 1-5: Â±k/duration), alpha step (modes
  0-8), removed when frame > duration. **GAME OVER** (0x00460CD0, tick t): t 0 0x43A (320,240) alpha 0, dur 130,
  alpha mode 2 (+0.02 for 50 frames, âˆ’0.05 in the last 20); t 120/125/130/135/140 splats 0x43B/0x43C/0x43D/0x43B/
  0x43D (dur 20, alpha mode 0); t 145 0x43A zoom mode 1 + alpha mode 1. New objects draw in their spawn frame.
- **Chapter card** (0x00436AD0, task 0x00434100): ids per chapter `0x009A1A08` (0x004343C0..), positions per
  chapter (0x004345CB..); phase 0 zoom-in (0..14), 1 shadows grow / fade, sub-lines fade in (15..45), 2 variant
  flicker + fade (46..85), 3 hidden (86..115), 4 grow-fade and squash-out (116..170). Counter starts 0 on the task's
  first frame; skipped by P1 START after 20 frames / P2 START.
- **Route map** (0x00461180): 4 Ã— 15 Ã— 5 tiles 128Ã—32, ids `0x005679FC[l]` + rowÂ·5 + col, y = scroll % 1920 +
  rowÂ·32 âˆ’ lÂ·480, depth 120; scroll `0x007DCCD8` (0 at start, +4 per frame while walking, 0x004615FC).
- **Subtitles** (Snd_PlayVoiceLine 0x00435B80 â†’ task 0x00435AA0): group `0x0058B6B8[line*3 + players]`, duration
  `0x00589DA8[g].+2`; per frame countdownâˆ’1 (0 or skip flags `0x009A2D74` / `0x009A2230` â†’ end), line+1 when
  countdown < record `+0x3E` (records 0x0058BC68, 64 bytes, text at +4; lines `0x005919A8[g*4+k]`), drawn with
  0x00436850 at y 384. Language 1: image `0x00589DA8[g]` at (+4, +8), flags 7.
- **Tutorial** (mode 9, script 0x00499740): label 0x5B9 (20,420); torches `((t + 4 i)/3) % 7` + 0x6C2 at 52 + 32 i,
  y 420 (t = the task's own counter); bullets 0xA74 at 16 + 24 i, y 372; reload A27 / hint at (24,260)/(16,325);
  captions 0xA69..0xA73 by kind and input kinds.

## Verification (`hud_test <game> <capture>`)
State per frame from the W lines (end-of-frame snapshot). Timing rules, all traced from the code: the player tasks
see the previous frame's mode, `0x009C8E00` and Letterbox_Task-made letterbox transitions (Letterbox_Task runs after
them; event commands 1 / 3 are seen at once), the previous frame's lives (enemy hits come later), this frame's ammo,
empty flag, aim and gun record; the reload timer is W âˆ’ 1 when the HUD advanced it; `0x005A4D44` = previous frame's
`0x009A5C40` for the player tasks and this frame's for PRESS START; `0x009A32A0` this frame's. Player_Task runs for
the state at the start of the frame; the player tasks are recreated at stage start and first run two frames after
the switch to mode 6; state 4's first frame is the init. Spawned sequences (CONTINUE, GAME OVER, chapter card,
subtitles, tutorial runs) start at their first captured frame and are then simulated.

Frames with exactly equal definition lists (match / differ). play2hud = the play2 script re-recorded with every
requested global in W; there the test uses the captured values instead of the defaults.

| layout | play1full | play2full | play2hud |
|---|---|---|---|
| crosshair | 628 / 0 | 1974 / 0 | 1974 / 0 |
| ammo / reload | 493 / 0 | 1769 / 0 | 1769 / 0 |
| lives / skip prompt | 1740 / 1 | 2543 / 1 | 2543 / 1 |
| credit line | 1626 / 0 | 2403 / 0 | 2403 / 0 |
| PRESS START | 354 / 0 | 662 / 0 | 662 / 0 |
| CONTINUE | 274 / 0 | 270 / 0 | 270 / 0 |
| per-player CONTINUE | 1 / 0 | 1 / 0 | 1 / 0 |
| GAME OVER fx | 167 / 0 | 138 / 0 | 138 / 0 |
| chapter card | 141 / 0 | 49 / 0 | 49 / 0 |
| route map | 134 / 0 | 69 / 0 | 69 / 0 |
| subtitles | 675 / 0 | 549 / 2 | 548 / 1 |
| title logo | 575 / 0 | 575 / 0 | 575 / 0 |
| mode-select AB1 | 198 / 0 | 198 / 0 | 198 / 0 |
| tutorial label / torches / bullets / caption | 165 / 165 / 165 / 45, 0 differ | 778 / 778 / 419 / 121, 0 differ | same as play2full |
| tutorial reload | – | 270 / 1 | 270 / 0 |

play2hud confirmed the defaults used for the older captures: P1 device mode 6 (P2 3), `Input_GetGunPos` = the gun
record's raw position, crosshair styles 0, `0x009CA08C` 0, credits 5 (slot 0) with coin state 2, free play 0,
coins per credit / continue 1, language `0x009C911E` = 2 (text subtitles), `0x009A5900` 3 → 2 at stage start,
`0x009A2BB8` and `0x007C2120` equal to the simulated values, route-map scroll 0, P1 START = `0x009C9028` bit 2
(the off-screen shot of the script). play2hud has no `9A32A0` key: the test then uses `0x009A5C40` (equal to it in
both older captures). `0x009A5C44` is the mode's frame counter (0 one frame after the mode switch, e.g. 4741), and
PRESS START uses the mode at the start of the frame (the title tasks start a frame after W shows mode 3).

Remaining differences:
- lives, frame 903 (all three): no skip prompt on the first Player_Task frame after the cutscene start, although
  letterbox is 4, `0x009A5900` = 2 (bit 0x10 clear), `0x009C8E00` = 0 and the counter gives 904 % 60 = 4. Both
  earlier hypotheses are refuted by play2hud; open (the task runs `Player_Start` 0x00414770 at 902, which calls
  Player_Task at once; something in the first regular frame suppresses the prompt).
- subtitles 1100 / 1101 / 3270 (play2): the cutscene skip. The flags `0x009A2D74` / `0x009A2230` are set by the
  skip watcher (task 0x00435F20, actor type in the table 0x00593358, spawned by events) when the player task sets
  `0x009A1A18` (START), and cleared the next frame. The subtitle task sees them in the same frame if the watcher
  runs before it in the task list (3270), else in the next (1101: the subtitle started first). The test kills on
  the W flag of the current frame, which is right for 3270 and one frame early for 1100. Exact modelling needs the
  watcher's spawn frame (event E lines) relative to the subtitle's start.
- tutorial reload 5918 (play2full only): the task ends when `0x007DD158` becomes 2; fixed when the key is captured
  (play2hud 270 / 0).

Without the captured keys (play1full / play2full) the test uses the defaults above; tutorial counts come from the
capture there (structure check only: `TutorialBullets` and the caption ids are not independently verified).

## Capture requests
- Keep `9A32A0:4` in the W key list (missing in play2hud).
- Event E lines together with W, to order the skip watcher against subtitle tasks.
- A capture with special weapons (`0x009CA08C == 1`), two players and a second CONTINUE, to exercise the untested
  branches (compact lives / ammo, weapon icons, coin lines, per-player CONTINUE in a 2P game).
## Not ported / open
- The tutorial script 0x00499740.. (when each tutorial task starts and the bullet / life / reload globals change)
  and the route walk itself (0x00461180 state, scroll changes, the walker sprite / text drawn as models).
- `Player_TriggerSpecial` weapon HUD ammo values (only the drawing is ported).
- The language-1 (Japanese) image paths are ported but unexercised.
- Menu / ranking / name-entry / stage-results screens are not in these captures.
