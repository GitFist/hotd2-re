# Stage 1 after the opening area: scene 1 objects, level collision, civilians (Stage 3)

`game::World` (game mode 6) now runs `build\scene_capture_play2scene1.txt` (`--script build\script_p1_reload.txt`)
frame-exact from the Stage_Start frame 901 to the switch to game mode 7 at 4520: scene 1 (op 0x4F at 1767), the
corridor / kitchen scenes, the door, the civilians with their attackers, the P1 game over (4248) and the continue
countdown.

**CONFIRMED** = code read + frame-exact against the capture; **CODE** = read and ported, not exercised;
**HYPOTHESIS** marked.

New files: `src/game/world_collision.cpp` (level collision meshes, shots / floor / pushes against them and against
collision-mesh objects), `world_scene1.cpp` (komono kinds 7, 9, 38, 39, 44, 69, actors 0x24, 0x33 state 4, 0x52 rats,
type 0x44 state 0 door, breakable-prop items, civilian penalty mark, civilian child spawn), `world_npc.cpp` (actor 0x25
scripted characters), `world_enemy_beh.cpp` (enemy behaviours 13, 34, 35, 40, 45, splash ring, flying head).
Small additive edits: `world.cpp` (collision task, op 0x10/0x11 lists, shot level test, item drop, kind-4 rand site),
`world_internal.h`, `world_enemy.cpp` (floor query / push / water hook, behaviour dispatch, node callbacks 0x004835D0 /
0x00485260, decapitation), `world_actors.cpp` (komono 40 variant 0, `SpawnBloodPoolAt`), `world_civilian.cpp`
(children, shot penalty, killed penalty, part model requests). Test: `tests/world_test.cpp` (model-id alias report).

## 1. Results

`world_test <game> build\scene_capture_play2scene1.txt --script build\script_p1_reload.txt` (901..4520 = 3620 frames,
ends at game mode 7):

| Check | Frames matching |
|---|---|
| rand state Z / rand calls Y / call sites | 3620 / 3620 each |
| task list K | 3620 / 3620 |
| event ops E | 3620 / 3620 |
| camera view | 3619 / 3619 |
| live enemies / attackers | 3620 / 3620 each |
| enemy hp / behaviour / flags (T) / position | 2939 / 2939 each |
| lock-on slots | 3620 / 3620 |
| game mode / sub-state, letterbox / can-shoot | 3620 / 3620, 3619 / 3619 |
| rank, hit shake | 3620 / 3620 each |
| players (state, lives, score, ammo, shots, hits), aim, P1 gun record | 3620 / 3620, 3620 / 3620, 3619 / 3619 |
| draws (all tasks of the frame exact) | 3563 / 3567 rendered frames |
| stage section draws | 3566 / 3567 |

Draw matrices 605,260 ok, 107 bad. The 4 frames: 1211 (the known player-shadow frame of gameplay.md), 3533 / 3970
(one enemy node each, error 2.1e-3 / 2.3e-3: float rounding just above the 2e-3 tolerance), 4520 (the mode-7 switch
frame: rain / stage section). `scene_capture_play2full.txt` (older capture of the same run) gives the same numbers.
Mode-5 demo (`scene_capture_stage3.txt`, 805 / 814 draws) and play1 (`scene_capture_play1full.txt` +
`script_p1_start.txt`, 2225 / 2251 draws, everything else exact) are unchanged.

Before this work play2 diverged at 1767 (scene 1). Steps and the first divergence after each (all checks):
collision + scene-1 komono / actors -> 1864 (actor 0x25); + actor 0x25, rats, shots vs level -> 2259 (behaviour 13);
+ behaviour 13 -> 2851 (door); + door / object meshes -> 2940 (item); + items -> 3070 (decapitation); + flying head ->
3357 (civilian children); + children, behaviours 34 / 35 / 45 -> 4060 (civilian shot); + penalty, behaviour 40 ->
4210 (serial: komono 69); + komono 69 -> none.

**Capture artifact (model ids).** `scene_capture.cpp ModelId` caches pointer -> id on first sight; when a model is
loaded later into memory that an earlier model used, the capture logs the old id (e.g. model 3958 of actor 0x24 logged
as 672 from 1767). world_test now counts a draw with a different id but the same matrix as such an alias and lists the
pairs: in this run 3958 -> 672 (actor 0x24 node 1, 898 draws; pol 0xCA with 672 is released at 1208 and pol 0xB8 with
3958 loaded at 1212) and 5467 -> 47 (543 draws). The matrices of these draws are exact. Suggested hook fix: map the
pointer through the slot table at every draw (no cache), or clear the cache when a slot is loaded.

## 2. Level collision meshes (world_collision.cpp) - CONFIRMED

- Res_LoadCollision 0x0048A310 from 0x0048A3B0 (stage reset): coli0.bin at 0x0098F200, coli<stage+1>.bin at 0x00990A00
  (stage 1: coli1.bin). Raw. Mesh = s32 groups; group = s32 face count, box max xyz, box min xyz, faces of 18 words:
  plane (a, b, c, d), s32 dominant axis 0/1/2, 4 vertices, material word. Ops 0x10 / 0x11 carry -1-terminated mesh
  address lists (e.g. 0x00990A00, 0x00990B40, 0x00991E80 door); task 0x00406010 copies them to 0x009C7040 /
  0x009CA0C0.
- Segment test 0x004AAA40: one-sided (0x009CAC5C is never written): a hit needs start behind the plane (<= 0) and end in
  front (> 0); hit = (|d_end| * start + |d_start| * end) / (|d_start| + |d_end|); four edge tests on the dominant
  axis; nearest to the end wins; returns the material. Float op order follows the disassembly (24-bit FPU).
