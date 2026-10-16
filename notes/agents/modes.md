# Game types and menu modes (mode select entries 1..6), save data, name entry

Agent "modes". Symbols: `modes_symbols.csv`. Code: `src/game/world_modes*.cpp` (+ `world_modes.h`), `src/game/save_data.*`.
Status words: **EXACT** = frame-exact against a capture (frontend_test), **CODE** = ported, not exercised by a capture,
**MAPPED** = read, not ported.

## 1. Mode map (mode select 0x00496960, cursor 0x009A2226)

| entry | what | game mode / type | functions | globals | data |
|---|---|---|---|---|---|
| 0 arcade | the game | mode 6, type 0 | (frontend.md) | credits 0x00496B70: setting 0x009C9F25 + 1 (-1 free play) | st1..st6evtbl |
| 1 original | items / special weapons, 6 credits | mode 6, type 1 (0x009CA08C = 1) | Evt_Init 0x0045EBC0: stage 0 variant 5 (+0x009A5900 \|= 8); item select actor 0x6E 0x00488820 (draw 0x00489830, done 0x004895C0 -> variant 1); special weapons Player_TriggerSpecial 0x00414B90; item task 0x00466B40; type-1 branches in many actors (grep "game type 1"); game over 0x00460960 copies the inventory to the save (0x009C90C0 -> 0x009C9F3D) and saves | per player 0x009A2240 + p*0x14: item slots +0/+1, +2 character, +8 special weapon / ammo, +0xC float damage factor (1.2 with the capture's items); inventory 0x009C90C0[33] (save 0x009C9F3D); item list 0x007DD068 | variant-5 scripts of st1evtbl; per-character pols 0xBE + c (0x009A2242) |
| 2 training | lessons, 1 credit | mode 6, type 2 | Stage 6 = trnevtbl.bin (0x00579928[6] = 10), scene 0x009A2BBC (0 = lesson select); select actor 0x6D 0x00496BA0 -> 0x00496DE0 (draws 0x00498090 / 0x004986F0, agent 0x00497F80); lesson start 0x00497360 / instructions 0x00497440 (trigger / start: GO 0x00497640; reload: back) / run 0x00497760 / result 0x00497A70 -> 0x00497C30 (any button: 0x009A2BBC = 0, 0x009C72F4 = 1, save if 0x009C6FF4); lesson actors 0x1A 0x00498FF0 / 0x1B 0x00499420; stage end 0x004603B0 type 2 -> stage 6 again | grades 0x009C8D40[10] (save 0x009C9F29); lesson 0x009C8FB1, difficulty 0x009C9118; lesson scenes 0x00570A78[10] = 1,3,..,19; names 0x00570A8C[] | trnevtbl.bin |
| 3 boss | boss select + one boss / boss rush, 1 credit | mode 6, type 3 | GameStart 0x0045FEF0: stage 0 scene 0x10 (st1evtbl); select actor 0x6C 0x00425010 (draw 0x00425780, entries 0x00425BD0, grade marks 0x00425DF0, agent 0x00497F80); choice -> 0x00425E90 game setup, table switch for bosses outside stage 1; boss-mode title 0x004342E0 -> 0x00434920 (picture, fight timer); records 0x00425F40 at the boss's death; stage end 0x004603B0 type 3 (rank check 0x004157F0 -> name entry, else save + back to the select / next boss of the rush) | 0x009A2268 boss, 0x009C8FB0 difficulty, 0x009A2BBC 1 = rush, 0x009A34C0 record changed, 0x009C7060 time, grades 0x009A2C10[10] (save 0x009C9F33), carousel 0x007DC810..0x007DC89A | bosses 0x0055CBBC {stage, scene}: (0,16) (1,41) (1,39) (2,15) (2,17) (3,27) (3,29) (4,9) (5,14), 9 = rush |
| 4 ranking | ranking screen | mode 8 0x0041A5E0 | root 0x0041B0B0: views 0x00482C40, players, table 0x0041B110 (0x0041A810 background, 0x0041AC20 titles, 0x0041B8C0 arrows), agent 0x0041BDC0 | pages 0..11 (0x0057A354 tables), rows 0x008113A0 / 0x00811220, 0x007C216C..0x007C2174 | tables in the save 0x009C97A0 |
| 5 options | options | mode 12 0x004869E0 | root 0x00486BF0: cursor 0x00486C10, list 0x00486DA0; items 0x005696E0 {x, y, label} + handlers 0x00487320 difficulty, 0x004873E0 lives, 0x004874A0 credits (Free Play only with 0x009C9F5E & 7 == 7), 0x00487250 hidden colour (0x007DD030), 0x004875D0 crosshairs, 0x004878E0 calibration (0x004882D0), 0x00487910 sound test, 0x00487A50 voice test, 0x00487B50 controller (0x00485F90), 0x00487B80 default (0x00401130), 0x00487C00 exit (save, sub-state 2 -> mode 4) | copies 0x009C8E20..0x009C8E36 | save 0x009C9F20.. |
| 6 network | DirectPlay lobby | mode 15 0x0049F380 | 0x0049F610 session; on failure state 8 -> game mode 4 written directly | 0x007DE560 | |
| 7 exit | quit | | 0x0049E930 | | |

Name entry (after a cleared game, and in boss mode for a qualifying time): game mode 6 sub-states 9 (0x00480D90 init) and 10
(0x00481400 per frame). Arcade: 0x004603B0 stage 6 -> sub-state 7 (0x00431550) / 8 (0x00431660, rank checks 0x004157F0)
-> 9. Player state 8 = entering. Letters: 32 tasks 0x004825B0 (objects 0x00811160[32], each a 0x38 block: pos, letter
index), cursors per player 0x00811140.. (pad moves) or the gun (shot letter), 0x1415 = end, 0x1430 = delete; 60-frame
fade (0x007DCF68) after both players are done; invalid names (0x005692D4 list, 0x00481F70) become "HOD"; then
0x0041C610 (tables -> save, Game_Save); type 3: Game_SetState(6) (the boss select again), else Mode_Next.
Table entry insert: 0x004811F0 (score) / 0x00481300 (boss time). MAPPED (not ported).

Game mode 6 sub-state table 0x005679A8: 0 start, 1 next stage, 2 play, 3/4 continue, 5/6 stage end, 7 0x00431550,
8 0x00431660, 9 / 10 name entry, 0xB / 0xC game over.

## 2. Save data (save_data.h)

The original keeps settings, rankings, grades and items in one block 0x009C9120..0x009CA06C (0xF4C), written by Game_Save
0x004011F0 (0x004A0BD0: checksum 0x004A06C0 at 0x009CA068, scrambler 0x004A06F0, magic 0x009CA05F = 7) as four 0x3DB-byte
files disguised as game data: pol\bg_adv19.bin, tex\scr_tod_itm_itamidome2.bin, pol\komono_0.bin, pol\tv2.bin (time
stamps copied from pol\st_adver07.bin). Game_Load 0x004A0B60 / 0x0040AB50 copies it out (lives 0x004D0EDC[F21],
difficulty F20, grades, items, tables via 0x0041C490(1)). Defaults 0x00401060 / 0x00401130.
Layout: 0x009C97A0 12 ranking tables x 10 x {name[4], b4 (character / -1), b5 (stage / character), score or time, -1};
0x009C9F20 difficulty, F21 lives index, F22 colour, F25 credits (0xFF free play), F28, F29 training grades[10], F33 boss
grades[10], F3D items[33], F5E flags (bit 2 all bosses graded; & 7 == 7 free play allowed), F60 / FDC per player
config (0x7C each: crosshair, calibration).
The scrambler key is read from the exe by replaying the byte stores of 0x004A06F0 (no key bytes in the repository).
The port: `SaveData::LoadOriginal` (read only) / `LoadNative` / `SaveNative` (own file "HOTD2RE1" + block + checksum).
WorldConfig::saveDir (original files, default the game directory) / savePath (own file; play.exe: hotd2re_save.bin
in the working directory) / applySaveSettings.

NOTE: two of the four save files in game\ already differed from the installer manifest before this work (earlier
runs wrote rankings: a "   " entry with 150060 points). The captures below were made with that state; a backup is in
build\modes_save_backup and every run restored it (by hash) afterwards.

## 3. Captures (main checkout build\), scripts, environment

Scripted d-pad: script buttons 8 / 16 / 32 / 64 = pad up / down / left / right (gun record bits 0x10..0x80; hook
input_hook.cpp ScriptPadBits, native MakeWorldInput). Only script input; raw-input devices never set them.

| capture | script | content |
|---|---|---|
| scene_capture_modes_rank.txt (3600) | script_modes_rank.txt | boot, mode select down x4, ranking 860..3269 (13 x R, 1 x L), start -> mode 4 |
| scene_capture_modes_options.txt (3300) | script_modes_options.txt | options 880..2980: every item, L / R on the values, exit (save unchanged) |
| scene_capture_modes_boss0.txt (8000) | script_modes_boss0.txt (prefix script_modes_boss0_prefix.txt + auto from 1001) | boss select 843..1000, boss 0 (stage-1 boss) 1001..2455, stage end, name entry 2804..6523 (times out, "HOD"), select again 6524.. (save changed: restored) |
| scene_capture_modes_train0.txt (7000) | script_modes_train0.txt (prefix + auto from 1153) | lesson select 843, lesson 0 (scene 1) 1150..2432 passed, result screen |
| scene_capture_modes_train01.txt (6000) | script_modes_train01.txt (prefix to 2803 + auto) | lesson 0, dismiss 2600, select 2637, lesson 1 (scene 3, lives 2) 2800..3298 passed |
| scene_capture_modes_orig.txt (15000, 1.1 GB) | script_modes_orig.txt (prefix script_modes_orig_prefix.txt + auto from 1124) | original mode: item select 805..1120 (items 3 + 16), stage 1 (route 1, 9, 7, 8, 4, 6, 11, 14) incl. the boss, stage 2 start 14562 |
| scene_capture_auto2_st3boss.txt | script_auto_full2.txt | (for the coordinator) stage-3 boss fight 37900..44199 |

Environment: HOTD2_WINDOWED=1 HOTD2_KEEPACTIVE=1 HOTD2_SCENECAP=1 FRAMES=N TASKS_FROM=600 EVENTS SPRITES SOUNDS RANDLOG,
HOTD2_INPUT_SCRIPT=<prefix> (+ HOTD2_INPUT_AUTO=1 HOTD2_INPUT_AUTO_FROM=<frame>), TASKS / WATCH lists: the auto_st1 lists
plus the mode functions above and 0x9CA08C, 0x9A2BBC, 0x9A2268, 0x9C8FB0, 0x9A2C10:12, 0x7DC810:144 (boss),
0x9C8FB1, 0x9C9118, 0x9C8D40:12, 0x7DD0C0:144, 0x9C72F0:8 (training), 0x9A2240:40, 0x9C90C0:33, 0x7DD068:33 (original),
0x9C8E20:32, 0x9C9F20:72 (options), 0x811220:360, 0x8113A0:360 (ranking). The run helper (mount, env, wait, stop,
dismount, restore the save files) was a scratch script; the commands are reproducible from this table.
Lesson / select timing: the training instructions screen needs a second trigger (0x00497440); the boss select confirms
on trigger / start after 4 frames. Note: one replay of script_auto_full2 lost P1's join at frame 900 (not reproducible;
CPU load from a parallel build at that moment is the suspect) - check P1's state when a replay matters.

