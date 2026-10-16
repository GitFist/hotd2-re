# Gameplay: game mode 6 in the native world (Stage 3)

`game::World` now runs a game started from the title (game mode 6) from its Stage_Start frame to the switch to game
mode 7 (game over), driven by per-frame gun input, and is verified frame by frame against captures of the original
played with scripted input (`build\script_p1_*.txt` + `build\scene_capture_play{1,2}full.txt`).

**CONFIRMED** = code read + frame-exact replay against a capture; **CODE** = read in code, not exercised by the
captures; **HYPOTHESIS** marked.

Files: `src/game/world_player.cpp` (input, players, mode-6 sub-states, damage / score / shake / rank, player bodies),
`src/game/world_actors.cpp` (stage-1 intro actors, komono kinds 3/40/41, blood pool, hit spark), additions in
`world.cpp`, `world_enemy.cpp` (track-1 animation, hit reaction, node callbacks, sever), `world_enemy_ai.cpp`
(behaviour 18, Player_TakeHit), `enemy.{h,cpp}` (decapitation flag), `event_runner.{h,cpp}` (sub-mode setup and
camera command 0x12 bookkeeping, players placed by the gameplay camera start), `camera.cpp` (roll channel read only
when enabled: out-of-bounds read on object paths). Test: `tests/world_test.cpp --script`.

## 1. Results

`world_test <game> <capture> --script <script>`; inputs taken from the capture: the task serial counter and the seed
counter 0x009A32A0 at the stage start, the players' states before it (P1 0 = start pressed, P2 9).

### play1 (`script_p1_start.txt`, 901..3336 = 2436 frames, ends at game mode 7)

| Check | Frames matching |
|---|---|
| rand state Z / rand calls Y / call sites | 2436 / 2436 each |
| task list K | 2436 / 2436 |
| event ops E | 2436 / 2436 |
| camera view | 2435 / 2435 |
| live enemies / attackers | 2436 / 2436 each |
| enemy hp / behaviour / flags (T) / position | 1016 / 1016 each |
| lock-on slots | 2436 / 2436 |
| game mode / sub-state, letterbox / can-shoot | 2436 / 2436, 2435 / 2435 |
| rank, hit shake 0x009CA0E4 | 2436 / 2436 each |
| player state / lives / score / ammo / shots / hits | 2436 / 2436 |
| player aim, P1 gun record | 2436 / 2436, 2435 / 2435 |
| draws (all tasks of the frame exact) | 2225 / 2251 rendered frames |
| stage section draws | 2250 / 2251 |

185 frames have no draw line at all in the capture (the title card 1157..1336 suppresses 3D, plus resource loading
frames 902, 906..909): counted as "not rendered", draws not compared. Model residency is modelled (Mdl_Draw
0x00418560 draws only models whose slot is loaded, 0x009A66AC[id] & 0x8001; ops 0x50-0x53 through the request
queue, the stage start loads), which removed the sky's second model 6911 (the demo loads it, the game does not).
Draw matrices: 303k ok, ~400 bad. Remaining frames with a difference:
- actor 0x22 (0x0049B260) 22 frames 1338..1366, errors 0.003..0.006: its second object is placed from
  (node-1 world x view) x inverse(view) (0x004A8D20); float rounding of that path, HYPOTHESIS: the original keeps a
  different intermediate.
- 2430: the player task draws the body shadow 0x10D0 one frame too long. That frame the camera script runs sub-mode
  setup 1/3 (0x004039D0: rail 0x0040C380, body at the rail); the original has no shadow that frame. Same pattern in
  play2 at 1211. Cause not found (0x00411090 -> 0x0040A590 draws it unconditionally on the flags checked).
- 3233/3234 enemy (0.002, float), 3336 (mode-7 switch frame) rain / stage section.
Fixed in this round: blood 0x00407230 (an enemy node's hit sphere is refreshed from 0x004D032C when its model is
swapped, 0x004098E0 -> 0x004099A0), the body at 1337 (rail 0x0040C380 sets the players' pitch / yaw / roll
0x009C71EC..F4 from context 0 through 0x00401C50; 0x004159A0 and 0x0040C9C0 use all three angles).

### play2 (`script_p1_reload.txt`, every 7th pulse an off-screen reload; 901..5999)

