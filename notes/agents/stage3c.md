# Stage-3 boss fix and the continuous game to the ending (stage3c)

One native game played from the game start with `build\script_auto_full2.txt` (the auto player that damages the
stage-3 heads and wins without a continue) now matches the original on **every logic check and the camera** in every
capture of that game, from stage 1 to the title switch after the ending:

```
world_test game <capture> --script build\script_auto_full2.txt --prefix build\scene_capture_auto_st1.txt
```

| capture | frames | logic checks | camera | draws |
|---|---|---|---|---|
| scene_capture_auto2_st3boss.txt (the fight) | 37900..44199 | all exact | 6300 / 6300 (was 5991) | 5623 / 6290 (was 5266) |
| scene_capture_auto_st4.txt | 44000..56099 | all exact | exact | 10802 / 11915 (was 10765) |
| scene_capture_auto_st5.txt | 55900..70899 | all exact | 15000 / 15000 (was 14776) | 13294 / 14812 (was 12845) |
| scene_capture_auto_st6.txt | 70700..86999 | all exact (lock-on was 13672 / 16300) | 16300 / 16300 (was 16002) | 14777 / 16271 (was 13962) |
| scene_capture_auto_st6_end.txt | 86900..97157 (title) | all exact (credits were 1 off at 97157) | exact | 10052 / 10254 |

The earlier part of the same game (auto_st1 / auto_st2 / auto_st3 captures, `script_auto_full.txt` = the same input
to 38081) is exact too: stage 2 players' point (was 15240 / 15400) and camera (was 15335) exact, stage 3 camera (was
15816 / 15912) and enemy positions (was 2777 / 2908) exact, game over + route map camera exact.

Status words: **CONFIRMED** = read + frame-exact against a capture; **CODE** = read and ported, not exercised.

## 1. State at the start

The previous agent (branch `worktree-agent-a4d493b955dd9ecf6`, stopped for a hardware change, no notes) had already
ported Boss3 variant 5 (0x00420360 / 0x004231C0: the big head swimming along the recorded path, camera, lunges,
splashes 0x00424800), actor 0x44 state 10 (0x00473370) and komono 31's scene-0xB models (frame counter 0x009A2BAC).
After merging it, the fight was already exact on rand / tasks / events / players; only the camera (39210..) differed.

## 2. Divergence progress

| first divergence | cause | fix |
|---|---|---|
| st6 lock-on 81456 | final boss / creature +0x121 never written (cleared task memory = 0) but the port used -1, so Build put them in slot 2+ | `target = 0` (0x00426A70 / 0x0042C830 do not write it) |
| ending credits 97157 | the ending's title switch clears the credits (0x00406FD0 called by 0x00431550 tail and the name entry 0x00481F15) | cfg.credits / freePlay cleared there |
| camera 39210 (heads) | node 1 angle 32767 vs -32768 after a flinch blend: 0x004019E0's final fpatan * 65536/2pi is rounded to float under 24-bit precision control | `MatrixToAngles`: float-rounded product (also BlendRotation 0x00412750: fsqrt / products / theta*f / phi*f in float, RotAxis 0x004A98D0 argument rounded, normalize 0x004AA9C0 in float order) |
| camera 39205.. | `mx::Translate` / `mx::Transform` / `TransformDir` / `MulLeft` / `MulRight` summed in a different order than the x87 code (each product / sum is rounded to float, so the order matters) | all five now follow the original's operation order (MulLeft 0x004A92A0 / MulRight 0x004A9570 per element, generated from their disassembly) |
| camera 41441 | variant-5 camera used the double `mx::DirToAngles` | `mx::DirToAngles` = the 24-bit Cam_DirToAngles 0x004016B0 everywhere |
| st5 camera 60252 (Boss2 mount) | body point +0x100 one ulp off: the port's skeleton is world space; 0x004114C0 takes the node's **model-view** translation (+0x10C) and `top x camera matrix` (0x004A9570) for +0x100 | AnimDraw builds the chain twice: world (draws, shadows, attachments, as before) and model-view from the active view (`EnemyNode::mv`); hit points (+0x68) and the body point come from the model-view chain |
| st6 camera 84085 | the lock-on slot point was a copy taken at `SyncLock`; the original reads +0x100 live (the death frame updates it in the draw only) | AnimNode writes the body point into the lock target too |
| st2 players' point 18450 | player +0x80 callback 0x00415E40 (stage-2 cutscene) not ported; the camera rail 0x0040C9C0 reads the bodies | 0x00415E40 / 0x00415F60 / 0x00415F90 ported; camera writes of the players' draw bit now apply on every write (counter), not on a change of the camera's copy (this made a stale bit draw the body into stage 3 and crash) |
| st2 camera 28683 (stage-2 boss) | node points via `world * drawView * inverse` | `NodeTimesCamera`: node model-view x camera matrix in the 0x004A92A0 order (chest hit, 0x0045CE70, death points) |
| st3 camera 32523 (civilian) | 0x0048C310 alignment through world space | node 1 model-view x camera matrix (0x004A92A0) |
| st3 camera 33672 (civilian on the vehicle) | 0x0048C850 / 0x0048B1E0 take the players' point into the vehicle frame (not ported); 0x0048C310 applies the vehicle frame first | ported; 0x0048B1E0 does not reset the stack: its matrix is the 0x0048B140 top (view x vehicle ops) x vehicle ops again; riders' model-view chain starts from the vehicle ops applied on the view (`frameMv`) |
| game-over map frame 3719 / 45995, title 3854 | test artifact: no stage draw, so the F line (the context at the end of the frame, after the map setup reset it) was compared with the view built earlier in the frame | world_test / frontend_test compare the F line with `World::ContextNow()` on such frames |

