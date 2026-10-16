# Scene Select (port only: play.exe / hotd2.exe)

Agent "sceneselect". Code: `src/game/world_scene_select.cpp` (+ small gated edits in `world_front.cpp` TaskMenu /
GameStartFrame / MenuFrame init, `world_modes_boss.cpp` SpawnModes 0x6C, `world_internal.h` Front::sceneSelect /
sceneStart, `world_modes.h`, `world.h` comments, CMakeLists). Everything is behind `WorldConfig::portFeatures`
(play.cpp sets it for interactive runs and with `HOTD2_PLAY_MENU=1`); the exactness tests never see it. No original
code is involved: nothing to add to symbols.csv.

## 1. The entry (mode select, game mode 4)

- Ninth entry "Scene Select" = cursor 7, Exit moves to 8 (`WorldHud::menuCursor` comment). Up / down wrap over 9
  entries; the bitmap label groups (3 tiles of 64 px) stay those of the original's eight: entry 8 shows Exit's tiles,
  entry 7 none.
- The label has no bitmap (the background `hod2title00..11` carries the original's eight labels, baked in): it is
  text in the options screen's font (`game/menu_text.h` MenuText, glyphs 0x0056AED0, texture 0x15E), cells (13.5, 18)
  = 216..408 x 432 px, below Exit (tiles end at 448) and left of the copyright line; grey (flags 0x40, 0.6) when not
  selected, white when selected. Its sprites get flags 0x700 (depth test always): the port draws the bitmap screens as
  TL quads at z 0 (0x0049EAF0), nearer than any sprite depth, so a depth-tested sprite would be hidden behind them.
- Confirm (`SceneSelectMenuConfirm`): as Boss mode's entry (game type 3: st1evtbl scene 0x10 = the select's room,
  0x009A2BBC = 0, cheat flag 0, sound 0x121A9) but with the Arcade entry's credits (0x00496B70(0): setting + 1 or free
  play), so the game started from the page has them; `Front::sceneSelect` = true. The player's task sees the same
  trigger / start and joins (Player_TryJoin -> Game_SetState(6)) exactly as for the other entries.

## 2. The page (`TaskSceneSelect`, replaces actor 0x6C's task 0x00425010 while Front::sceneSelect)

Same init as the boss select (textures 0x14E / 0x16F / 0x173, pol 0xBF, model 0x1A4C, motion file 0x30, BGM
0x10000024, agent skeleton 0x3A motion 0x31A -> 0x31C, attackers = 1 so the scene script waits, 0x009A5900 |= 0x18),
same draw structure as 0x00425780: background tiles 0xADB.., carousel (frame 0xAF1, entries 0xD2 apart sliding 21 px
x 10 frames, brightness 1 / 0.3 +-0.07; a ring of six: window -420..840 instead of -420..1470), pulsing star 0xAEF,
arrows 0xAFF, the bubble 0xB28 at (144, 365), `SelectAgentDraw`, `ModesDimCount`. Entries: the six chapters, pictured
by the first boss of each stage (portraits 0x0055CBE4[0, 1, 3, 5, 7, 8]), none locked.
Text the original has only as pictures is drawn with the subtitle / caption glyph font (0x0055E054, file 0x17B, loaded
by every Stage_Start; `GlyphText`, 16 px cells x scale): title "SCENE SELECT" (scale 1.5 at y 36, where 0xB30 "BOSS
MODE" sits), the entry's "CHAPTER n" (yellow tint, (321, 90), where the boss name 0x0055CC0C[k] sits), the chapter
card's title ("A PRELUDE" .. "ORIGINAL SIN", scale 0.8 at y 270; the cards 0x007DCBA0.. are per-stage textures
0x18C.., not loaded here), and in the bubble, in place of the caption picture 0xB1B: "THIS IS THE SCENE SELECT. /
CHOOSE THE CHAPTER TO / START THE GAME FROM." (3 lines at x 200, y 322 / 350 / 378).
Input (the boss select's mask tables 0x00589140..): left / right AND up / down move (60-frame auto repeat), trigger /
start confirm (after 4 settling frames), reload / Backspace = back (sound 0x500A9, FrontAfterStart, mode 4), like
the boss select.

## 3. The start (`SceneSelectStart` -> GameStartFrame)

The confirm plays 0x80000000 (BGM off) + 0x121A9, sets `Front::sceneStart` = stage, game type 0, every player in the
select (state other than 9 / 6) to state 0 (start pressed), the others 9, and sub-state 0. The next frame is the normal
game start 0x0045FEF0 (`GameStartFrame` -> `FrontStartStage` -> StartStage sub-state-0 path: rank init 0x00460770,
route record / civilian counters / komono-40 pieces reset, players' row 0 = lives from the setting, score 0) with
event mode = the stage and scene = `SceneSelectFirstScene(stage)`: 0x009A2BC0 at a normal game's Stage_Start of that
stage = 0, 0, 7, 0, 0, 0 (stage 1: the mode select leaves 0; later: the previous stage's 0x0045F000 leftover = scene
table 0x00597890[stage - 1] row (count - 1)'s next scene; read from the auto-play captures' W lines at frames 14659 /
29736 / 44107 / 56007 / 70806). From there the game is the ordinary one: the stage's chapter card (actor 0x60 with
0x009A1A08 = stage), stage end -> nextEventMode = stage + 1 -> ChainStage (MakeCarry) -> ... -> the ending.

## 4. Checks

- Regressions (worktree binaries, main's captures; `scratchpad\sceneselect\regress_ss.ps1` = the coordinator's
  regress.ps1 + the frontend_test lists of modes.md 3 / 8.2 / 9.2): the 18 runs of regress.ps1 identical to
  `build\regress_final1` line for line (fe_orig / fe_play1 only gained the three enemy (T) check lines that
  frontend_test has since; all exact); the 31 other frontend runs all exact except the known partials (boss1 / 2 / 6 /
  8 draws, boss8 camera 10743 / 10777, train0 draws 6151 / 6152, the attract captures' story-mode draws / camera).
  By construction nothing changes with portFeatures off: TaskMenu's entry count / label mapping are the original's,
  SpawnSceneSelect returns false, GameStartFrame only reads sceneStart (-1).
- Headless (`HOTD2_PLAY_MENU=1`): `build\script_scene_select_st3.txt` (mode select at 701, 7 x pad down, trigger at
  920 -> game mode 6 at 920 / page at 921, 2 x pad right -> Chapter 3, trigger at 1100 -> game start 1101):
  `play.exe game --script build\script_scene_select_st3.txt --screenshot out_%d.png --every 60 --every-from 900 --frame 1700`
  shows the entry selected (900), the page on Chapter 2 / 3 (1020 / 1080), the stage-3 chapter card (1500) and the
  boat ride (1700: P1 state 5, lives 3, score 0; no notes). `build\script_scene_select_back.txt`: pad left -> Chapter
  6, reload at 1100 -> mode 4 at 1100, cursor still on the entry. Shots: `build\shots_sceneselect\`
  (boss_960 = Boss mode's select from script_modes_boss0.txt for the look comparison).
- Not verified at runtime: the stage chain after a scene-select start (needs a stage played through; it is the
  normal game's path), two players in the select, the interactive keyboard path (same pad bits as the script).
