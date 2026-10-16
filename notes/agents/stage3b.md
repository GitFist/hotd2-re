# Stage 3 boss, continues and game over (the rest of the auto run)

`game::World` now replays the stage-3 boss (actor type 0x45: the five-headed boss in scene 0x0B), the player deaths
and the five continues during the fight, the no-credits end of game mode 6 and the game over (mode 7, route map) of
the auto-play run frame-exact on every logic check, up to the switch to the title:

```
world_test game build\scene_capture_auto_st3.txt --script build\script_auto_full.txt --stage-start 29736
```

Mode 6 29736..45612 (15877 frames) and mode 7 45613..46375 (763 frames): rand state / calls / sites, task list,
event ops, sounds, live enemies / attackers, enemy hp, lock-on slots, players, aim, gun record, continue timers,
sub-state, credits, mode, letterbox, rank, shake, players' point all exact. World_test stops at the switch to game
mode 3 (46375); frames 46376..46999 of the capture are the title (not part of the World).

**CONFIRMED** = code read + frame-exact against the capture; **CODE** = read and ported, not exercised.
Symbols: `stage3b_symbols.csv`. Code: `src/game/world_st3_boss.cpp`.

## 1. Divergence progress (first logic divergence of the auto_st3 run)

| frame | cause | fix |
|---|---|---|
| 38082 | actor 0x45 (placeholder) | 0x0041FC00 + variants 0 / 1 / 2 / 3, name card, sparks, bar: rand exact to the end at the first build |
| 38476 | task 0x004758E0 missing (serials one behind from here) | actor 0x44 state 14 (0x004736D0) |
| 45611 | mode-6 sub-state 11 not ported | sub-states 0xB / 0xC (0x00460250 / 0x00460350) -> mode 7: everything exact to 46375 |
| lock-on 36360 / 39306 | +0x121 of the birds (0x0042D9C0) and heads is never written (cleared task memory = 0): the table puts them in slots 0 / 1 | `target = 0` |
| camera in the fight | head hit points through world matrices | the head chain also in model-view (from the camera matrix) like the original: hit points / lock point exact |
| camera at blends | 0x00401790 in double | 24-bit rounding of its products (mstack.cpp `DirAngles2`) |

## 2. The boss (CONFIRMED in mode 0 = scene 0x0B)

- Actor 0x45, spawned by op 9 (record params byte 0 = skeleton +0x1F4, byte 1 = variant +0x130C, s16 +0x22 = index for
  variants 2 / 3). 0x0041FC00 (all but variant 5) re-initialises the boss block 0x007DC6F0 with one rand (0x41FC9E)
  and sets the variant's function; the variant's init runs on the next frame.
- Intro (38082..38304): variant 1 (body, skeleton 0x34) waits for flag 3, turns, dives; variant 0 (head 0x49 motion
  0x5A) follows it (0x007DC7F0).
- 38469: five heads (variant 2: 0x0041FE30 / 0x004209B0, block of 0x77C4 bytes, here inline in `HydraTask`) and two
  rocks (variant 3). Heads 0 / 4 break their rocks (0x00422D30), head 0 starts the name card 0x00424900 at flag 1,
  flag 2 starts the fight (phase 1) and the health bar (0x00435E50 in world_st1b_boss.cpp: `SpawnBossBar`).
- Fight: head 2 runs the attack scheduler (countdown 0x007DC71E, `0x00421910(4)` = the boss counter 0x00811200, rand
  0x421519 for the next interval; at most two heads picked, not more than 2 apart); a picked head starts an attack
  (state 5, table 0x7628, target player by players / 0x009C7000) when its idle motion ends; at the hit tick
  Player_TakeHit(target, 1, 9), level - 3, rand 0x42127B. Shots: damage only on the head node with the jaw open
  (> 0x1700) and the neck not turned (z sum 0x1700..0xC000), never head 2 in modes 0 / 1; 15 hp (45 / 3; 9 with two
  players), bar = total / 180; flinch (state 6, 8 frames) or death (state 7, 0x1C17A9); otherwise sound 0x1216A9 and
  a spark 0x004246F0 at the node. The auto player kills three heads; the boss stays to the game over.
- Node chain 0x00421F20 (states 4..6): the skeleton chain again with extra angles (attack lunge, flinch decay).
  The port builds it twice: from identity (world, for the shadow / AnimDraw consumers) and from the camera matrix
  (model-view, as the original) for the hit points and the draws; the other states draw from a model-view chain of the
  AnimDraw angles (`HeadMvChain`). Head draws: 219 / 5400 frames off (blends, rounding), were 5230.
- Debug: `HOTD2_HYDRA_DEBUG=<head index>` prints a per-frame trace of that head to stderr.

## 3. Continues and game over

Nothing new was needed for the five continues (40301 / 41657 / 42613 / 43655 / 44557, credits 5 -> 0: the existing
state-4 / mode-6 sub-state 3 / 4 code); the last death with no credits goes to sub-state 0xB (0x00460250: fog / light
reset (renderer, not modelled), counter 0x007DCCD4) and 0xC (0x00460350), then game mode 7 two frames later. The
mode-7 port (world_gameover.cpp, made on stage 1) is exact on stage 3 including the route map (rand, tasks, players,
mode -> 3 at 46375). Its two known draw / camera cases repeat: 45995 = the map setup frame (play1 3719), 46010.. the
old player task's body copy (play1 3734..).

## 4. Results

auto_st3 (above): mode 6 logic 15877 / 15877, camera 15780 / 15876, draws 12291 / 15696; mode 7 logic 763 / 763,
camera 761 / 762, draws 542 / 548.

Regressions (logic unchanged everywhere): auto_st1 901..14799 exact (draws 11512 / 12787, +1), auto_st2 exact to
22126, play1full 2436 + game over 517 exact, 2p 5099, 2p_continue 5099 (camera 5098), stage-3 demo 814.

## 5. Open / not ported (World::Notes where reached)

- Camera: 32523..32582 + 33672 / 33677 (the civilian alignment 0x0048C310: its body point is ~1e-4 off before the
  motion change already: the world-space skeleton of AnimDraw; switching AnimDraw's body point / civilian alignment to
  a model-view chain made stages 1 / 3 worse, reverted), 39407 and 42822..42864 (1-2 angle units: small node-angle
  differences of odd-tick interpolation of an idle motion), 45995 (map setup, known).
- 0x004019E0's final angle with 24-bit rounding: +58 / +67 draw frames on stages 1 / 3 but -4 on the demo and -1 on
  2p_continue: left in double (comment in mstack.cpp).
- Boss modes 1 / 2 (scenes 0x0D / 2), game types 1 / 3 boss paths, variant 5 (0x00420360 / 0x004231C0), the
  mode-2 jaw wobble 0x00422C70, the splash 0x004248B0 (variant 5) - CODE paths with Notes.
- Renderer state: boss light 0x0041DC70 / 0x0041DCC0, 0x00460250 fog / light colours.