Also: 24-bit angle interpolation (0x00411700 `fmul; fiadd`), root-motion prevRoot (0x00410C50) in float steps.

## 3. Stage-3 boss in game type 3 (CODE)

From 0x004209B0 / 0x004231C0: in boss mode the heads skip the rock intro (LAB_00420F35): head 0 starts the name card
at flag 1, every head starts the fight at flag 2 (phase 1, state 4, 0x009CA0EA = 1, head 0 the health bar); the
central head's roars as in mode 0. Variant 5's death clears 0x009CA0EA; its removal at flag 4 runs the records
0x00425F40 (`BossRecords`, world_modes_boss.cpp). Mode selection (0x0041FC00: scene != 0xF -> mode 1) was already in.
Not exercised (no capture of boss-mode bosses 3 / 4); modes_boss0 unchanged.

## 4. Regressions (all logic checks exact, numbers only improved)

auto_st1 (draws 11875 / 12787, was 11809), auto_st2 prefix (all exact incl. camera and players' point; draws 11227,
was 11213), auto_st3 old fight (`--prefix`, to 46375: all exact incl. camera / enemy positions; draws 12432, was
12291), auto_st4 `--stage-start 44107` (draws 10802, was 10765), auto_st5 `--stage-start 56007` (camera exact, was
14669 / 14893; draws 13294, was 12845), auto_st6 `--stage-start 70806 --civilians 0,1` (lock-on / camera exact; draws
14672, was 13857), ending (credits exact), play1full (camera exact incl. 3719; draws 2228, was 2226), 2p (exact),
2p_continue (camera exact, was 5098), stage-3 demo (draws 807, was 805), frontend_test play1full (camera exact, was
576 / 577 and 516 / 517). Menu-mode captures (frontend_test, main's modes_*): rank / options exact; boss0 draws now
exact (was 7134 / 7137); train0 / train01 / orig camera now exact (were 1 frame off), draws +1 / +1 / +394.

## 5. Notes for later work

- The world-space chain stays the source of the draws (`n.world`); the original's draw matrices are the model-view
  chain (`n.mv`). Drawing from `n.mv` (view-space draws) would likely close many of the remaining draw-matrix
  differences, but every node callback takes a world matrix - not done.
- actor 0x31's view position keeps the old summation order (`mx::TransformWorldOrder`, world_st2_actor31.cpp): its body
  point is still world space (own chain), and that order fits the captures' draws better (st2 +54, st6 +94 frames).
- `mx::Inverse` (0x004A8D20) is still computed in double; the original is 24-bit float with its own order (used by the
  vehicle-frame target and the stage-2 chest test). No divergence traced to it.
- Debug switches (stderr): `HOTD2_ANIMDBG=<fn hex>` (skeleton state / nodes), `HOTD2_LOCKDBG=<fn hex>` (lock point /
  body view), `HOTD2_BODYDBG` (Boss1/2 mounts), `HOTD2_BOSS2DBG`, `HOTD2_CAMDBG` (view re-aim), `HOTD2_HYDRA_LOCK`,
  `HOTD2_HYDRA_NODES` (with `HOTD2_HYDRA_DEBUG=<head>`). world_test `--view-eps E` lists view differences from E
  (prints both matrices below 1e-3).

Symbols: `stage3c_symbols.csv`.
