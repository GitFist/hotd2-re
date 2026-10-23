# Horde mode (port only)

`src/game/world_horde.cpp`. play.exe / hotd2.exe only: it needs `WorldConfig::portFeatures` and the modern mode select
(`portModernMenu`); nothing of it exists in the original and it is off in the exactness tests (the classic menu has no
entry, `Impl::horde` stays null, every hook tests it).

## Flow

1. Mode select entry 8 "Horde Mode" (world_front.cpp `TaskMenu`, Exit is 9) -> `HordeMenuConfirm`: the Arcade entry's
   confirm (credits, game type 0) plus `Front::hordeStart`. `GameStartFrame` starts an arcade game at stage 1 scene 0 (the
   street of the opening) and `HordeGameStart` creates the state (`Impl::horde`) and the mode's task (last in the list).
2. The opening cutscene is skipped as soon as the script allows (`skipRequest`, as a reload press would: the skip watcher
   0x00435F20 ends the camera segment). When the gameplay camera starts (camera mode 2, letterbox open) the mode takes
   over (`TakeOver`): the event VM is halted (`EventRunner::halted`), the rail stops advancing (`Impl::hordeHoldRail`,
   `RailAdvance`), the scripted zombies get the despawn record type, the players' point (0x009C71E0) and the camera stay.
   The camera's forward (ctx 0 yaw) and right give the street's axes.
3. Rounds: `RoundSeconds` / `RoundCap` / `RoundSpawnInterval` / `Weight` / `RoundHp` by the round number; the spawner
   keeps the alive count (+ the pools) below the cap while the time runs; the round ends when the time is out and the
   mode's zombies and bats are all dead ("CLEAR THE STREET"). Then the store (30 s, or every player ready), then the
   next round. Every 10th round is the boss round. Game over is the engine's (lives, continues, mode 7).
4. Money per player: `HordeEnemyKilled` from Enemy_ApplyShot's score tail (world_enemy.cpp; `KillMoney`), half of a
   kill to everyone for an agent's kill, `RoundBonus` at the round's end, `BossKillBonus` for Zeal (the store section).

## Barrels

`kLanes` (5) barrels (model 0x19E8, cracked 0x19E6 below half, komono_2.bin) `kBarrelDistance` (30) in front of the
players' point, `kLaneSpacing` (6.5: a barricade, the barrels touch) apart, on the floor (`FloorHeight`) but drawn
`kBarrelSink` lower: right in front of the players, only the rims and upper staves in view along the screen's bottom;
drawn with the route barrels' shake; a broken one spawns the 15 pieces (`SpawnBarrelPiecesFrom`, world_routes.cpp)
from its last model-view. No health bars: the barrels show their state (cracked, gone). The camera stays on the rail: with
`hordeHoldRail` the host's `LockOnSlots` reports no targets, so the shooting camera never turns towards a zombie.

## Zombies

Spawned like `World::Impl::Spawn` for an enemy record (`SpawnZombie`: EnemyTask, lock target, synthetic SpawnRecord of
type `kHordeRecordType` with params kind / variant / behaviour / the `kSpawns` index; +0x1328 keeps the walk pace),
`kSpawnNear..kSpawnFar` down the street, facing the players. `kSpawns` (kind, variant, hp, barrel damage, pace, type)
is the original's own kinds / variants (`HOTD2_ENEMYLOG=1` over `build\script_auto_full.txt` prints every
Enemy_Create's kind / variant / behaviour / hp); `Weight(i, round)` phases them in and out:

| round | spawns |
|---|---|
| 1 | basic znsam2 7/0 only |
| 2 | + the axe thrower znonoo 0x13/7 |
| 3..4 | more throwers; bats from round 3 (`BatCount` / `BatInterval`, a group once the last is gone) |
| 5 | + a few brutes znnick 5/0 (320 hp) |
| 6..8 | the basic phases out for the shirt znsam2b 8/0 (170 hp); 7: the chainsaw znchain 2/2 (fast, rare); 8: the brute phases out for harold 0/0 (480 hp) |
| 9 | + the speedy znkage 0xB/0 (pace 420) and the swamp znebi2 0xF/0 from pools |
| 10, 20, .. | the boss round (below) |
| 11+ | + the giant sword znken 0xE/1; 12: znebi3 0x10/6 pools, znele 0x12/0, znonoopa 0x14/2; 13: zngold 4/2 |