rand, K, E, camera, enemies, player fields exact through **1766** (the intro skipped by a reload at 960, the title
card skipped at 1029, two enemies killed with head shots / limb hits, reloads). First divergence **1767**: the next
scene (scene 1, op 0x4F) spawns props and actors not ported yet (komono kinds behind 0x0046BCC0 / 0x0046C240 /
0x0046D850 / 0x00466930 / 0x00472830, actor 0x24 0x00482CE0 -> 0x00482EE0, 0x00433B70, ...) whose constructors use
rand (0x00463605 ...). Later checks are not meaningful after that point (they still run). At 1767 the event VM
spawns komono kinds 9, 7, 38, 39, 40 (variant 0 via 0x004636A0: 64 rands at 0x00463605 in one task), 44, actors
type 33 kind 16 (x2) and type 24; the capture's K grows from 36 to 62 tasks that frame. Draws: exact frames up to
1210 (1211 = the shadow case above).

Mode-5 demo (`scene_capture_stage3.txt`): unchanged, all checks exact as before (draws 805/814, the 9 known float
rounding cases).

## 2. Mode-6 start and frame (CONFIRMED)

- Frame N (title, mode 4): start pressed -> P1 state 0; Game_ApplyState 0x0040E860 switches to mode 6 at the end of
  the frame (Mem_Init: K empty; 0x009C90A4 = 0; player states kept for modes 6/7).
- Frame N+1 = `StartStage(gameMode 6)`: Game_UpdateInput runs first (gun records), then mode 6 sub-state 0
  (0x0045FEF0: event mode 0, scene 0, rank init 0x00460770 = 0x005679F4[difficulty], rank timer 0x009A2C30 = 1, sub
  1) -> Stage_Start 0x00460030 with `srand(0x009A32A0)` (Game_SeedRandom 0x004157D0; 0x009A32A0 counts frames since
  boot, incremented first thing in Game_Update) -> task list 0x00460710 without the demo timer; player tasks with
  0x00579CD0[state * 5]: P1 0 -> 0x00413DB0, P2 9 -> 0x004145C0; player bodies 0x00416450 (in 0x00414F20); then
  Stage_ResetState (0x0040D220 clears both gun records' aim). Sub-state 2.
- Every later frame (`Step`): 0x009A32A0++, 0x009A5C50++, resources, Game_UpdateInput (Â§3), mode 6 sub-state
  (0x005679A8): 2 = 0x004601D0 (if both players are "not playing", 0x00579CD4[state * 5] != 0: sub-state 3 when a
  continue is possible, 0x00406ED0), 3 = 0x004604E0 (countdown 0x009A2BB8 = 0x9FFF), 4 = 0x00460530 (countdown -0x2D
  per frame, trigger skips to the next second; at < 1: 0x009C8E88 = route, Game_SetState(7)); each runs 0x004606D0
  (tasks, Game_UpdateRank 0x004607B0, rank timer 0x009C8A7C++, hit timers 0x009C8E08[p]--).
- Op 0x4D reseeds with 0x009A32A0 every time in mode 6 (mode 5: only the first). 0x009C71C4 (demo auto-gun) = 0.
- The event VM reads route 0x009C8E84 / players 0x009C8E80 at op time (World passes the live values).
- GameRunning 0x007DCCA4 = a player in state 5 with lives, or a non-interactive mode (0x00413280); rail advance
  0x0040C790 = non-interactive || (hit shake timer 0x009C8E8C == 0 && players != 0); op 0x32 readiness 0x0045F900.

## 3. Input (CONFIRMED)

`World::SetInput(p, WorldInput)` = the slot state the hook / port input layer writes (aim in 640x480 pixels, fired,
off-screen, held / pressed buttons 2 reload, 4 trigger, 8 start); `MakeWorldInput` = the hook's ComputeGunSlot.
Inside the world: Input_LatchShots 0x0041EB30 (an off-screen shot sets the position to 0xFFFF; only the last slot
that fired is latched), Game_UpdateShots 0x0040CE70 (a latched slot updates only that player's shot input
0x009C8800), buttons 0x009C9034 / pressed 0x009C9028 (0x0040D6C0, P2 << 16), Gun_UpdateRecords 0x0040CBB0: gun
(+0x125 == 1) -> copy + mode-6 calibration (GunCalibrateMode6), off screen -> aim 0 / valid 0; pad (+0x125 == 0) ->
pad cursor 0x0040CFE0, valid 1 and, while 0x009A34DC is set (Stage_ResetState until the letterbox has opened once),
0x0040D220 clears **both** records' aim every frame (why P1's centred aim reads 0 during the intro while its raw
position moves). Script frames = the boot counter 0x009A5C40 read in Input_Poll = the capture's F number.

