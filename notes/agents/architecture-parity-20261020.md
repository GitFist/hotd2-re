# Current Windows x86 / x64 parity evidence

The 2026-10-20 source checkpoint was built for both architectures, then 19 executables
per architecture were copied into ignored build output before execution. Later builds
cannot overwrite the binaries used by these comparisons. No original asset, save,
capture or public service was modified.

Evidence is under `build/parity-current-source/` in this checkout:

- `pinned-binaries.json`: SHA-256 of the 19 pinned executables per architecture.
- `pinned/x86/` and `pinned/x64/`: exact executable snapshots used by the harness.
- `results/report.json`: all **53 paired cases passed** with byte-identical transcripts
  and identical native-save bytes wherever a test produced a save.
- `results/<case>/{x86,x64}/`: commands, full comparison transcripts and isolated saves.
- `netplay-progress.log`: actual loopback client lifecycle results for both architectures.
- `horde-proof/report.json`: additional live Horde replay binary/transcript/sidecar hashes.

The 53 cases include real six-chapter/ending captures, boot, reload, two players, pause,
Original mode, all nine boss captures, eight training lessons and three weapon variants.
The core cases exercise randomizer/recovery, stage results, final-chapter credits,
fresh boss completion guards, Horde records/attract isolation, all shipped bat variants,
texture/material loading and the real Home/rankings UDP services on loopback.

The capture transcripts deliberately retain their existing differences from the 2001
reference. Matching these transcripts between architectures proves current x86/x64
parity; it does not assert that every comparison against the original game is exact.

The actual client test uses Home Host/Browse, explicit Create/room pointer targets,
Arcade/Original/Scene/Boss selection, guest pause/resume and shared Return to Lobby.
Each architecture runs two real clients against a temporary local service. Both peers
and both architectures finish frame 4950 with World hash **`DC8EA345`**. The report records
the temporary output folders. The Night Shift fixture accounts for the current Home
and pause rows; use `--night-shift` when running the room script directly.

The additional `night_shift_replay_test` exercises actual Home -> Play -> Horde inputs,
a real second-player join, two completed rounds/store transitions, then input replay.
Each variant has 9008 live frames plus 9008 replayed frames. The complete transcript and
both actual Horde sidecar files are byte-identical between architectures:

| Variant | Final digest | Score | Completed rounds |
|---|---|---:|---:|
| 1P | `5EB95FE6C6098E0E` | 26670 | 2 |
| 2P | `433D03D45309AC94` | 24490 | 2 |

Repeat the suite with `tools/architecture_parity.py`, passing explicit current x86/x64
build folders, the read-only installed game directory, original fixture folder and an
ignored output directory. `--netplay` includes the local client lifecycle; `--netplay-only`
retains an existing capture report while rerunning that lifecycle. Tests never contact
the configured public lobby. Production NS32/NS64 compatibility IDs remain different;
this evidence does not enable cross-architecture production matchmaking.

Two subsequently reviewed leaderboard fixes (per-player joined history and explicit
new-completion submission tokens) are outside the pinned checkpoint above. Their final
current-source verification is under `build/leaderboard-review-current/`:

- `paired-report.json`: all **seven affected case pairs**, **three full visual-audit
  pairs** and **two actual Horde sidecar comparisons** passed byte for byte.
- `pinned/{x86,x64}/Release/`: the exact affected-test executable snapshots.
- `{x86,x64}/`: full current transcripts, commands, executable/transcript SHA-256 and
  isolated sidecars. Pause tests pass 355/355 on both architectures.
- `visual-{x86,x64}/`: actual asset/scene audits and hashes. Fish: 407 cases, 818646
  checks, 73 runtime/29 corpse poses; bats: 104 fixtures, 348 assets, 1362510 draws;
  layered smoke: 2168 submissions across 135 shipped animation frames. No failures.

The affected cases include actual Night Shift/Boss completion, live Horde/replay,
network-room/randomizer recovery, pause/settings/timing and a real local leaderboard
service. They verify dead joined players retain valid scores while unjoined stale slots
remain excluded; separate equal-valued runs remain distinct; accepted retries deduplicate;
uncertain sent initials stay frozen with a clear retry message; and unsent initials stay
editable after a service timeout. The nine service and fourteen lobby Python checks pass.

Current Horde rolling digests are `741A2A3B0E23A8EB` (1P) and `17AEA77E89A21CDD` (2P),
identical between architectures. These differ from the earlier checkpoint because that
test intentionally includes Horde participation metadata, now correctly seeded during
the intro. RNG, final scores, rounds and sidecar bytes remain the same. The fixes do not
change simulation input, renderer state or the production World hash format.