Hp +8 % a round (`RoundHp`; Enemy_Create's 0x0040A8B0 caps the record hp at 300), the pace rises with the round
(`RoundPace`, to 1.6x: `WalkPace` adds ticks or holds the counter with +0x34 bit 14) and `RoundChargeChance` percent
start with the charge motion (+0x34 bit 27). Behaviour `kHordeBehaviour` (90, dispatched by `EnemyBehaviour`), by the
spawn's type:

- walker: sub 0 the nearest intact barrel (none: behaviour 2, the engine's approach / attack of the players); sub 1
  the set's walk-in motion 2 (3 once hit, as 0x00457220) - the motion sets' entry 0 is the shuffle in place of
  behaviour 2, entries 2 / 3 carry root travel; sub 2 / 3 / 4 the attack entry of 0x00592DC0 / 0x00592F18 as behaviour
  3 (approach motion until within the entry's range, the attack motion, the hit tick damages the barrel by the spawn's
  damage + round / 2); sub 5 back off to the attack's start (set entry 4), then sub 1; the row is solid: a zombie is
  kept beyond its barrel along the forward (the lunges end at the barrel);
- thrower (0x13/7, as behaviour 33): sub 10 walks to 112 units from the players, 11 stands (set entry 0) and after a
  pause throws an axe at a random player in the game (the hand's attack entry: its motion, 0x0045A240 at its tick),
  11 again while a hand still holds one (`HoldsAxe`: node 5 model 0x1ECE / node 8 0x1ECA), then a walker; the thrown
  axes are the original's (world_thrown.cpp: shoot them down) but fly at a standing barrel (`HordeThrowAim`: the one
  nearest the target player's side, spread by the hand) and take its hp on arrival (`HordeThrowStrike`, 14 + round /
  2; a player's life only with no barrel left);
- swamp (0xF, 0x10): no walk-in; `SpawnOne` puts a pool of its blood (0x00408370) 68..108 units from the players and
  `PoolsFrame` spawns the zombie there 50 frames later with +0x34 bit 8 (not shot); sub 20 / 21 the rise 0xB8 with
  behaviour 13's splashes, shot from the splash tick on, then a walker.

`HordeShotDamage` multiplies the player's damage by the bought weapon's factor (the item rows: the grenade's 4.0) and
the power ups. Bats (the leeches of scene 3's cellar window): the stage-1 bats (world_st1b_bats.cpp, `SpawnBatGroup`
with the mode's count) spawned on the street 115 units down (group 0's splines circle a unit over the record's point;
higher they fly through the air); their dive ends at a standing barrel (`HordeBatDive`: the offset from the camera) and the bite takes
the barrel's hp (`HordeBatStrike`, 8 + round / 2; the players' life only with no barrel left). A round ends only with
every bat shot (`BatsAlive` counts TaskBat 0x0043C440; 0x007DCBD0 keeps 1 after the last of a group).

## The boss round (every `kBossEvery` = 10th round)

No walkers. `StartBoss` spawns the stage-1 boss pair as the mode's own enemies (generic EnemyTasks of kinds 0x44 / 0x45
with +0x34 bit 8: Enemy_ApplyShot and the hit reactions of the kind tables never run on them) and the boss bar
(0x00435E50 at y 62). The boss skeletons face with 0x0049DE60's convention (Cam_DirToAngles of self - target).

- Zeal (0x45, `ZealBehaviour`; its wings 0x46 drawn by the mode's task at its node 1 with 0x00570C00's motions, as
  0x0049D770): hp `BossHp` (700 + 150 a boss round, set after Enemy_Create's cap), hit spheres x3 (the original's
  0.7..1.5 are for its close fight). Sub 1 hovers `kHoverDistance` (78) down the street `kHoverHeight` up, sweeping
  sideways; sub 2 a `kChargeFrames` zigzag charge (the taunt 0x40E) at a random standing barrel; sub 3 the attack
  0x413, its middle tick hits the barrel (45 + round / 2; none standing: the players, kind 4); sub 5 back to the hover
  point; sub 4 staggered (0x408 / 0x409) by three hits during a charge; its hits are read from +0x190 each frame
  (Enemy_CollectShots clears the +0x34 bits even with bit 8): 30 damage (25 with two players) x the weapon factor
  capped at 2, node blood, 120 / 10 points, 5 money; at 0 hp sub 6: the stagger motion, the fall, `BossKillBonus`
  (2500 + 1000 a boss round) to every player, `bossOver` (the timer ends, the knight walks off), gone after 150 frames;
  sub 7 flies off after the knight's blow.
- Kuarl (0x44, `KnightBehaviour`; node 2 hidden as 0x0048FD90): spawns 220 units away, walks 0x393 towards the row so
  slowly that it arrives as the timer ends: the walk's root travel of the last frame is scaled back and the animation
  rate held to 0.25..1 (`WalkPace`) from the measured travel a frame (+0x1370). At the goal or the timer's end: attack
  row 0 of 0x005703E0 (0x383); at its hit tick every barrel breaks and every player in the game loses the last life
  (lives = 1, Player_TakeHit kind 4); `bossOver`.
- Bats: a group whenever none are left (`BossFrame`): 3 + 5 x the health lost, no more once Zeal is dead.

The round ends when the boss is over, the pair gone and every bat shot. Enemy_Despawn / Enemy_Destroy end the
*current* task (`KillCurrent` is noreturn): an enemy is only ever despawned from its own behaviour.

## Store

`StoreFrame` / `StoreDraw`, phase 2: a black page (the street off through 0x009A5900 bit 5 as under the chapter cards,
and `Impl::hordeBlackout`: no sky, no rain, `Draw` takes only the store task's own draws, since every 2D sprite is drawn
over the whole 3D scene and a sprite backdrop would hide the models). A 4 x 4 grid of framed cells (`GridLine`: the
boss bar's sprites 0xB5 / 0xB6 stretched; the pointed cell in red; 152 x 90 from (16, 44), above the HUD's torches),
one row each: the weapons, their upgrades, the base, the agents + READY. Each cell: its picture (the upper 44 px), name,
price and state: the glyph font (0x0055E054), the item select's name sprites (0x5F9 SHOTGUN, 0x5FA MACHINE GUN, 0x5FB
GRENADE, 0x617 FIRST AID KIT), the original mode's item models of etc_1.bin drawn in view space `kModelDepth` off
(`CellModel`: the agent's pistol 0x1591, shotgun 0x109E, machine gun 0x109B, grenade 0x1087, the power ups 0x10A5.. and
the chamber 0x107E.. as the upgrades' pictures, lamp 0x109A, first aid kit 0x1097, the barrel), the agents as their own
bodies from the torso up (`MakeBody`: the player skeletons 0x3B Amy, 0x3C Harry, 0x3D Goldman with the pelvis / leg
nodes 9..15 without models, motion 0x32C, facing the camera). A gun player's aim (`WorldInput::x / y`) picks the cell,
the trigger (0x009C9028 bit 2) buys, start (bit 3) or the READY cell ends the player's shopping. Items (`Item`, prices
`ItemPrice`, states `ItemStatus`):

| cell | effect |
|---|---|
| PISTOL / SHOTGUN / MACHINE GUN / GRENADE | the original mode's weapons 0..3 (`Equip`: `ModesState::PlayerItems` weapon / item row / damage, 0x00414E40 reload; `HordeSpecialWeapon` routes the trigger to 0x00414B90), owned for good once bought (`kWeaponPrice` 4500 / 6000 / 8000); the pistol cell equips it. The grenade launcher holds `GrenadeCapacity` rounds (4 + 2 a level; `kGrenadeRounds` 4 a purchase, `kGrenadeRefill` 2500); at none the pistol comes back |
| PISTOL / SHOTGUN / MACHINE GUN / GRENADE LV2..LV4 | the weapon's upgrade levels (`level[p][weapon]`, `kUpgradePrice`): the damage factor on the row's (`kDamageLevel`: the bought weapons start at 0.6 / 0.6 / 0.5 of their rows and reach 1.25 / 1.2 / 1.0, the pistol 1.0 .. 1.5; `WeaponFactor` -> `HordeShotDamage`) and rounds a magazine (`kMagazineAdd`; the launcher's capacity) |
| FIRST AID KIT | the standing barrels back to full (800) |
| NEW BARREL | one destroyed barrel back (the lane nearest the middle first); shown "N DOWN" (1000) |
| REINFORCE | +40 barrel hp (3 levels, 1500 / 2500 / 3500) |
| LIFE UP | +1 life (to `kMaxLives` 5, the HUD's torches; 2000) |
| AGENT AMY / HARRY / GOLDMAN | a CPU agent (`kAgentDef`: interval / accuracy / damage, off screen; 2500 / 6000 / 10000: Amy about round 3) |
| READY | done |

Income: `KillMoney` 20 + 3 x round + hp / 40 (+15 a head shot), `RoundBonus` 60 + 30 x round, half a kill to everyone
for an agent's, `BossKillBonus`: the whole store takes about twenty rounds. Zeal takes 30 (25 with two players) x the
weapon factor capped at 2 a hit over `BossHp` 3000 (+1000 a boss round).

## The weapon row

`WeaponRow` (the fight): the owned weapons as small fixed-pose icons (the agent's pistol 0x1591 and the item models,
drawn in view space `kRowDepth` off with a dimmed light, no spin) in framed slots above the ammo (P1 bottom left, P2
bottom right, `RowX`), the equipped one framed in red, the launcher with its rounds. A player's shot whose screen point
(`Player::aim`, centred, y up) lands on an icon equips it (the shot still fires). A grenade shot spends a round.

## Agents

`AgentsFrame`: an owned agent stands beside the players, off screen; every `interval` frames it shoots the zombie
nearest the players (`AgentHit`: hp, a blood effect, the kill as Enemy_ProcessHits ends one: 0x4000000, the death
behaviour 6, the voice), with the muzzle flash models of 0x00416B00 in view space at the screen's edge (`flashX /
flashY`) and the gun sound.

## Checks

- `HOTD2_PLAY_MENU=1 play.exe game --script build\horde\script_horde.txt --screenshot out_%d.png --every 120 --every-from 1200 --frame 5400`
  (the modern menu's row 8 by the aim, the take-over at ~1195, round 1); `HOTD2_HORDE_DEBUG=1` prints the take-over, the
  spawns, barrel hits, purchases and the rounds to stderr.
- `HOTD2_HORDE_DEBUG=1 HOTD2_HORDE_STORE=1` opens the store 120 frames after the take-over with 20000 per player:
  `build\horde\script_horde_store.txt` buys the shotgun, its upgrade, Amy and the grenade launcher and presses READY.
- `HOTD2_HORDE_DEBUG=1 HOTD2_HORDE_ROUND=N` starts at round N (10: the boss round), `HOTD2_HORDE_SECONDS=S` shortens
  the rounds (the knight's blow at S seconds). `build\horde\script_horde_zeal.txt` fires at the screen centre with
  reloads: Zeal dies at ~2431 (the bonus, the knight walks off). The scratch `hordetest.ps1 -Round N -Out <dir>` runs
  these with screenshots and a sheet. (The shell keeps HOTD2_* variables between tool calls here: clear them.)
- `build\horde\script_horde_2p.txt`: player 2 joins with start during round 1; both shoot.
- Online: the connected room (world_modes_net.cpp `RoomFrame`) has a HORDE MODE row (4; LEAVE ROOM is 5); the host's
  choice starts the same Horde game on both machines in lockstep. Loopback: the scratch `lobbyflow_test.py horde 4500`
  (host `build\script_lobby_p2p_host_horde.txt`: the aim at row 2 + the two replayed downs pick row 4 once the
  connection restarts the frame counter at 901; joiner `build\script_lobby_p2p_join.txt`): "in sync" to the joiner's
  end, no DESYNC. (play.cpp's rejoin hand-over waits 10 s of silence, as the console connections: a stage start with
  the asset pack or a big internal resolution can keep a machine silent for seconds.)
- Exactness: frontend_test / pause_menu_test unchanged (the entry only exists with the modern menu).