## 4. Players (CONFIRMED unless marked)

- Init rows 0x00579DE8 (0x18 bytes: flags, state, hit timer, +0x7C / +0x80 callback setters, task fn): 0x00414770.
  Row 0 (start pressed): lives 0x009A34C4 (3), score 0, players++ (0x10), route++ (0x20), hit timer 0x5A, ammo 6,
  state 5, fn 0x00413E90. Row 4 = the demo.
- Player_Task 0x00413E90: +0x80 callback (body placement), +0x7C callback (body draw 0x00415120 / damage overlay
  0x00415180), trigger (lives > 0), crosshair 0x004169C0, flashes / tracers 0x00416B00, in modes != 5/9: lives 0 ->
  players--, state 4 (fn 0x00414200 -> 0x00414280 continue countdown +0x68), else ammo HUD state 0x004177D0 (RELOAD
  prompt counter +0x20, empty click sound) and +0x08 = lives; overlay 0x00417300 (hurt voice with one rand 5 frames
  after an overlay starts: the 0x004173DE calls); 0x00414600 start check when non-interactive.
- The +0x7C / +0x80 callbacks are also set by the camera sub-mode setups (0x00576C14: 0x00403A00.. set 0x00415180 /
  0x00415980) and camera command 0x12 (0x00579E90[arg], e.g. 0x00415B60 intro walk). CameraDirector::SubmodeSetup now
  records the setup function and command 0x12 its argument; World applies them after the event and camera tasks.
- Player_Trigger 0x00414940: aim from the gun record; fired: off screen (valid 0) with ammo != 6 -> reload
  (0x00414B30, sound 0x3E16A9); valid with ammo -> ammo-- (empty flag at 0), pending shot, shots++ (unless 0x009A5C48),
  ray / tracer / gun sound; while the letterbox is closed and the scene is skippable, the reload button requests the
  skip (0x009A1A18).
- Player bodies (player +0x78, 0x00416450): skeletons 0x00579F50[p] (57), motion 0x004EC8A4[p], node callback
  0x00416570 (props of motions 0x32A / 0x356), drawn with a shadow by 0x00415120 while +0x12C bit 0 is set. Intro
  callbacks 0x00415B60 (walk motion 0x322, gun model on node 5) / 0x00415BD0 (placed on paths 0xFD / 0xFE beside the
  car, y 0) / 0x00415E00 (hold the last frame); gameplay 0x004159A0 (at the rail position + route offset
  0x00579E90). Rail 0x0040C9C0 (camera sub-mode 1/1) copies the body of the player in game into 0x009C71E0 (the
  point enemies turn to). The gameplay camera start 0x00402460 places the players 15 below context 0's camera.

## 5. Damage, score, shake, game over (CONFIRMED)

- Player_TakeHit 0x00415300(p, flag, kind) from the enemy attack hit 0x00456490 (flag 1, or 0 + despawn for
  flag-0x2000000 attackers) and the lunge 0x00457B50: only in state 1/4/5 (0x004153E0), not while 0x009C8E08[p] > 0,
  not in mode 5: lives - 1, rank adjustment -2 (0x009A3794, applied by the rank update), score -100 (clamped at 0),
  flag -> +0x70 = 1 (shake + overlay), hit timer 0x5A; the last life is only taken while the camera mode
  (0x009C6F08 / 0x009C6F0C) is 2.
- Player_HitShake 0x00415270 (in task 0x00414F40): 0x30-frame timer; 0x009CA0E4 = ftol(cos(n * 0x1800 * 2pi/65536)
  * n * 1.0) (24-bit products); the camera task of the next frame re-aims context 0 by it (BuildContextViews).
- Score: Player_AddScore 0x004156C0 (clamped at 0). Enemy shots (0x00409430 tail): kill +0x50; head (slot 2) +0x78 +
  streak (+0x22, += 10); other slots +10 unless deflected, streak reset; hits +0x26. Props: 0x004650F0 (+10, a hit;
  rand picks the shooter when both players hit). Actor 0x20: per node like enemies, kill +0x50.
- Continue (state 4 / mode-6 sub-states 3-4) and the switch to mode 7: Â§2. Free play assumed (0x009C8E70 == 1,
  WorldConfig::freePlay); a start press during the countdown is detected but the rejoin path is not verified.

## 6. Enemies and actors added (CONFIRMED in play1 unless marked)

