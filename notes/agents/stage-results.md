# Chapter results (build K, 2026-10-08)

WorldConfig::portStageResults enables the port's results after each completed chapter in Arcade,
Original, and Scene Selection, and after a completed Boss Mode route. Training retains its own
results. The normal client and network games enable it; original-exactness tests default to off.

The page uses the original options-screen background tiles and menu glyphs, with red P1 and blue
P2 headings. Existing asset overrides apply without another graphics pack. It shows per-stage
score, shots, accuracy, successful ordinary-enemy head hits, rescue credit, accepted hits taken,
and continues; the team summary shows score and unique civilians rescued. Shared rescues credit
both players, but count only once in the team rescued number. Inactive players show NA. The menu
font lacks a percent sign, so accuracy uses PCT. A zero-shot run displays zero.

StageResults lives in World::Impl and is copied into WorldHud. AddScore, PlayerInit, PlayerTakeHit,
the continue callback, EnemyHits, and both civilian-rescue paths supply the counters. Native shots
and hits are captured when the page opens; counters reset at StartStage and game-mode changes.
WorldHash includes every result field, including the timer and dismissal state, for desync and
recovery checks. The new code neither writes original assets nor changes source save formats.

The totals page pauses the native rescue-screen task in supported port game modes. Dismissing
the totals resumes the original 420-frame rescue scene, with its civilian cast, camera animation,
capped life bonus and event flag FE. The life bonus is awarded once by the native rescue task.
StageEnd provides the fallback for final-chapter/boss routes without the rescue-screen actor.
A shown flag prevents a second page at the same boundary. While active, stage tasks stop, inputs
and network frames continue, and the results counter advances. After 180 frames a fresh trigger
or Start/Enter edge from either player dismisses the page; the next frame resumes the original
event/stage transition. Early presses are discarded and holding a button cannot queue a skip.
The windowed client also gates newly captured confirmation for three wall-clock seconds after
the first completed results render, so texture loading cannot consume the local minimum.
Replay applies recorded inputs without consulting local render time. Headless tests use the
deterministic 180-frame minimum. Escape does not pause this page; Enter confirms it.

Rendering is a complete 2D pass independent of the preceding stage's texture residency/camera,
as with the multiplayer lobby. No new images are generated. HOTD2_STAGE_RESULTS=1 enables this
page in captures while preserving the old menu layout for existing gameplay recordings.

Validation:

- stage_results_test: all six stage-end boundaries, ending vs next chapter, Boss Mode fallback,
  Training exclusion, early/held input rejection, P1 trigger and P2 Enter, a real recorded stage-1
  clear at frame 14232, counters, no repeated reward while waiting, next chapter, and recovery replay.
- Actual clients over loopback: host's recorded stage clear at frame 13331, guest confirmation
  at 13602, next chapter and equal final hashes 975686F4 at frame 14000. P2 is inactive in this
  fixture; it specifically checks that either peer can dismiss the page. See netplay_results_smoke.py.
- Actual clients through the deployed VPS relay: all four lobby modes, pause/resume, returns
  to lobby, repeat Arcade and matching final hashes (netplay_room_smoke.py).
- Pause checks 59/59; original comparison through frame 3000: 250207 draw matrices match.
- Captured and visually inspected the countdown and ready states with original game artwork.

Both players need build K (2026100803). No server update is required. The network tests above
ran two clients on this PC, not a two-household session. Packages: build/dist-k/.

Build S (2026101601) restores the rescued-civilian scene after the totals. Stage-results tests
cover all five rescue-scene chapters in Arcade and Original, with full and empty rescue lists,
the single life award, all six chapter boundaries, and recorded chapter-1 rendering and replay.
Two local network clients pass through totals, rescue animation and the next chapter in sync.
Both players need S; the lobby server and optional graphics pack are unchanged.