- Floor Coll_FloorHeight 0x00409D40(x, y, z): segment (x, y - 1000, z) -> (x, y, z) through 0x004053B0 (object meshes,
  then both level lists; candidates keyed ftol(|end - hit| * 10), stable 16-bit radix sort); y = 0x009CAC58 or the
  ground 0x009C8E58; material 0x009CAC40 (0x34 dirt, 5 / 0x37 water). Enemies stand on y 6.187 (plane 6.19 - 0.00023 z)
  where the ground value says 6.5.
- Sphere push 0x004057F0 (enemy post-root 0x00454900 with flags136C 0x20000000, actor 0x33 state 4): sphere test
  0x004AAFF0 against list 0x009C7040 and the object meshes; push = (r - sqrt(d^2)) along the normalized
  centre - nearest point (or the plane normal); reached from 1896 on, enemy positions stay exact.
- Shots: Hit_TestLevel 0x00404B80 (ray from the eye: camera matrix x Shot_BuildRay's view direction, 1000 units out
  back to the eye, both lists; candidates keyed ftol(-view z * 10) with the object hits). A level hit nearest to the
  eye blocks the shot and spawns the material's effect (Hit_RecordImpact 0x00405260 -> 0x00407340(point, angles of the
  normal, type = material, face mode 1 for 5 / 0x37)): the dust effect 0x34 at 1890.
- Collision-mesh objects (flags 0x10, mesh +0x14C, matrix +0x150 = node model-view x camera matrix at Hit_Register):
  shot test 0x00404A00 (segment through the inverse matrix, normal by the object's angles), floor 0x00405550,
  push part of 0x004057F0. Exercised by the door's shot tests (the floor / push parts are CODE).
- The enemy water hook 0x00456920 now sees real materials (water 5 / 0x37 -> splash 0x00408740: reported, not reached).

## 3. Objects (CONFIRMED unless marked; symbols in scene1_symbols.csv)

Scene 1 (op 0x4F, 1767): komono 9 (door frame + leaves, flag 0x1F), 7 (hanging lamp, swings when shot at 1770),
38 (9 bottles: fall, tumble, land on a corner), 39 (8 plate stacks; the 64 rands at 0x00463605 are its ctor, not
kind 40's), 40 variant 0 (8 bottles placed by T(16.473, 16.821, 4.749) RotY(0x278D) T(table)), 44 (7 pots,
prop skeleton 0x13); actor 0x33 state 4 (x2, static target models, not shootable until event flag P byte 12;
0x00432FF0 only sets the function, so it first runs the next frame); actor 0x24 (background character, skeleton 0x33);
two enemies on the collision floor. Later: actor 0x25 (1864, 3113: scripted characters, kind 0x3E talking / holding
model 0x7ED), rats 0x52 (1906, x4, Target_AddDirect -> lock-on slots 2/3 until the next table build), behaviour-13
enemies rising at a camera tick (2258), the door (type 0x44 state 0, 2851, flags 0x12 / 0x13), breakable props kind 4
with an item (2937: slot 2 -> item 0x004724A0 shot for 100 points at 2940 + score digits), decapitations (3070: neck
blood 0x00407120, flying head 0x0040A230 with blood pools), the civilian with a child enemy (3357: the enemy walks to /
grabs the civilian, behaviour 35 kills it at the list's attack tick (3415), both players -100, the enemy continues
with 45 -> 1 -> 2 -> 3), komono 69 (4066, removed on its first run in game type 0), a second civilian + child (3968),
the civilian shot by P1 (4060: Player_Penalty 0x00415430 = a life + 100, then 100 more, streak 0, a hit, penalty mark
0x0048E190), then behaviour 40 (walk past a point) and the P1 game over / continue (other agent's paths, exact).

Part lists 0x00412440: ids > 0x11 also queue their model (Res_RequestModel 0x0041D5D0, a load request in the same
queue as ops 0x50-0x57); without it the part model 4590 of actor 0x24 was not resident (applied to civilians too).

## 4. Not ported / open (reported through World::Notes())

- Enemy motion sounds 0x00452A10 (sound cues only; no check reads them), civilian voice 0x0048D140 (sounds only).
- Actor 0x25: commands 9, 10, 11 (camera path follow), 12 (head look 0x00485BA0), 16, 17, extras by P s16 6
  (0x00484FF0), kinds 0x3B / 0x3C / 0x3F props and motions 0x32A / 0x34C / 0x35D of the node callback.
- Actor 0x24 variants 1-5 and P s16 12 != 0 (0x00482F60), kinds 0x53 / 0x55 extras.
- Behaviour 35 jump motions 0xB2 / 0xB7 (+0x13CC target) and effect 0x0043FCA0; 0x0045C8D0 case 0x2A (+0x132C).
- Item slots 1 / 3 (0x00471BD0, 0x004722A0 creature), slot 8 extra draw 0x00418660; slot 6 / 7 height offsets are
  applied to a copy (the original adds and subtracts on the prop: float round trip not modelled).
- Komono 7 item 0x00466B40 and komono 69 / the rats' game-type-1 paths; rats variant >= 2.
- Water splash 0x00408740 (hook 0x00456920 condition ported); floor query exclusion of the current actor
  (0x009A26A0) in 0x004053B0.
- Collision-mesh object floor / push parts (CODE); the animated prop 0x00474120 (type 0x44 state 3) does not set its
  +0x150 / +0x14C yet (not in this run).

## 5. Captures that would help

- A play capture that rescues a civilian (shoots the child enemy before the attack tick) and one that reaches the
  stage-1 boss: civilian rescue bonus / scripts after a child's death, actor 0x25 head look / path follow.
- T dumps for 0x004842A0 (actor 0x25) and 0x00473B90 (door) to check fields directly (only draws checked now).
