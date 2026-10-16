# Resume point (2026-10-06)

Every agent branch is merged into main, and no agent work is in flight. The current state is in progress.md, under "State after the parallel rounds".

Before any Hod2 run, check that build\hod2.lock is gone: no agent holds it, so a leftover file is stale and can be deleted.

## Still open

- **Original mode:** done by orig5 (orig5.md): every scene captured and exact (stage 6 scenes 7 / 8 / 10 through poked route counters), two-player Original mode exact for stages 1-2, item characters ported, the stage-5 camera pitch fixed. Left: one stage-4 boss camera frame (2 ulps), unpoked stage-6 7 / 8 / 10 games, a two-player game over with an item character.
- **The user's physical tests:** two devices (two mice or guns), and light guns.
- **Save files:** the two the original changed during agent runs (pol\tv2.bin and tex\scr_tod_itm_itamidome2.bin no longer match the install). Restoring them is the user's decision.
- **Mobile / Android port:** not started. The SDL3 platform layer, SDL_GPU renderer and touch input already exist.