- Track-1 (upper body) animation: 0x00411B70 (start, loops back over n frames), 0x00411B20 (hold), 0x00411BD0
  (return), 0x00411AC0 (lower-body blend while the upper body plays), Anim_TrackTick 0x004112E0, node tracks 0x00412200
  with partial skeletons 0x00409A00 / 0x00409A50, partial slot loads in 0x00411C20.
- Hit reaction 0x004543F0 -> 0x004544C0 (flinch motion 0x00592FC8[kind][variant][0x004C84A8[slot]] on track 1, flag
  0x40000000), hit effects 0x00454050 (blood task 0x00407230 on the node, 25 frames; spark for armour), arm variant
  0x00454270, upper-body hold 0x004547C0, behaviour 18 0x004575A0 (hold a motion until a camera tick), node draw
  callback 0x004534A0 (blinking face 0x1F09: 1..59 then the base model; mouths / props by model), sever 0x00409AB0
  (parts below the node removed), decapitation flag (rand & 3 == 0; blood / flying head 0x0040A230 not ported).
- Stage-1 intro actors: voice lines / subtitles 0x00435B80 -> task 0x00435AA0; skip watcher actor 0x63 (0x00435F20 /
  0x00435F40 / 0x00435FA0: ends the camera segment, sets skipping 0x009A2D74 and 0x009A2230); title card actor 0x60
  (0x004342E0: 0xB4 frames or reload, hides the letterbox 0x009A5900 | 0x20, sets event flag 0xF8); the car actor
  0x26 state 1 (0x0048E600, paths 0xFD-0xFF, removed by flag 0xE0); komono kind 3 (0x004659D0 -> 0x00465BC0, car model
  patch, no draws); actor 0x22 (0x0049B0D0: skeleton 0x45 + a second object 0x46 on node 1, behaviours 0 / 1, node
  callback 0x0049D980 with the mouth cycle and eye parts); actor 0x20 (0x00448ED0 -> 0x00449020, distant characters:
  one rand for the start tick, hands / feet drawn by their parent's callback 0x00449530, any hit kills, death
  0x00449380 / 0x00449430); komono kind 40 (0x004636A0 -> 0x0046C570: 40-piece shatter, 11 rands per piece when shot)
  and 41 (0x0046CC50: two shutters); hit spark 0x004666B0; blood pool 0x00407DA0 (0x00407E30 / 0x00408100 /
  0x00408220).
- Actor slots: Anim_InitEnemy registers one (0x00409270); actor 0x22's second object keeps its slot after the actor
  ends (as in the original).

## 7. HUD interface

`WorldHud` (world.h) per frame: letterbox state / counter, can-shoot, game mode / sub-state, game continue countdown
0x009A2BB8, rank, shake, skippable / skipping, the frame counter 0x009A32A0 (torch animation), and per player: state,
lives, lives shown (+0x08), score, ammo, empty, reload prompt counter (+0x20; prompt visible while value % 60 < 45,
"gun" text after 120), hits, continue countdown (+0x68, digit = >> 12), hit timer, input kind, crosshair on / screen
position (0x004169C0), gun record aim / valid. `WorldCamera::playerPos` = 0x009C71E0.

## 8. Not ported / open (reported through World::Notes())

- Model residency: ops 0x54 / 0x55, section loads and Res_Flush stalls are not modelled. HYPOTHESIS for frames
  without any draw: resource loading frames.
- Debug switches (env): WORLD_TRACE_TASKS, WORLD_TRACE_SPAWN, WORLD_TRACE_SETUP print to stderr.
  `world_test --verbose --draws <fn hex>` prints native vs captured matrices of mismatching draws.
- play2 from 1767: scene-1 props / actors (komono kinds via 0x00463xxx with rand, actor 0x24, 0x00433B70 ...), enemy
  kinds / behaviours of later scenes (13, 0x0045E770), civilians' shot penalty / rescue, the flying head task
  0x0040A230, enemy motion sounds 0x00452A10.
- Player join / continue with the start button (0x00414FC0 -> states 1 / 3 / 10), P2, special weapons (game type 1),
  AUTORELOAD 0x007C17AC, infinite ammo / lives cheats, pad cursor movement, device modes other than 6.
- Game over screen (mode 7, 0x00460960) and later modes.
- Title card / car-light renderer state (0x0040E140, 0x0040C2C0, model patch 0x004659D0), subtitles and other HUD
  sprites (HUD agent).