## 4. Results (frontend_test, `--script` = the capture's script)

| capture | result |
|---|---|
| modes_rank | modes 16, 3, 4, 8 (860..3269), 4: EXACT - mode / K / Z / camera / sprites 2410 / 2410 / draws 2060 / 2060 in mode 8 |
| modes_options | modes 16, 3, 4, 12 (880..2980), 4: EXACT - sprites 2101 / 2101 (all values / tints / labels), K, Z |
| modes_boss0 | to 2803: boot, mode select (cursor 3), game start type 3, boss select 843..1000 EXACT (K, Z, events, camera, draws, select sprites); boss fight 1001..2803 EXACT for rand / tasks / events / camera / mode (draws 1944 / 1947: the known boss rounding frames); select + boss-title sprites 1433 / 1920: the rest differ only in the fight-timer digits (the original's timer is wall-clock time); first divergence 2804 = stage end type 3 (name entry not ported) |
| play1full / 2p / auto_st1 / auto_st4 | unchanged (section 6) |

## 5. Ported (CODE unless marked)

- Mode select entries 1..6 (ModesMenuConfirm): game type, credits 0x00496B70, 0x009A2BBC = 0, modes 8 / 12 / 15. EXACT for 4/5.
- GameStartFrame: type 2 = stage 6 scene 0x009A2BBC, type 3 = stage 0 scene 0x10; StartStage passes the type to the event
  runner (Evt_Init variants) and runner->globals[0x009CA08C] (GlobalInt readers); PropState.gameMode.
- Ranking (mode 8), options (mode 12; calibration and the controller screen noted only), network (mode 15 = the failure
  path back to the mode select). EXACT.
- Save data module; Store applies the settings (0x0040AB50) to cfg (lives / difficulty / credit setting).
- Boss select 0x00425010 (EXACT), 0x00425E90, boss-mode title / timer 0x00434920 (timer in game frames: the original uses
  GetTickCount), records 0x00425F40 (called at the four bosses' death sites), rank check 0x004157F0 (type 3),
  stage-1 boss / mount type-3 entrances (0x0049B640 / 0x0048FEB0 branches). EXACT through the fight.
- Spawn records at the end of a stage table (the boss select record 0x0097E044 lies past st1evtbl's end).
- SpriteDef::tint (light colour of flags-0x2000 sprites: selected option line, boss portraits) used by the port's
  sprite renderer; SpriteDef::source (tests).

## 6. Not ported / open (next steps, in order)

1. Stage end types 2 / 3 (0x004603B0: rank check -> sub-state 9, else save + stage 0 scene 0x10 / the rush's next boss via
   ChainStage with runner->scene) - World::Notes "stage end in game type 3"; then the name entry (sub-states 9 / 10,
   section 1) - the boss0 capture has 3700 frames of it, ending with "HOD" in the boss-0 table and the save written.
2. Bosses outside stage 1 in boss mode (BossSwitchScene: Evt_LoadTable + Evt_GetScript + sections / coli in the middle of
   a scene; stage-3 boss projectiles 0x00424900 exist only in type 3; "stage-5 boss in game type 3 (0x0047F5F0)" etc.).
3. Training: the select 0x6D (0x00496BA0 / 0x00496DE0, draws 0x00498090 / 0x004986F0; same agent 0x00497F80 skeleton
   0x39) and the lesson flow 0x00497360..0x00497C30, lesson actors 0x1A / 0x1B; captures modes_train0 / train01.
4. Original mode: item select 0x6E (0x00488820 / 0x00489830 / 0x004895C0), special weapons 0x00414B90, type-1 branches
   (variant-5 scripts, item drops, character pols); capture modes_orig.
5. Boss-mode game over -> mode 6 (0x00460960 type 2 / 3), auto-play support for menus (it only plays mode 6).
6. play.exe: the entries start these modes natively; ranking / options work; boss mode runs boss 0 to the stage end;
   training / original start their game type but stop at their unported select screens (the scene script waits).

(Items 1..6 were done by the second pass, section 7.)

## 7. Second pass (agent "modes2"): stage ends, name entry, training, original mode, other bosses

Symbols: `modes2_symbols.csv`. New code: `world_modes_end.cpp` (0x004603B0 types 2 / 3), `world_modes_train.cpp`
(actor 0x6D select / lesson flow, lesson actors 0x1A / 0x1B, grades, lesson-3 cameras), `world_modes_orig.cpp` (item
select 0x6E, items 0x0048A0D0 / 0x00415FE0 / 0x0048A140), `world_modes_items.cpp` (original-mode pickups, item
damage / magazine / lives accessors). Small edits in shared files are all gated by the game type (0x009CA08C) or by
`World::Impl::modes` (absent in world_test runs), see the list at the end.

### 7.1 Results (frontend_test from boot, `--script` = the capture's script)

| capture | result |
|---|---|
| modes_boss0 (0..7999) | mode / sub-state, rand Z / Y, task list, camera view, credits: 7159 / 7159 in mode 6 (boss select, the stage-1 boss, the stage end 2804, the name entry 2805..6523 with its timeout, Game_SetState(6) and the select again 6524..7999); events 3435 / 3435; draws 7134 / 7137 (the 3 known boss rounding frames); select / name sprites 6626 / 7113 (the rest: the fight-timer digits, wall-clock time in the original) |
| modes_train0 (0..6999) | lesson select 843, lesson 0 1101..2432 (instructions, READY / GO, run, grading, the passed screen): mode / rand / tasks / events / sprites all frames exact (6159); camera 6158 / 6159 (2148: 0.115 off for one frame); draws 6098 / 6152 (civilian head look mode 1 0x0048D1F0, renderer-side) |
| modes_train01 (0..5999) | lesson 0, dismissal 2600, stage end -> trnevtbl scene 0 again (2632..2635), select 2637, lesson 1 (scene 3, actor 0x31 behaviour 28) to the passed screen: all logic exact (5159 frames); camera 5158; draws 5093 / 5147 |
| modes_orig (0..14999) | item select 805..1120 (items 3 + 16, inventory, list, sprites 176 / 176), the item effects (damage factor 1.2, credits +2, magazine), stage 1 variant 5 with the type-1 section tables, the item pickups (komono kinds 70..72, komono 69, breakable / lamp drops, captions), the boss with the damage factor, the stage-2 start 14562: mode / rand / tasks / events / credits 14199 / 14199; camera 14198 (10978); draws 12623 / 13910 (actor 0x24 / 0x25 extras of variant 5, see open items) |
| rank / options / play1full | unchanged (exact) |

world_test: auto_st1, auto_st2 / auto_st3 (`--prefix auto_st1 --script script_auto_full.txt`), auto_st4 / st5 / st6,
the ending (`auto_st6_end --prefix auto_st6`), play1full, 2p, stage3 demo: unchanged against the pre-change baseline.

frontend_test changes: native draws of task serial 0 are skipped (the capture's ^0 lines are not read, as world_test);
capture model-id aliases (pointer cache) are accepted when the matrix matches and listed (as world_test); `--dump`
prints rand calls and live enemies; `--save-native <file>` loads / writes the port's own save (tests with grades set:
a scratch copy only).

### 7.2 What was ported

- 0x004603B0 for game types 2 / 3 (ModesStageEnd): training -> event mode 6 again (scene = 0x009A2BBC, the runner's
  copy `globals[0x009A2BBC]`); boss mode -> 0x004157F0 for both players (one qualifying: sub-state 9, the rush moves on
  to the next boss first), else Game_Save when 0x009A34C0, then the select (stage 0 scene 0x10) or the rush's next boss
  (0x0055CBBC) through sub-state 1 / ChainStage.
- Name entry for type 3 (world_ending.cpp): players 8 / 9 by 0x004157F0, 0x00481180 / 0x00481300 (time rows), the
  boss's table 0x00480F70 (ModesTableIndex), 0x009C8E00 = 1, the forbidden-name list 0x005692D4 (strcmp, "HOD"),
  0x0041C610 (tables -> save + the port's file), type 3: players 8 -> 0 and Game_SetState(6). The arcade name entry now
  uses the loaded table ModesState::tables[0] when booted (defaults 0x00530048 otherwise, as before).
- Game over (mode 7) for types 2 / 3: players 6 -> 0, Game_SetState(6) (the lesson / boss select); no 0x14F / 0x4E
  requests; type 1: inventory -> save at 0xC6. Player rows 0x00414770: types 2 / 3 always reset the lives.
- Training: actor 0x6D (op 0x0A records are {type, s16 state}: 0x00408990), the select with its carousel / grade marks
  / captions / agent, Evt_GetScript into the lesson scene, the lesson flow 0x00497360 .. 0x00497DB0 (READY / GO,
  timers 0x00498C10, per-lesson end conditions, the camera reset and the params' script / variant, grading, grades
  0x00498ED0 + save), event flags 0xF0 / 0xF1 / 0xF2 / 0xF4, 0x009A2234 (runner global), lesson actors 0x1A / 0x1B
  (CODE: shuffled spawns, 0x0045BC10, the time display 0x004995B0), lesson-3 cameras (EventHost::SegmentCamera, from
  0x004033B0 / 0x00403510), out of lives fails a lesson (0x00413E90), no flashes after the end. Type-2 branches:
  enemies / actor 0x31 frozen with plain node draws until flag 0xF1 (0x00454860 / 0x00453B30, 0x00449EF0 /
  0x0044A2B0), civilians (0x0048A920: counters from flag 0xF1, 0x009A2234 when hurt; 0x0048A3E0: child hp by lesson /
  difficulty), actor 0x31 behaviour 28 (0x0044F510), coliT.bin for event mode 6 (names 0x004D1CC4).
- Destroyed civilians stay readable for their children (World::Impl::civGhosts; the original's freed block keeps its
  contents): a use-after-free crash in train01 at 2411..2415 (heap layout dependent).
- 0x009C8E4C (the actor updated last: enemies, civilians, actor 0x31) as World::Impl::actor8E4C.
- Original mode: the item select 0x00488820 (camera path 0x36, the case models 0x145F / 0x1460, both players' columns,
  the combination table 0x0056B014, two-player returns), 0x0048A0D0 at every game start, 0x00415FE0 (rand 0x004160FB
  for item 0x1C), 0x0048A140 (lives, magazine, 0x00416420 weapon row, credits 0x00406FA0 / free play), damage factor in
  0x00409430 (24-bit products, _ftol; item 0x1D boost 0x004C4CA0), the boss / mount damage (0x0049D640 / 0x00491420),
  lives cap +5 (0x00415630, 0x00434EF0, Player_AddLife), magazine +8 (reload / off-screen reload / infinite -1),
  Stage_SelectTables 0x004014C0 (type-1 section tables), pickups 0x004629C0 / 0x004675A0 / 0x00466BE0 / 0x00467B90 /
  0x00475E40 / 0x00475D00, komono 69 (0x00470500), lamp drops (komono 7), breakable drops (slot = +0x2A0), komono 34
  with item 0x1E, the final boss's item reward (0x00428400).
- Boss mode: bosses outside stage 1 (BossSwitchScene: Evt_LoadTable, cfg.eventMode, Evt_GetScript, section 0, coli);
  stage-5 boss (0x0047F5F0: camera path 0x180 at 598, sub-state 3, flag 0x16) and final boss (0x00426A70: path 0x185 at
  1810, motion 0xA3, flag 0x32, state 2) type-3 starts; the final boss's records 0x00425F40. Smoke runs (scripted
  select, no shooting) of bosses 1..7: no crashes, no notes for bosses 1, 5..7; boss 2 (stage 2, scene 0x27) meets
  actor types 21 / 23 and vehicle motions 65 / function 0; bosses 3 / 4 hit the stage-3 boss's type-3 path (the
  stage-3 boss agent's file, not touched). Boss 8 / the rush need all grades.
- play.exe: Left / Right keys (pad 0x40 / 0x80) for the selects. The original's boss / lesson / item selects, lessons,
  the boss-mode name entry and the return to the selects run natively.

### 7.3 Open items

1. Special weapons (Player_TriggerSpecial 0x00414B90, weapon rows 0x00579ED8, tracer extras 0x00416F70, sounds
   0x004EC9A0): a Note; weapon 0 plays as 0x00414940 with the item magazine (exact in modes_orig). Other characters'
   bodies (0x0048A140 skeleton 0x39 + c / 0x21 / 0x34): a Note. Game over residency for type 1 (pols 0xBE + c).
2. Training lessons 2..9 (smoke runs with a scratch save, grades 3, no shooting): all reach their result screen except
   lesson 9 (waits for the player); unported there: komono kinds 0 / 67 (lesson 2), 68 (lesson 3), actor type 35
   (lesson 4), vehicle motions (lessons 7 / 9). No captures: lessons 2..9, 0x1A / 0x1B are CODE.
3. Variant-5 actor 0x24 / 0x25 extras (draw differences in modes_orig), civilian head look modes (0x0048D1F0).
4. play.exe name entry: the letters / panels (pol nment.bin) are not visible in the headless screenshot (renderer /
   asset side; the logic and the draw list are exact). (Visible since the per-strip cull mode: checked in section 8.)
5. Boss mode: the rush (needs all eight grades), the stage-3 boss in type 3 (0x004209B0, stage-3 boss file), the
   stage-2 scene-0x27 actors.

(Items 1, 2, 5 and the story / network / menu items of frontend.md were taken up by the third pass, section 8.)

Shared-file edits (all small): world_ending.cpp (tables, type-3 rows, HOD list, save), world_gameover.cpp (types
1..3), world_player.cpp (lives rows, type-1 magazine, type-2 death / flashes, special-weapon note), world_enemy.cpp /
enemy.cpp / enemy.h (type-2 frozen nodes, type-1 damage factor, 0x009C8E4C), world_civilian.cpp (type-2 branches,
ghosts, 0x009C8E4C), world_st2_actor31.cpp (behaviour 28, type-2 nodes), world_st2_actors.cpp / world_scene1.cpp /
world.cpp (type-1 items, LoadStageTable type-1 tables, civGhosts reset), world_st1b.cpp / world_st1b_boss.cpp /
world_st1b_civ.cpp / world_st3_actors.cpp (stage end hook, type-1 damage / lives), world_st5_boss.cpp /
world_st6_boss.cpp (type-3 starts, records), world_collision.cpp (coliT.bin), event_runner.* (SegmentCamera hook),
world_front.cpp (0x0048A0D0 at game start), world_internal.h (declarations, actor8E4C, civGhosts).

## 8. Third pass (agent "menus"): lessons 2..9, bosses 1..8, special weapons, story / attract, network, menus

Symbols: `menus_symbols.csv`. New code: `world_modes_story.cpp` (mode-11 title card, actor 0x50 captions, actor 0x26
state 0), `world_modes_net.cpp` (game mode 15), `world_modes_weapons.cpp` (0x00414B90 / 0x00414E40 / tracer extras).

### 8.1 Capture setup (hook, test options)

- `HOTD2_NOSAVE=1`: Game_Save's writer 0x004A05C0 is skipped (the run never writes the user's save files). Every
  capture of this pass used it. The run helper (scratch) records the save files' hashes before a run and copies them to
  `build\saves_<topic>\` (frontend_test `--save-dir` layout) when they differ from `build\modes_save_backup`; it never
  writes them back. (One exception, reported: before this rule was in the helper, it restored pol\tv2.bin and
  tex\scr_tod_itm_itamidome2.bin from modes_save_backup after another agent's run had changed them; the changed
  copies are in `build\saves_after_modes_train2\`.)
- `HOTD2_POKE=<frame>:<addr>:<hex>,...`: test setup written into game memory at a boot frame (training grades
  0x009C8D40 + block 0x009C9F29, boss grades 0x009A2C10 + 0x009C9F33, inventory 0x009C90C0 + 0x009C9F3D). The port
  gets the same state with `frontend_test --save-patch <va>:<hex>` / `play --save-patch` (WorldConfig::savePatch,
  applied to the loaded save block before Boot copies it out).
- play.exe: `--save <file>` (the port's own save, also headless), `--trace` (mode / sub-state changes + notes),
  `--save-patch`.
- frontend_test: the lessons' time-left sprites 0x004995B0 are compared with the select / name sprites.

### 8.2 Captures (main checkout build\, all with HOTD2_NOSAVE, the stage-4 task / watch lists + the mode functions)

| capture | script (prefix: `script_<topic>_prefix.txt`, full: `script_<topic>.txt`) | test options | content |
|---|---|---|---|
| scene_capture_modes_train2..9.txt (4500) | modes_train<k> (auto from 1304) | `--save-patch 9C9F29:03030303030303030300` | grades 0..8 = 3 poked at 100; lesson k, difficulty 0, from the select to the result |
| scene_capture_modes_boss1..7.txt (9000) | modes_boss<k> (auto from 1101) | - | boss k from the select, the fight, the name entry, the select again |
| scene_capture_modes_boss8.txt (12000) | modes_boss8 (auto from 1151) | `--save-patch 9C9F33:0101010101010101` | boss grades 1 poked: the final boss |
| scene_capture_modes_weapon1..3.txt (5000) | modes_weapon<k> (auto from 1144) | `--save-patch 9C9F3D:010101` | items 0..2 poked into the inventory; original mode with weapon k |
| scene_capture_menus_net.txt (0..1048) | script_menus_net.txt | - | network mode on this machine: the lobby (frames 900..1048, shot build\shots_menus_net\shot_00950.bmp), then CREATE SESSION at 1050 hangs the game (8.6) |

Regression commands (add to the list of section 3 / 7.1):

```
frontend_test game build\scene_capture_modes_train<k>.txt --script build\script_modes_train<k>.txt --save-patch 9C9F29:03030303030303030300   (k = 2..9)
frontend_test game build\scene_capture_modes_boss<k>.txt --script build\script_modes_boss<k>.txt                                      (k = 1..7)
frontend_test game build\scene_capture_modes_boss8.txt --script build\script_modes_boss8.txt --save-patch 9C9F33:0101010101010101
frontend_test game build\scene_capture_modes_weapon<k>.txt --script build\script_modes_weapon<k>.txt --save-patch 9C9F3D:010101      (k = 1..3)
frontend_test game build\scene_capture_menus_net.txt --script build\script_menus_net.txt
frontend_test game build\scene_capture_attract_loop.txt [--resync]   (demo 3 / story-mode Y lines: a new capture is still wanted; one try crashed)
```
(add `--save-dir build\saves_<topic>` when that folder exists: the run saw save files other than modes_save_backup.)

### 8.3 Results (frontend_test, game mode 6, `--script` = the capture's full script)

| capture | rand / tasks / events / camera | draws | notes |
|---|---|---|---|
| train3 (creature) | all 3659 | 3654 / 3654 | exact |
| train4 (the mount) | all 3659 | 3655 / 3655 | exact |
| train5 | all 3659 | 875 / 3653 | an enemy's nodes not drawn natively from 1722 (renderer side) |
| train6 | rand 3659, tasks 3656, events 3652 | 1453 / 3653 | the lesson ends one frame later natively: the lesson clock is GetTickCount (wall clock) |
| train7 (the riders) | all 3659 | 3655 / 3655 | exact |
| train8 | all 3659 | 3654 / 3654 | exact (after the main merge) |
| train2 | rand / tasks to 1460 | | komono kind 0 is on main now (routes); its training variants (0x009C9118) and komono 67 (0x00470080) are not ported |
| train9 (the juggled target) | all 3659 | 3654 / 3654 | exact |
| boss1 (stage-2 boss) | all 8159 | 7845 / 8147 | exact logic incl. the name entry and the select again |
| boss2 (stage 2 scene 39) | rand to 6072, tasks to 1103 | | actor types 0x15 (0x00441750) / 0x17 (0x004422D0), vehicle motion 65: not ported (stage-2 route objects) |
| boss3 (stage-3 boss) | all 8159 | 8149 / 8149 | exact |
| boss4 (stage-3 boss, mode 1) | all 8159; camera 8144 | 8110 / 8149 | wake marks / water ported |
| boss5 (stage-4 boss) | all 8159; camera 7877 | 7686 / 8149 | camera 0.19 off from 2497: the carried object leaves the boss's hand 0.0137 higher at the throw (2480: hand node matrix, draw precision), its flight and hit come later |
| boss6 (stage 4 scene 29) | all 8159 | 8133 / 8149 | vehicle motion 8 ported |
| boss8 (final boss) | rand to 3069, tasks to 1154 | | actor 0x26 state 6 / 7 in game type 3 (0x0048F930 -> 0x0048FD00) and the creature's pattern 3 (0x0042C0B0..): not ported (actor 0x26 belongs to the attract-leftovers work now) |
| weapon1 (burst) | all 4199 | 3997 / 4017 | exact logic incl. the spread rand calls |
| weapon2 (full auto) | all 4199 | 3998 / 4018 | exact logic |
| weapon3 | rand / tasks to 2669 | | the original records a level impact (0x004073B0 material 0x53, rand 0x00407610) in the frame a weapon-3 shot kills enemy 65590; the port does not (hit system / ShotImpact) |
| menus_net | mode 15: all 148 | | the lobby (to the original's hang, 8.6) |
| boss7 (stage-5 boss) | tasks to 1886, camera to 1864, rand to 1962 | | camera 0.84 off at 1865 (stage-5 boss precision, as in the arcade run: stage5.md camera 14669 / 14893), then one more projectile, the player dies natively |

The fight-timer digits (boss mode) and the lessons' time left are wall-clock time in the original (GetTickCount):
their sprites differ by a digit in places.

### 8.4 Ported (addresses)

- Story modes: actor 0x60 in game mode 11 runs 0x00434DA0 (200 frames: the story modes now end on the original's
  frame; the attract loop's mode switches are exact without resync); actor type 0x50 (0x004997E0 -> 0x004998A0 /
  0x00499A80, caption sprite effects 0x00499C60); actor 0x26 state 0 (0x0048E350, the parked car of mode 10).
- Attract demo 2: the held object's throw (0x00442B90 state 5 -> 0x00443DC0; the hit in front of the view is st4's
  0x00444160): demo 2 rand / tasks exact for the whole demo. Demo 3: komono kinds 73 / 75 / 76 outside game type 1
  (0x00470B70 / 0x004710C0 / 0x00471330): tasks exact to 28988 (rand to 28980: an actor 0x31 rand call, see 8.5).
- Network (game mode 15, 0x0049F380): the lobby without a network game (8.6).
- Training: komono 68 (0x00463E50, world_st1b.cpp) and the creature's motion gate in game type 2 (0x00471FA0);
  actor 0x20 held in scene 0xD (0x004494D0); the mount alone (actor 0x23 variant 2: 0x00490EB0 / 0x00490FD0,
  SpawnMount23); actor 0x31 behaviour 18's walk by difficulty in lesson 5 (0x0044E2A0); actor 0x21 + vehicle in game
  type 2 (0x00452540 / 0x004525C0 / 0x004528B0 / 0x00452930); vehicle motion function 9 (0x00445050, lesson 9's target).
- Boss mode: stage-3 boss mode 1 tasks 0x00424E10 / 0x00424C10; vehicle motions 5 / 8 (0x00441000).
- Original mode: 0x00414B90 for game type 1 (weapon rows 0x00579ED8: weapon 1 burst of three with the spread rand
  calls 0x00414CE9.., weapon 2 full auto, weapon 3 cooldown 4), reload 0x00414E40 (weapon 2 only when empty, item
  sounds 0x004EC9C0), shot sounds 0x004EC9A0, flash types 3 / 4, tracer type 5 (path 0x194, model 0x109D), the third
  effect ring 0x009A2700 (type 4, models 0xA6F..).
- Arcade ranking save (item 4 of the brief): already in place (0x0041C610 = ModesState::Store, forbidden names 0x00481F70 /
  0x00481FB0); verified end to end: play.exe with `script_auto_full2.txt` and `--save` stores the ending's entry
  (150060, "   " after the timeout) in the port's file, and the Rankings screen shows it (rank 3 of the arcade table).

### 8.5 Open (most of it done by the fourth pass: section 9)

- Lesson 2: komono kind 0 (barrel stacks 0x00462A80 / 0x00464620) is ported on the routes branch (world_routes.cpp);
  after that merge the game-type-2 variants of 0x00462A80 (difficulty 0x009C9118: hp 1 / model 0x1A0F / state 6 for some
  barrels) and the type-2 branches of 0x00464620 are what lesson 2 still needs; lesson
  5's enemy node draws, lesson 6's wall-clock end.
- Boss 2 (stage-2 scene 39 actors 0x15 / 0x17, vehicle motion 65), boss 5 / 7 camera precision, the rush.
- Story modes: the remaining differences are actor 0x24 / 0x25 (NPC) functions (mode 11 from 18036, mode 10 from
  22861) and the sky yaw leftover; demo 3 rand at 28981 (actor 0x31, task 0x00449910, in stage 4 scene 5).
- Special weapons: weapon 3's level impact (8.3); weapons 0xC / 0xD (items 0xB / 0xC) not captured.
- Boss 8: actor 0x26 states 6 / 7 in game type 3 and the final boss creature's pattern 3.
- Ownership note: actor 0x26 state 0, komono 73 / 75 / 76 and the held object's throw (state 5) were ported in this
  pass before those areas went to the attract-leftovers work; they are in world_modes_story.cpp, world_modes_items.cpp
  and world_st1b_held.cpp.

### 8.6 Network mode on this machine

0x0049F380 does not depend on a network: state 0 (0x0049F610) creates the DirectPlay objects (0x004A0C40,
CoCreateInstance; DirectPlay is registered here), the font and the four bitmaps tex\scr_etc000..003; only a missing
bitmap fails (-> state 8). State 1 (0x0049FF40) is the lobby menu (GDI blits 0x0049F1E0 of table 0x00598148: title,
JOIN SESSION / CREATE SESSION / EXIT, cursor up / down, trigger / start, reload = exit). JOIN (state 4, 0x0049FA80):
an edit control for the address (INPUT IP ADDRESS panel); an empty address only plays 0x500A9; a typed one that finds no
session ends in state 9 (FAILED!!), any button -> state 8 -> game mode 4 (written directly). CREATE (state 3,
0x0049FA20) opens the session "houseordead" and waits (state 6, PLEASE WAIT!) until reload (state 10, CANCEL!!).
On this machine (the DirectPlay Windows feature not enabled; the CLSIDs are registered to the stub dplayx.dll) the
original shows the lobby (capture menus_net: state 1 at 900..1049, the shot matches the port's), and CREATE SESSION
(trigger at 1050) makes Windows open its 'enable DirectPlay' prompt (Fondue.exe /enable-feature:DirectPlay
/caller-name:Hod2.exe) while the game hangs; declining it (the prompt was closed, nothing installed) ends in an access
violation (the game's crash handler faults at 0x004C2E5E): the DirectPlay interfaces stay null. JOIN would reach the
same call (0x004A0FC0) on its confirm.
The port has no DirectPlay and does not hang or crash: CREATE ends in FAILED!!, JOIN's confirm stands for an address
with no session (FAILED!!);
everything else as above. play.exe shows the lobby (WorldHud::screens with a pixel source rectangle).

## 9. Fourth pass (agent "menus2"): lessons 2 / 5 / 6, bosses 2 / 5 / 7 / 8, weapons 3 / 0xC / 0xD, attract demo 3

Symbols: `menus2_symbols.csv`. New code: `world_st2_water.cpp` (actor types 0x15 / 0x17, the water level 0x00442390).

### 9.1 The wall-clock timer (lesson 6, the boss fight time)

0x0049DF20 = ftol((GetTickCount() - 0x007DD174) * 0.06 + 0.5 + 0x007DD178) (0x0049DF00 starts it): frames by the wall
clock, called by the lesson run 0x00497760 (+0x1330) and the boss-mode title 0x00434920 (0x009C7060). The captures
show its value in every frame: the 0x6D actor's T line (+0x1330 while 0x00497760 runs) and W 0x009C7060. The frame
that ends a lesson resets +0x1330; there the value is the last one plus the change of the time-left value read back
from its sprites (0x00498C10: digits 0xACF.. from 0x00498D4C / 0x00498D70, scale (d % 60) / 60 + 1 below 181).
`World::SetWallClock(count)` takes it for one frame (ModesState::Clock; without it the timer counts game frames, as
play.exe does); frontend_test feeds it from the capture by default (`--no-wall-clock`: frames). Lesson 6 now ends
on the original's frame (the original's clock jumped 838 -> 840) and the fight-timer / time-left sprites are exact.

### 9.2 Captures (main checkout build\, HOTD2_NOSAVE, the capenv lists of section 8)

| capture | script | test options | content |
|---|---|---|---|
| scene_capture_menus2_weapon12.txt (5000) | script_menus2_weapon12 (prefix + auto from 1144) | `--save-patch 9C9F48:0101` | items 0xB / 0xC poked at 100 (HOTD2_POKE=100:9C90CB:0101,100:9C9F48:0101); with the save's items 3, 7, 16 the list is 3, 7, 0xB, 0xC, 16: 2 x down = custom air gun (weapon 0xC) |
| scene_capture_menus2_weapon13.txt (5000) | script_menus2_weapon13 | `--save-patch 9C9F48:0101` | 3 x down = toy gun (weapon 0xD, item flag 0x009C88AC) |
| scene_capture_menus2_attract.txt (31000) | none (no input) | `[--resync]` | the attract loop, K / W / Z / Y / E / S from 15400 (HOTD2_SCENECAP_FROM / TASKS_FROM 15400, extra tasks 0x426960, 0x434DA0, 0x4998A0, 0x499A80, 0x48E350, 0x449910, W 0x9A3400), HOTD2_DEMOKEY_GUARD=1 |

The earlier attract-loop try crashed at the stage-1 demo start (~2785): the demo key loader 0x00426980 opens
etc\key_*.bin, which is not installed; GetFileSize gives -1, 0x004A7400 returns a 0-byte block, ReadFile fails, and the
PRS decompress of the heap memory after that block writes 122880 bytes into the 0x10000-byte buffer 0x009A2228 (+0x100)
and the blocks behind it (the custom heap's links): some runs crash, depending on the heap. The key data is never read
(0x009A2228 has no other reference). Hook option `HOTD2_DEMOKEY_GUARD=1` skips that one decompress when the file is
missing (the allocations stay); use it for captures through the attract demos. frontend_test now runs the frames
before a capture's first frame (HOTD2_SCENECAP_FROM captures) without comparing them.

Regression commands (add to 8.2):
```
frontend_test game build\scene_capture_menus2_weapon12.txt --script build\script_menus2_weapon12.txt --save-patch 9C9F48:0101   (and 13)
frontend_test game build\scene_capture_menus2_attract.txt [--resync]
```

### 9.3 Results (frontend_test, game mode 6 unless noted; after merging main 8a8dd94)

| capture | logic (rand / tasks / events / sounds / sprites) | camera | draws |
|---|---|---|---|
| train2..9 | all 3659 exact (train2 was to 1460, train6 to 2298) | all | all (train5 was 875 / 3653) |
| train0 / train01 | exact | exact | 6151 / 6152 (civilian head look) / 5147 / 5147 |
| boss0 / boss1 / boss3 / boss6 | exact, timer sprites now exact | exact | 7137 / 7137, 7845 / 8147, 8149 / 8149, 8133 / 8149 |
| boss2 (stage 2 scene 39) | exact (was rand to 6072, tasks to 1103) | 7699 / 8159 | 6895 / 8146: the boss's legs in the water (leg IK, see 9.5) |
| boss4 | exact | 8144 / 8159 | 8110 / 8149 (stage-3 boss mode 1, not touched) |
| boss5 | exact | 8159 (was 7877) | 8143 / 8149 |
| boss7 | exact (was rand to 1962) | 8159 | 8147 / 8147 |
| boss8 | all 10777 exact incl. the name entry (was tasks to 1154) | 10743 / 10777 | 10528 / 10761 (0x0042BF30 trail history: uninitialized block in the original) |
| weapon1 / 2 / 3 / 12 / 13 | exact (weapon3 was to 2669) | exact | 3997..3998 / 4018 (0x0049B260 intro bodies, as play1full) |
| orig | exact | exact | 13801 / 13910 |
| attract (old capture, from boot) / menus2_attract `--resync` | modes 3 / 4 / 5 / 9 / 11 exact; demo 2 and demo 3 rand / tasks / events / sounds exact (was rand to 28980); mode 10 tasks to 23022 | demo 2 3475 / 3484 | mode 11 5150 / 6137 (was 3592) |

World tests (the coordinator's suite) unchanged in logic; boss / weapon / training checks of frontend_test gained
`rank 0x009C8E96` and `players' point 0x009C71E0` (all exact).

### 9.4 Ported (addresses)

- Lesson 2: komono kind 0 in game type 2 (0x00462A80: one-shot barrels by the difficulty 0x009C9118, skeleton 6,
  model 0x1A0F), 0x004650F0 in type 2 (no score, hits always counted), komono kind 67 (0x00461CF0 case 0x43: board
  0x00470080, barrels 0x004702E0; 0x00465170 shared as SpawnBarrelPiecesFrom).
- Lesson 5: actor 0x1A's pol requests 0x0041D650 (all but the first enemy of the order) and releases 0x0041D690.
- Lesson 6 / timers: 9.1.
- Weapon 3: Hit_Deliver 0x00404DB0 in game type 1 with weapon effect 3: effect 0x53 at the hit (mesh: its point,
  sphere: view centre pushed back by radius - 1, x the camera matrix), rand 0x00407610.
- Items 0xC / 0x15 (0x009C88AC / 0x009C88A8): the head (1.5, 1, 1.5) and hands / feet (2, 1, 2) of the player body
  (0x00416570), actor 0x24 (0x004835D0), actor 0x25 (0x00485260; skeleton 0x38: head (2, 2, 2) with item 0x15) and
  the part lists (0x004124F0).
- Boss 2: actor 0x17 waves (0x004422D0, pool 0x005644E4: 0x004420C0 / 0x004421B0, queries 0x00442110 /
  0x00442210), the water level 0x00442390 (WaterLevelAt; the stage-2 boss's 9 water queries now pass their point),
  actor 0x15 rafts (0x00441750 -> 0x004418C0; foot points 0x009A3500 as World::Impl::st2Feet), vehicle motion
  function 0 (0x0041EBB0) is a no-op.
- Boss 5: the stage-4 boss's carried object drawn / thrown from the hand node's model-view chain (0x00443200 /
  0x00442B90: world = model-view x camera matrix).
- Boss 7: stage-5 boss behaviour 8 (0x0047D5E0) reads the hop table 0x00596848 after case 1's aggression change.
- Boss 8: actor 0x26 states 6 / 7 in game type 3 (0x0048F930 -> 0x0048FD00 at once), the final boss's creature of
  patterns 3 / 4 (0x0042D490 / 0x0042D530 / 0x0042D820).
- Attract demo 3: actor 0x31 PickTarget 0x0044CA40 (the side check uses 0x00409C90's result, not the players in game),
  behaviours 32 (0x00450B20), 24 (0x0044EA50), 25 (0x0044EC80), 0x0044CE60 with +0x136C & 0x400 (table 0x00592A00).
- Actor 0x24 node extras (0x004835D0 after the node model: kind 0x53 node 1 0x9D5 + 0x009A32A0 % 100, kinds 0x31 /
  0x54 / 0x55, the 0xE24 flash of kinds 0x42 / 0x43; its shell casing 0x00485DE0 is a Note).
- The "collision mesh address outside the loaded files" note: an actor killed by Task_Kill while still in the hit
  target copy left a dangling pointer that the next frame's floor / shot queries read (random). TaskSystem::keepFreed
  keeps such blocks readable for 3 FreeDead calls (the original reads the freed block); the note is gone everywhere.

### 9.5 Open

- Boss 2: the stage-2 boss's leg IK / floor follow in the water (0x00476AD0 with flag 0x20000 clear; it also writes
  the foot points 0x009A3500 that push the rafts) - the boss's draws from 1106 and the camera (looks at the boss) from
  1409. Owned by the leftovers work (stage-2 boss legs).
- Mode 10: the held object's throw state 3 (0x005926A8[3] = 0x004432D0) at 23023 (leftovers: held throws).
- Mode 11 draws: actor 0x24's birds (0x00483F00) drawn natively one frame early, 0x00483030 draws.
- Boss 8 draws: the trail 0x0042BF30's history is copied only into slot 0 by 0x0042BEE0 (the other 19 slots are the
  heap's leftovers in the original).
- Items 0xC / 0x15 on the civilians (0x0048D1F0 at 0x0048D888..), ClearChar 0x004357F0 and 0x004208F0 (not in the
  captures).
