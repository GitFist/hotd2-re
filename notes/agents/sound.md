# Sound subsystem (Stage 2 agent report)

Static analysis of Hod2.exe; no runtime run. CONFIRMED = read from code/data and, where marked, cross-checked against
the data files or the st1 demo event trace. HYPOTHESIS = interpretation not proven.

## Summary

* PC sound is a thin DirectSound layer: **21 channels**, each one DirectSound buffer per playing sound. No mixing
  code, **no volume / pan / frequency / 3D control** (CONFIRMED: no SetVolume/SetPan/SetFrequency/3D-buffer calls
  anywhere; the 3D listener is queried at init and the 3D-buffer path is never enabled by any caller), no CD audio
  (no WINMM/MCI imports), **no per-frame sound update** (finished channels are reclaimed lazily in `Snd_PlayFile`; BGM
  streams on its own thread).
* All game code and event scripts play sounds through **`Snd_PlayGameSound(id)` 0x0041CFD0** (551 call sites, 511
  with an immediate id) which maps the id to a file name and a channel, then calls `Snd_PlayFile` 0x004AC020.
* Files are plain RIFF PCM 16-bit WAVs under `game\sound\` (802 files: SE mono/stereo 11-44 kHz, BGM and voice
  22.05 kHz stereo). Paths are built relative to the current directory: `Sound\SE\`, `Sound\BGM\`, `Sound\Voice\`.
* All 802 files except 13 are referenced by the tables; the 36 `*_OFF` SE names in the table do **not exist** on PC
  (Dreamcast key-off samples). Playing an `_OFF` id stops every looped SE and then silently fails to open its file.

## Id format (Snd_PlayGameSound 0x0041CFD0) — CONFIRMED

| top nibble | meaning | lookup | channel |
|---|---|---|---|
| 0 | SE | exact match of the whole id in the SE table **0x005845F8** (324 × 0x34: u32 id, char file[0x30], ends with id 0xFFFF); not found = ignored | auto 0..14; 0x21A9/0x121A9 (START_COIN) → 17 |
| 1 | BGM | index `id & 0xFFF` into char* table **0x00580354** (41 entries, "_AR" tracks) or **0x005803F8** (20 entries) when `0x009C8E98 == 6 && 0x009CA08C == 0`; null = ignored; no bounds check | 15, streamed (3000 ms buffer), looped except 0x10000009 / 0x10000025 / 0x10000014 |
| 2 | voice | index `id & 0xFFF` into **0x00580448** (467 × 0x24: u16 group, s16 number (-1 = empty → ignored), char file[0x20]); no bounds check | 16, one-shot |
| 8 | stop | 0x80000000 stop+release BGM and current BGM (0x009C8FB8) = 0; 0x80000001 stop+release SE channels 0..14; 0x80000002 stop+release voice; other 0x8xxxxxxx = stop BGM | - |
| 0 special | 0 = nothing; 0x000100A0 = `Snd_Command(0,0x100A0)` + "SS_SND_ALL_SOUND_OFF" debug string — a **no-op** on PC | | |

* SE ids are `(sample << 16) | (bank << 8) | 0xA9` (HYPOTHESIS for the meaning: Dreamcast sound-driver command packing,
  low byte 0xA9 = "play SE", 0xA0 = system commands like 0x2A0/0x3A0/0x100A0). The PC only compares whole ids.
* Voice `group`: 2 COM, 3 ORG, 4..9 = ST1..ST6 (folder). `number` (e.g. 0x0178) is only tested against -1.
* Loop / key-off lists: **0x005887FC** (44 SE ids, -1 terminated) and the parallel list **0x005888B0**. Scan order per
  index i: `id == loop[i]` → play looped; `id == off[i]` → `Snd_StopLoopedSe` 0x004AC220 first. The off list holds
  exactly the `_OFF` ids. `Snd_StopLoopedSe` stops **all** remembered looped SE channels (max 6, 0x00810150), not only
  the matching one, and stops by channel number (a channel may have been reused meanwhile — original quirk).
* Every BGM play sets the current BGM 0x009C8FB8 = id (even when the file fails); the pause menu 0x0040E500 replays it
  from the start when leaving pause.
* BGM table B entries 2, 4, 7 are null; table B at index ≥ 20 would read into the voice table (never used).
  HYPOTHESIS: table A = "arcade arrange" tracks (`*_AR`), table B = alternate tracks for the game state 6 / mode flag
  0x009CA08C == 0 (0x009CA08C also selects the alternate stage tables in `Stage_SelectTables`).

## Channel layer (DirectSound wrapper 0x004A2F50..0x004A4BE0) — CONFIRMED unless noted

* `Snd_Init` 0x004ABF50 → `Snd_CreateDirectSound(hwnd, 21)` 0x004A2F50: `DirectSoundCreate`, `SetCooperativeLevel(hwnd,
  DSSCL_PRIORITY)`, primary buffer (`DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D`), QI `IID_IDirectSound3DListener`
  (0x00570FA0), primary `SetFormat` (44100 Hz, 2 ch, 16-bit per the caller's arguments), `Compact`. Channel array
  `malloc(21 * 0x44)` at 0x007DE5A8, count 0x007DE5BC, init flag 0x007DE5B8 bit 31. Error string "Not Initialized
  Sound System!!".
* Channel record (0x44): +0 IDirectSoundBuffer*, +4 IDirectSound3DBuffer* (only if flag 2), +8 IDirectSoundNotify*
  (streams), +0x0C file handle (streams), +0x10 / +0x14 events (half/end notify, stop), +0x18 CRITICAL_SECTION,
  +0x30 stream thread, +0x34 stream buffer bytes, +0x38 stream state, +0x3C data offset in file, +0x40 flags
  (0x80000000 open, 0x40000000 streamed, 0x20000000 3D, 0x08000000 loop, 0x00010000 stopped, 0x00020000 paused,
  bit 1 = 4-bit?, value 1 = 8-bit data (silence 0x80)).
* `Snd_OpenChannel(ch, path, flags, ms)` 0x004A3EF0: `CreateFileA` via `Snd_OpenFile` 0x004A6A00 ("%s"), RIFF walk
  ("RIFF" size skipped, "WAVE" ignored, "fmt " first 0x12 bytes, other chunks skipped, stops at "data"; no pad-byte
  handling). flags&1 = stream: buffer = avgBytesPerSec·ms/1000 rounded up to 64, `DSBCAPS_GETCURRENTPOSITION2 |
  GLOBALFOCUS | CTRLPOSITIONNOTIFY | CTRLVOLUME | CTRLPAN | CTRLFREQUENCY | LOCSOFTWARE`, notify at half, end-2 and
  stop, thread 0x004A4640 refills the idle half (double buffering), loops by seeking back to the data start, else pads
  silence and stops after it. Streams read to **end of file** (the trailing LIST chunk of most WAVs is played as a
  few samples). Otherwise static: buffer = data chunk size, `GLOBALFOCUS | CTRLVOLUME | CTRLPAN | CTRLFREQUENCY |
  STATIC`, whole data loaded, file closed. flags&2 = 3D (never used), 0x10/0x20/0x40 extra flag bits (never used).
  Only caller: `Snd_PlayFile` with flags 0 (SE, voice) or 1 + 3000 ms (channel 15).
* `Snd_PlayChannel(ch, loop)` 0x004A34B0 (restores lost buffers; a playing static buffer is stopped and rewound
  first), `Snd_PauseChannel` 0x004A3780 (Stop, keep position), `Snd_StopChannel` 0x004A39E0 (Stop + rewind; streams
  re-prime via 0x004A3B90), `Snd_ReleaseChannels(first, last, mode)` 0x004A32D0 (mode 0 = one, 1 = range, 2 = all;
  streams: signal thread, wait, `Snd_FreeStreamChannel` 0x004A33F0), `Snd_GetChannelStatus` 0x004A3280 (DSBSTATUS
  byte), `Snd_DuplicateChannel(src, dst)` 0x004A4BE0 (`DuplicateSoundBuffer`; dst must be empty, src static).
* `Snd_PlayFile(path, loop, channel, id)` 0x004AC020:
  1. release every SE channel 0..14 that is not playing and not cached in the 6-entry ring 0x008101C8
     ((channel, id) pairs, write index 0x008101FC; **zero-initialised, so channel 0 counts as cached until six plays
     have overwritten the ring**);
  2. channel -1: first channel 0..14 not playing and not cached, else fail (sound dropped); explicit channel must be < 21;
  3. id != -1 and not looped: if a ring entry has this id and `DuplicateSoundBuffer` from it succeeds → play the copy,
     push (source channel, id) again, done;
  4. auto channel: open (result ignored), push (channel, id), looped → remember in 0x00810150 (max 6);
     explicit channel: stop if playing, release, open (channel 15 streamed 3000 ms);
  5. `Snd_PlayChannel(ch, loop)`.
* `Snd_Command(_, cmd)` 0x004ABF80: 0x2A0 pause BGM; 0x3A0 resume BGM (always looped, also for play-once tracks);
  0x300A0 / 0x1100A0 stop + release SE channels 0..14 (the stop loop only touches channels that are *not* playing —
  inverted test — but the release stops everything anyway); 0x100A0 / 0x800A0 / 0x900A0 and anything else: no-op.
* `Snd_StopSlot(slot)` 0x00401000 (via 0x0041D4F0; slots 0x009C90A8 = 0 BGM, 0x009C90AC = 1 voice, set by
  0x0040AD70): 0 → stop+release 15, 1 → stop+release 16, other → stop all playing channels.
* `Snd_StopAll` 0x0041D350: current BGM = 0, stop BGM + voice, 7 × `Snd_Command(0x1100A0)`. Called on scene/mode
  changes (0x0041A5E0, 0x0041F9B0, 0x0041FB00, 0x00426800, 0x0045ED90, 0x00460960, 0x004869E0, 0x00495EB0, 0x00496200,
  0x004996C0).
* Focus (`Game_WndProcHook` 0x0049E780, WM_ACTIVATEAPP): activate → 0x3A0; deactivate → 0x2A0, stop voice,
  0x300A0. Pause menu 0x0040E500: entering → stop BGM + voice; while paused every frame 7 × 0x1100A0; leaving →
  `Snd_PlayGameSound(current BGM)` (restarts the track).
* Dreamcast leftovers that only print a debug string: 0x0041D3A0 "SS_SndLoadPack", 0x0041D3B0 "SS_SndFreePack",
  0x0041D3C0 "SS_EntrySndPack_COMMON", 0x0041D3D0 "SS_FreeSndPack_except_COMMON", 0x0041D440 "SS_EntryStageSE",
  0x0041D4A0 "SS_GetAudioVal" (so `Snd_ApplyAudioMode` 0x0041D4B0 always sends 0x800A0, ignored).
* 0x004A6A50 / 0x004A6AA0 / 0x004A6B00 / 0x004A6B80: a 2-slot file registry (0x007DF3D0, mutex event 0x007DF3DC);
  streams register their file handle; nothing reads it (HYPOTHESIS: HGL library leftover).

## Event script ops — CONFIRMED (handler table 0x005931D8)

| op | len | handler | meaning |
|---|---|---|---|
| 0x38 | 8 | 0x0045F700 | `Snd_PlayGameSound(arg)` |
| 0x39 | 16 | 0x0045F720 | `Snd_PlayRepeat(id, s16 count, s16 interval)` 0x00435EE0: plays now, then a task (0x00435E90) replays every `interval` frames, `count-1` more times, while 0x009A2D74 == 0 and 0x009A2230 == 0 |
| 0x3A | 8 | 0x0045F750 | play unless 0x009A2D74 != 0 |
| 0x5F | 20 | 0x0045F7C0 | `Snd_EntryBgm(a, b, id, d)` 0x0041D450: "SS_Event_ENTRYBGM_BG_core", stop BGM (0x80000000), play id; scripts use (0, -1, id, 0) |

Script scan (tests/sound_test.cpp): opcode word followed by a resolvable id. 326 ops / 107 distinct ids in 10
scripts (comevtbl has none); every file exists except `_OFF` ids. The 4 sound events of the st1 demo trace
(`E 2829/2880/2969 56 0097A42C/444/480 004616A9`, `E 3232 56 0097A52C 002116A9`) are all found.
0x28 / 0x29 are not sound ops (they compare 0x009A2224).

## Other sound-id tables in the exe (data scan) — addresses CONFIRMED, roles HYPOTHESIS

| table | content | user |
|---|---|---|
| 0x004EC8BC | gun SE per player (GUN5_22, GUN4_22) | player shot 0x00414940 / 0x00414B90 |
| 0x004EC9A0 | 9 DC_SE weapon SEs (SHOT_GUN, MCHN_GUN, GRENADE, MAGNUM, AIR_GUN, TOY_GUN, …) indexed by weapon | 0x00414B90 (Original-mode weapons) |
| 0x00577674..0x005776AC | random BLOOD / ZOMBIE hit SEs, 0x005A4EA8/AC | enemy hit sounds 0x0040A6F0 (10 enemy callers) |
| 0x00579FD8 | 11 DAMAGE/BLOOD SEs by kind | 0x00417440 (indexed by its 2nd argument) |
| 0x00588EA4 / 0x00588EC0 | boss SEs (STAGE3/STAGE6 BOSS3_x) | 0x004207D0 (choice by 0x009A1A08 == 2) |
| 0x00589DA4 | 16-byte dialogue records: voice id at +0x10, s16 at +6 (duration?) | 0x00435B80 (voice + caption task), index table 0x0058B6B8 [line][3 variants by 0x009C7000] |
| 0x00589828 | 0x14-byte attract-demo records, BGM id at +0xC (ST1, ST2, ST4) | 0x00426800 → `Snd_EntryBgm` |
| 0x0056B954 | 8-byte records with voice ids | civilian voices 0x0048D140 (+ inline ids 0x2000001A/12/0B/11/0D) |
| 0x00593DBC | 11 footstep-like SEs (GRASS1, DAMAGE1, DAMAGE4) | 0x0046B040 |
| 0x00569798 | (id, -1) pairs of every SE — sound test list | sound test menu 0x004873E0 |
| 0x005970C8 | 38 BGM ids — sound test list | 0x004873E0 |

## Music by stage / mode (from scripts and code)

Stage 1 `ST1_AR` (0x10000001), stage 2 `ST2_AR` (…00), stage 3 `ST3_AR` (…11), stage 4 `ST4_AR` (…10), stage 5
`ST5_AR` (…12, op 0x5F), stage 6 `ST6_AR` (…13); bosses `BOS_AR` (…05, stages 1-4 and the boss-entry op of 5/6),
stage 5 bosses `ST5_BOS1_AR` / `ST5_BOS2_AR` (…0A / …0E), stage 6 bosses `ST6_BOS1_AR` / `ST6_BOS2_AR` (…0B / …0F);
stage clear `CLR_AR` (…03, 0x00434100), endings `ENDL_AR` / `ENDS_AR` (…0C / …0D, 0x00434100), game over `OVR_AR`
(…09, once, 0x00460960), name entry `NAM_AR` (…06), adventure intro `ADV_AR` (…08, advevtbl), `HOD1_ADV` (…14, once,
adv2evtbl), boss mode `BOSS_MOD` (…24, 0x00425010), ranking `RANK_MOD` (…26, 0x0041A5E0), training `TRA_MOD` (…27,
0x00496DE0), item select `ITEM_SELECT` (…28, 0x004885B0), `CLR2` (…25, once, 0x00497640). Attract demo: ST1 / ST2 /
ST4 via 0x00589828. Table B (state 6 && 0x009CA08C == 0) swaps the `_AR` tracks for ST1..ST6 / BOSS / CLR / NAME /
ST5BOS1/2 / ST6BOS1/2 / ENDL / ENDS. Ids 0x10000015..0x10000023 (plain tracks) are only referenced by the sound test.

## Port

* `src/game/sound_table.{h,cpp}`: `SoundTable::Load(ExeImage)` reads the tables above; `ResolveSound(table, id, ctx)`
  = 0x0041CFD0 decoding (path, channel, loop, stop-looped-SE, current BGM); `ParseWavHeader` = 0x004A3EF0 chunk walk.
* `src/game/sound_driver.{h,cpp}`: `SoundDriver` = `Snd_PlayFile` / `Snd_StopLoopedSe` / `Snd_Command` /
  `Snd_StopSlot` / `Snd_StopAll` / focus handling on an abstract `SoundBackend` (DirectSound semantics).
* `src/port/audio/xaudio_backend.{h,cpp}`: XAudio2 backend (one source voice per channel, whole data chunk in memory,
  no trailing-LIST noise). `build\src\port\Release\snd_play.exe game <hex id>... [--ms N] [--state 6 --alt 0]`,
  `--list` prints the id table.
* `tests/sound_test.cpp`: `build\Release\sound_test.exe game [--dump]` — table sizes, all table files present except
  the 36 `_OFF` SEs, WAV headers, evt id resolution, the 4 traced st1 events, `SoundDriver` scenarios on a fake
  backend (ring reuse via duplication, loop/_OFF, 15-channel limit, BGM tables, focus, stop commands).

## Open questions / captures wanted

* Runtime check of `SoundDriver` against the original: hook `Snd_PlayFile` 0x004AC020 (log path, loop, channel, id,
  return) and `Snd_PlayGameSound` 0x0041CFD0 (id), plus `Snd_GetChannelStatus` per call, during the st1 demo and a
  short st1 play session; compare channel choice / duplication with `SoundDriver` fed the same ids.
* Meaning of game state 0x009C8E98 == 6 and 0x009CA08C (which BGM set the player hears in normal arcade play).
* 0x00589DA4 dialogue table semantics (+6 s16, +8.. fields) — belongs with the HUD/caption agent.

## Tables

Generated from Hod2.exe (`sound_test --dump` prints the complete id → file list: 324 SE, 38 BGM, 463 voices).

### SE banks (table 0x005845F8)

| bank (id bits 8-15) | folder | entries | sample range (bits 16+) |
|---|---|---|---|
| 0x15 | ETC | 3 | 0x0..0x2 |
| 0x16 | COMMON | 61 | 0x1..0x46 |
| 0x1D | DAMEGE_GA | 3 | 0x0..0x2 |
| 0x1E | DAMEGE_JMS | 2 | 0x0..0x1 |
| 0x34 | COMM3 | 4 | 0x0..0x3 |
| 0x00 | DC_SE | 27 | 0x0..0x1A |
| 0x17 | COMMON2 | 36 | 0x0..0x52 |
| 0x21 | START_COIN | 2 | 0x0..0x1 |
| 0x18 | STAGE1_SE | 28 | 0x0..0x36 |
| 0x19 | STAGE2_SE | 27 | 0x0..0x3C |
| 0x1A | STAGE3_SE | 31 | 0x0..0x2A |
| 0x1B | STAGE4_SE | 22 | 0x0..0x3A |
| 0x23 | STAGE5_SE | 32 | 0x0..0x35 |
| 0x25 | STAGE6_SE | 46 | 0x0..0x45 |

### BGM (tables 0x00580354 / 0x005803F8)

Users: event scripts (st1..st6, adv, adv2, trn, end; op 0x38 unless marked) and code (immediate argument).

| id | table A (default) | table B (state 6, 0x009CA08C == 0) | once? | used by |
|---|---|---|---|---|
| 10000000 | ST2_AR.WAV | ST2.WAV |  | st2 trn(op5F) |
| 10000001 | ST1_AR.WAV | ST1.WAV |  | st1 trn(op5F) |
| 10000003 | CLR_AR.WAV | CLR.WAV |  | code 00434100 |
| 10000005 | BOS_AR.WAV | BOSS.WAV |  | st1 st1(op5F) st2 st2(op5F) st3 st3(op5F) st4 st4(op5F) st5(op5F) st6(op5F) trn(op5F) |
| 10000006 | NAM_AR.WAV | NAME.WAV |  | trn(op5F) |
| 10000008 | ADV_AR.WAV | ADV_AR.WAV |  | adv |
| 10000009 | OVR_AR.WAV | OVR_AR.WAV | once | code 00460960 |
| 1000000A | ST5_BOS1_AR.WAV | ST5BOS1.WAV |  | st5 |
| 1000000B | ST6_BOS1_AR.WAV | ST6BOS1.WAV |  | st6 |
| 1000000C | ENDL_AR.WAV | ENDL.WAV |  | code 00434100 |
| 1000000D | ENDS_AR.WAV | ENDS.WAV |  | code 00434100 |
| 1000000E | ST5_BOS2_AR.WAV | ST5BOS2.WAV |  | st5(op5F) trn(op5F) |
| 1000000F | ST6_BOS2_AR.WAV | ST6BOS2.WAV |  | st6 st6(op5F) |
| 10000010 | ST4_AR.WAV | ST4.WAV |  | st4 st4(op5F) trn(op5F) |
| 10000011 | ST3_AR.WAV | ST3.WAV |  | st3 trn(op5F) |
| 10000012 | ST5_AR.WAV | ST5.WAV |  | st5(op5F) trn(op5F) |
| 10000013 | ST6_AR.WAV | ST6.WAV |  | st6 st6(op5F) trn(op5F) |
| 10000014 | HOD1_ADV.WAV |  | once | adv2 |
| 10000015 | BOSS.WAV |  |  |  |
| 10000016 | CLR.WAV |  |  |  |
| 10000017 | ENDL.WAV |  |  |  |
| 10000018 | ENDS.WAV |  |  |  |
| 10000019 | NAME.WAV |  |  |  |
| 1000001A | ST1.WAV |  |  |  |
| 1000001B | ST2.WAV |  |  |  |
| 1000001C | ST3.WAV |  |  |  |
| 1000001D | ST4.WAV |  |  |  |
| 1000001E | ST5.WAV |  |  |  |
| 1000001F | ST5BOS1.WAV |  |  |  |
| 10000020 | ST5BOS2.WAV |  |  |  |
| 10000021 | ST6.WAV |  |  |  |
| 10000022 | ST6BOS1.WAV |  |  |  |
| 10000023 | ST6BOS2.WAV |  |  |  |
| 10000024 | BOSS_MOD.WAV |  |  | trn(op5F) code 00425010 |
| 10000025 | CLR2.WAV |  | once | code 00497640 |
| 10000026 | RANK_MOD.WAV |  |  | code 0041A5E0 |
| 10000027 | TRA_MOD.WAV |  |  | code 00496DE0 |
| 10000028 | ITEM_SELECT.wav |  |  | code 004885B0 |

### Ids used by the event scripts

| id | file | event scripts |
|---|---|---|
| 000034A9 | sound\SE\COMM3\SPARK1_22.WAV | st3 |
| 000123A9 | sound\SE\STAGE5_SE\BRAKE_22.wav | st5 |
| 000134A9 | sound\SE\COMM3\SPARK1_22_OFF.WAV | st3 |
| 00021AA9 | sound\SE\STAGE3_SE\EUROPA_TELEPHONE_22.wav | st3 |
| 00031AA9 | sound\SE\STAGE3_SE\EUROPA_TELEPHONE_22_OFF.wav | st3 |
| 000419A9 | sound\SE\STAGE2_SE\BRAKE.wav | st2 |
| 000425A9 | sound\SE\STAGE6_SE\ELEVATE_16.wav | st6 |
| 000518A9 | sound\SE\STAGE1_SE\BRAKE.wav | st1(op3A) |
| 000519A9 | sound\SE\STAGE2_SE\BRIDGE_CRASH1_22.wav | st2 |
| 000525A9 | sound\SE\STAGE6_SE\ELEVATE_16_OFF.wav | st6 |
| 000618A9 | sound\SE\STAGE1_SE\CAR_CRASH1.wav | st1 |
| 000625A9 | sound\SE\STAGE6_SE\ELEVATE3_11.wav | st6 |
| 000716A9 | sound\SE\COMMON\BLOOD07_16.WAV | adv |
| 000718A9 | sound\SE\STAGE1_SE\CAR_FIRE.wav | st1 |
| 000719A9 | sound\SE\STAGE2_SE\CAR_SRIP_22.wav | st2 st2(op3A) |
| 000723A9 | sound\SE\STAGE5_SE\CAR_FIRE_22.wav | st5 |
| 000725A9 | sound\SE\STAGE6_SE\ELE_BELL1_22.wav | st6 |
| 000818A9 | sound\SE\STAGE1_SE\CAR_FIRE_OFF.wav | st1 |
| 000823A9 | sound\SE\STAGE5_SE\CAR_FIRE_22_OFF.wav | st5 |
| 000925A9 | sound\SE\STAGE6_SE\THUNDER1_16.wav | st6 |
| 000D17A9 | sound\SE\COMMON2\ZOMBIE_018_16.wav | end |
| 000E18A9 | sound\SE\STAGE1_SE\CHAIR1_44.wav | st1 |
| 000F19A9 | sound\SE\STAGE2_SE\GEAR1_44.wav | st2 |
| 001019A9 | sound\SE\STAGE2_SE\GEAR1_44_OFF.wav | st2 |
| 00111BA9 | sound\SE\STAGE4_SE\MECH5_44.wav | st4 |
| 00121BA9 | sound\SE\STAGE4_SE\MECH5_44_OFF.wav | st4 |
| 001325A9 | sound\SE\STAGE6_SE\BOSS6_EXP1_A_16.wav | st6 |
| 001423A9 | sound\SE\STAGE5_SE\GATE_22.wav | st5 |
| 001519A9 | sound\SE\STAGE2_SE\HELI1_44.wav | st2 |
| 001523A9 | sound\SE\STAGE5_SE\BRIDGE_CRASH1_22.wav | st5 |
| 001618A9 | sound\SE\STAGE1_SE\KAERU2_22K.wav | st1 |
| 001619A9 | sound\SE\STAGE2_SE\HELI1_44_OFF.wav | st2 |
| 001723A9 | sound\SE\STAGE5_SE\DRIVE_OFF_22.wav | st5(op3A) |
| 00181BA9 | sound\SE\STAGE4_SE\BEEP6_44.wav | st4(op3A) |
| 001823A9 | sound\SE\STAGE5_SE\ENGINE_22.wav | st5 |
| 00191AA9 | sound\SE\STAGE3_SE\BOAT_FAST2.wav | st3 |
| 001A00A9 | sound\SE\DC_SE\SHOSE_44.wav | adv2(op39) |
| 001A16A9 | sound\SE\COMMON\DAMAGE1_22.WAV | st2 |
| 001A1AA9 | sound\SE\STAGE3_SE\BOAT_FAST2_OFF.wav | st3 |
| 001C16A9 | sound\SE\COMMON\DAMAGE3_22.WAV | st2 |
| 001C19A9 | sound\SE\STAGE2_SE\MECH5_44.wav | st2 |
| 001D16A9 | sound\SE\COMMON\DAMAGE4_22.WAV | st2 |
| 001D19A9 | sound\SE\STAGE2_SE\MECH5_44_OFF.wav | st2 |
| 001F19A9 | sound\SE\STAGE2_SE\METAL_DOOR_HARD_OPEN.wav | st2 |
| 002016A9 | sound\SE\COMMON\DOOR4_16.WAV | st1 st2 st4 st5 st6(op3A) trn |
| 00201AA9 | sound\SE\STAGE3_SE\BOAT_TURN.wav | st3(op3A) |
| 002116A9 | sound\SE\COMMON\DOORKICK1_22.WAV | adv st1 st2 st4 st5 trn |
| 002118A9 | sound\SE\STAGE1_SE\RAIN3ST_44.wav | st1 |
| 002216A9 | sound\SE\COMMON\DOORKICK3_22K_1.WAV | st2 st2(op3A) st4(op3A) |
| 002218A9 | sound\SE\STAGE1_SE\RAIN3ST_44_OFF.wav | st1 |
| 002316A9 | sound\SE\COMMON\DOORKICK3_22K.WAV | st2(op3A) |
| 002325A9 | sound\SE\STAGE6_SE\SHATTER1_22.wav | st6 |
| 002425A9 | sound\SE\STAGE6_SE\SHATTER2_22.wav | st6 |
| 002516A9 | sound\SE\COMMON\ENE_WALK2_11.WAV | st4 |
| 002616A9 | sound\SE\COMMON\ENE_WALK3_16.WAV | st2(op39) |
| 002719A9 | sound\SE\STAGE2_SE\SIBUKI5_22.wav | st2 |
| 002725A9 | sound\SE\STAGE6_SE\EXPLO1_22.wav | st6 |
| 002817A9 | sound\SE\COMMON2\WALK_SND1_22.wav | st2(op39) |
| 002916A9 | sound\SE\COMMON\ENE_WALK6_22.WAV | st4 |
| 002B16A9 | sound\SE\COMMON\GRASS1_22.WAV | st2 |
| 002D1BA9 | sound\SE\STAGE4_SE\KEY_1.wav | st4 |
| 002E1BA9 | sound\SE\STAGE4_SE\KEY_THORW.wav | st4 |
| 002F16A9 | sound\SE\COMMON\GRASS7_16.WAV | st1 st5 st6 |
| 002F18A9 | sound\SE\STAGE1_SE\THUNDER21_22.wav | st1 |
| 003018A9 | sound\SE\STAGE1_SE\WARAI_22.wav | st1 |
| 003118A9 | sound\SE\STAGE1_SE\WOOD_DOOR_BROKEN.wav | st1 |
| 003119A9 | sound\SE\STAGE2_SE\WALK_HASI_44K.wav | st2(op39) |
| 003219A9 | sound\SE\STAGE2_SE\WOOD_DOOR_BROKEN.wav | st2 |
| 003416A9 | sound\SE\COMMON\GUN5_22.WAV | end |
| 003419A9 | sound\SE\STAGE2_SE\WOOD_DOOR_HARD_OPEN.wav | st2 |
| 003516A9 | sound\SE\COMMON\HERTBEAT_22.WAV | st1(op39) st3(op39) st4(op39) st5(op39) |
| 003517A9 | sound\SE\COMMON2\SHUTTER1_16.wav | st3 st5 |
| 003519A9 | sound\SE\STAGE2_SE\WOOD_FRAME_CRASH2.wav | st2 |
| 003925A9 | sound\SE\STAGE6_SE\EREVATOR_16.wav | st6 |
| 003A25A9 | sound\SE\STAGE6_SE\EREVATOR_16_OFF.wav | st6 |
| 003B25A9 | sound\SE\STAGE6_SE\EREVATOR_STOP_16.wav | st6 |
| 004116A9 | sound\SE\COMMON\SIBUKI2_16.WAV | st2 |
| 004516A9 | sound\SE\COMMON\SIBUKI7_16.WAV | st2 |
| 004517A9 | sound\SE\COMMON2\HABATAKI2_16.wav | st1 st1(op3A) st5 |
| 004525A9 | sound\SE\STAGE6_SE\AUTO_DOOR.wav | end st6 |
| 004616A9 | sound\SE\COMMON\SIBUKI8_16.WAV | st1 st2 |
| 004A17A9 | sound\SE\COMMON2\HABATAKI6_16.wav | st2 st3 st4 |
| 004D17A9 | sound\SE\COMMON2\CHAIN_SAW_22.wav | st3(op3A) st4 |
| 004E17A9 | sound\SE\COMMON2\CHAIN_SAW_22_OFF.wav | st3 st4 |
| 10000000 | sound\BGM\ST2_AR.WAV | st2 trn(op5F) |
| 10000001 | sound\BGM\ST1_AR.WAV | st1 trn(op5F) |
| 10000005 | sound\BGM\BOS_AR.WAV | st1 st1(op5F) st2 st2(op5F) st3 st3(op5F) st4 st4(op5F) st5(op5F) st6(op5F) trn(op5F) |
| 10000006 | sound\BGM\NAM_AR.WAV | trn(op5F) |
| 10000008 | sound\BGM\ADV_AR.WAV | adv |
| 1000000A | sound\BGM\ST5_BOS1_AR.WAV | st5 |
| 1000000B | sound\BGM\ST6_BOS1_AR.WAV | st6 |
| 1000000E | sound\BGM\ST5_BOS2_AR.WAV | st5(op5F) trn(op5F) |
| 1000000F | sound\BGM\ST6_BOS2_AR.WAV | st6 st6(op5F) |
| 10000010 | sound\BGM\ST4_AR.WAV | st4 st4(op5F) trn(op5F) |
| 10000011 | sound\BGM\ST3_AR.WAV | st3 trn(op5F) |
| 10000012 | sound\BGM\ST5_AR.WAV | st5(op5F) trn(op5F) |
| 10000013 | sound\BGM\ST6_AR.WAV | st6 st6(op5F) trn(op5F) |
| 10000014 | sound\BGM\HOD1_ADV.WAV | adv2 |
| 10000024 | sound\BGM\BOSS_MOD.WAV | trn(op5F) |
| 2000000B | sound\Voice\COM\190_Y_W.WAV | st2 st3(op3A) |
| 2000000D | sound\Voice\COM\200_C.WAV | st2 |
| 20000016 | sound\Voice\COM\214_YNG_M_B.WAV | st2 |
| 20000018 | sound\Voice\COM\214_Y_M_A.WAV | st1 |
| 20000029 | sound\Voice\COM\211_M.WAV | st3 |
| 2000002E | sound\Voice\COM\421_M.WAV | end |
| 2000009D | sound\Voice\ST2\67_YNG_M.WAV | st2 |
| 80000000 | stop BGM | st4 st5 st6 |

### Ids passed as immediates by game code (SE / voice / commands)

| id | file | calling functions (immediate argument) |
|---|---|---|
| 000000A9 | sound\SE\DC_SE\SELECT2_22.wav | 00425010 004869E0 00487320 004873E0 00496DE0 |
| 000015A9 | sound\SE\ETC\vo_title_16k.wav | 00495EB0 00496200 |
| 000017A9 | sound\SE\COMMON2\ZOMBIE_002_16.wav | 00477CD0 0049A190 0049A3F0 0049A600 |
| 000018A9 | sound\SE\STAGE1_SE\AXE_44K.wav | 0048FEB0 00490FD0 |
| 000019A9 | sound\SE\STAGE2_SE\BEEP6_44.wav | 00467E50 |
| 00001BA9 | sound\SE\STAGE4_SE\AXE_44K.wav | 00493F30 |
| 00001DA9 | sound\SE\DAMEGE_GA\188_2_GA.WAV | 00417300 00422D30 |
| 00001EA9 | sound\SE\DAMEGE_JMS\184_J.WAV | 00417300 0049AA60 |
| 000025A9 | sound\SE\STAGE6_SE\BOSS_BIT1_22.wav | 00426E60 00426FD0 |
| 000100A9 | sound\SE\DC_SE\CURSOR1_22.wav | 004885B0 |
| 000115A9 | sound\SE\ETC\vo_RELOAD_16.wav | 004177D0 0049AC10 |
| 000118A9 | sound\SE\STAGE1_SE\BOBBLE1_22.wav | 0043E3D0 00448C50 |
| 000119A9 | sound\SE\STAGE2_SE\BOBBLE1_22.wav | 0043E3D0 00448C50 |
| 00011DA9 | sound\SE\DAMEGE_GA\187_1_GA.WAV | 00417300 |
| 00011EA9 | sound\SE\DAMEGE_JMS\185_J.WAV | 00417300 |
| 000121A9 | sound\SE\START_COIN\START1_22.wav | 004043C0 00425010 004873E0 00496890 00496DE0 |
| 000125A9 | sound\SE\STAGE6_SE\BOSS_BIT1_22_OFF.wav | 00426FD0 00428400 |
| 000200A9 | sound\SE\DC_SE\ERROR1_22.wav | 00425010 00496DE0 |
| 000215A9 | sound\SE\ETC\vo_SHOOT_16.wav | 0049AC10 |
| 000216A9 | sound\SE\COMMON\BLOOD02_16.WAV | 00491B40 0049AF10 |
| 000218A9 | sound\SE\STAGE1_SE\BOS_WALK1_44.wav | 0048FEB0 00490FD0 |
| 000225A9 | sound\SE\STAGE6_SE\BOSS_BIT2_22.wav | 00428400 |
| 000300A9 | sound\SE\DC_SE\OK1_22.wav | 004885B0 |
| 000316A9 | sound\SE\COMMON\BLOOD03_16.WAV | 0042D820 0045CBD0 00475E40 00491B40 0049AA60 |
| 000325A9 | sound\SE\STAGE6_SE\BOSS_BIT2_22_OFF.wav | 00428400 |
| 000400A9 | sound\SE\DC_SE\REREASE_22.wav | 004885B0 |
| 000417A9 | sound\SE\COMMON2\ZOMBIE_007_16.wav | 00477CD0 0048FEB0 00490CE0 |
| 000418A9 | sound\SE\STAGE1_SE\BOS_WALK3_44.wav | 0048FEB0 |
| 000423A9 | sound\SE\STAGE5_SE\DRIVE_DEAD2_22.wav | 00433860 |
| 000500A9 | sound\SE\DC_SE\RET_OPTION_22.wav | 0041A5E0 00425010 00496DE0 00497440 0049F780 0049FA80 0049FDB0 0049FF40 |
| 000516A9 | sound\SE\COMMON\BLOOD05_16.WAV | 0040A6F0 0045CBD0 |
| 000523A9 | sound\SE\STAGE5_SE\DRIVE_DEAD2_22_OFF.wav | 00432590 |
| 000700A9 | sound\SE\DC_SE\UFO_44.wav | 0046FCC0 |
| 000716A9 | sound\SE\COMMON\BLOOD07_16.WAV | 00438C10 00439FA0 |
| 000719A9 | sound\SE\STAGE2_SE\CAR_SRIP_22.wav | 00452930 |
| 000723A9 | sound\SE\STAGE5_SE\CAR_FIRE_22.wav | 00432590 00434100 |
| 000800A9 | sound\SE\DC_SE\UFO_44_OFF.wav | 0046FCC0 |
| 000823A9 | sound\SE\STAGE5_SE\CAR_FIRE_22_OFF.wav | 00432590 00434100 |
| 000B16A9 | sound\SE\COMMON\BOMB1_11.WAV | 00407950 00420810 00422D30 00440210 004408A0 00441000 0046FCC0 004805C0 004952A0 |
| 000B23A9 | sound\SE\STAGE5_SE\KIAI_22.wav | 0047DD50 |
| 000C16A9 | sound\SE\COMMON\BOMB2_16.WAV | 00407950 00477CD0 |
| 000C23A9 | sound\SE\STAGE5_SE\KIAI_22_OFF.wav | 0047DD50 00480140 |
| 000D16A9 | sound\SE\COMMON\BONE01_22.WAV | 0045C080 |
| 000D17A9 | sound\SE\COMMON2\ZOMBIE_018_16.wav | 00477CD0 |
| 000D23A9 | sound\SE\STAGE5_SE\MAG_ATTACK_11K.wav | 0047D410 0047E250 |
| 000D25A9 | sound\SE\STAGE6_SE\BAKUHA_A_16.wav | 00429730 |
| 000E16A9 | sound\SE\COMMON\BULLET_MET1_16.WAV | 00407950 004668A0 00467E50 00468CA0 0046A360 0046B040 0046EEE3 0046FCC0 |
| 000E23A9 | sound\SE\STAGE5_SE\MAG_BAKU1_22K.wav | 00480810 |
| 000E25A9 | sound\SE\STAGE6_SE\BIT_KOUGEKI_L1_22.wav | 00429D50 |
| 000F00A9 | sound\SE\DC_SE\STAR_44.wav | 00425010 00496DE0 |
| 000F16A9 | sound\SE\COMMON\BULLET_MET2_16.WAV | 00449B20 00454050 00468CA0 004694A0 0046EEE3 0046FCC0 |
| 000F23A9 | sound\SE\STAGE5_SE\MAG_MOVE_22K.wav | 0047D120 0047D410 0047DD50 |
| 001000A9 | sound\SE\DC_SE\STAR2_44.wav | 00425010 00496DE0 |
| 001023A9 | sound\SE\STAGE5_SE\MAG_DMG1_22.wav | 00480140 |
| 001100A9 | sound\SE\DC_SE\MOV_RANK_22.wav | 0041B0D0 |
| 001116A9 | sound\SE\COMMON\BULLET_MET3_22.WAV | 0040A130 00429D50 0042B9D0 0042CBA0 0042D220 0042D820 0043D4F0 00444EE0 00449B20 00450050 00454050 00459D20 00467E50 0046EEE3 0046FCC0 00474120 |
| 001117A9 | sound\SE\COMMON2\ZOMBIE_022_16.wav | 0042CBA0 00475E40 |
| 001123A9 | sound\SE\STAGE5_SE\MAG_DMG2_22.wav | 0047CA50 |
| 001125A9 | sound\SE\STAGE6_SE\BIT_KOUGEKI_S_22.wav | 00429D50 |
| 001200A9 | sound\SE\DC_SE\MOV_RANK2_22.wav | 00481400 |
| 001216A9 | sound\SE\COMMON\BULLET_OTH1_16.WAV | 00407950 00420810 00422D30 00428DA0 |
| 001225A9 | sound\SE\STAGE6_SE\BIT_SYUGOU_22.wav | 00426E60 00426FD0 |
| 001316A9 | sound\SE\COMMON\BULLET_SND1_16.WAV | 00407950 00491420 |
| 001416A9 | sound\SE\COMMON\BULLET_WAT1_16.WAV | 00407950 0046B040 |
| 001417A9 | sound\SE\COMMON2\ZOMBIE_032_16.wav | 00477CD0 |
| 001425A9 | sound\SE\STAGE6_SE\BOSS6_IDO_1_22.wav | 00426E60 00426FD0 |
| 001516A9 | sound\SE\COMMON\BULLET_WOD1_16.WAV | 00407950 00449B20 0046B040 0046EEE3 |
| 00151BA9 | sound\SE\STAGE4_SE\BOS_WALK1_44.wav | 00492460 |
| 001523A9 | sound\SE\STAGE5_SE\BRIDGE_CRASH1_22.wav | 00433FE0 |
| 001616A9 | sound\SE\COMMON\CAN_CRASH1_16.WAV | 00467E50 |
| 001617A9 | sound\SE\COMMON2\ZOMBIE_035_16.wav | 00477CD0 |
| 001623A9 | sound\SE\STAGE5_SE\CAR_EXPLOSION5_22.wav | 00407950 |
| 001625A9 | sound\SE\STAGE6_SE\BOSS6_KIIN_22.wav | 00426FD0 00428DA0 |
| 001700A9 | sound\SE\DC_SE\TRUNK_16.wav | 004885B0 |
| 001716A9 | sound\SE\COMMON\CAN_CRASH2_16.WAV | 0046A360 |
| 001717A9 | sound\SE\COMMON2\ZOMBIE_036_16.wav | 00477CD0 |
| 001725A9 | sound\SE\STAGE6_SE\PISHI3_22.wav | 00429730 |
| 001816A9 | sound\SE\COMMON\CAN_ROLLING1_16.WAV | 00472A50 |
| 001825A9 | sound\SE\STAGE6_SE\BOSS_A_16.wav | 00429730 |
| 001916A9 | sound\SE\COMMON\CAN_ROLLING2_16.WAV | 00443540 00467E50 0046A360 0046AD20 |
| 001917A9 | sound\SE\COMMON2\ZOMBIE_041_16.wav | 00455720 |
| 001925A9 | sound\SE\STAGE6_SE\KIAI_LONG_A_16.wav | 00428400 |
| 001A16A9 | sound\SE\COMMON\DAMAGE1_22.WAV | 0045CBD0 00464500 00469F50 0046B040 0046FCC0 |
| 001A1BA9 | sound\SE\STAGE4_SE\QUAKE_22.wav | 004408A0 00441000 |
| 001B16A9 | sound\SE\COMMON\DAMAGE2_22.WAV | 0045CBD0 |
| 001B1BA9 | sound\SE\STAGE4_SE\QUAKE_22_OFF.wav | 004408A0 00441000 |
| 001B25A9 | sound\SE\STAGE6_SE\KIRITUKE1_22.wav | 00426FD0 |
| 001C16A9 | sound\SE\COMMON\DAMAGE3_22.WAV | 00457FB0 00458A70 0045CE70 00464500 00468CA0 0046B040 |
| 001C17A9 | sound\SE\COMMON2\ZOMBIE_046_16.wav | 00420810 0044C2E0 |
| 001C1BA9 | sound\SE\STAGE4_SE\QUAKE_END_22.wav | 004408A0 00441000 |
| 001C25A9 | sound\SE\STAGE6_SE\KIRITUKE1_22_OFF.wav | 00426FD0 |
| 001D16A9 | sound\SE\COMMON\DAMAGE4_22.WAV | 00442EB0 0045CBD0 00464500 00465950 00466B40 0046B040 0046FCC0 00477CD0 |
| 001D18A9 | sound\SE\STAGE1_SE\PDMG_MORR1_44.wav | 0042FC00 |
| 001D23A9 | sound\SE\STAGE5_SE\KIRITUKE2_22.wav | 00426FD0 |
| 001E16A9 | sound\SE\COMMON\DAMAGE5_22.WAV | 00448110 0047CE10 |
| 001E18A9 | sound\SE\STAGE1_SE\PDMG_MORR2_44.wav | 0043D4F0 |
| 001E1BA9 | sound\SE\STAGE4_SE\BOSS_WALL2_22.wav | 004408A0 00441000 |
| 001F16A9 | sound\SE\COMMON\DOOR1_16.WAV | 00472A50 |
| 001F23A9 | sound\SE\STAGE5_SE\LASER_SWORD_22.wav | 0044EC80 |
| 001F25A9 | sound\SE\STAGE6_SE\LASER_SWORD_22.wav | 00452FD0 |
| 002023A9 | sound\SE\STAGE5_SE\LASER_SWORD_22_OFF.wav | 0044EC80 |
| 002025A9 | sound\SE\STAGE6_SE\LASER_SWORD_22_OFF.wav | 00456600 |
| 002116A9 | sound\SE\COMMON\DOORKICK1_22.WAV | 00468CA0 0046EEE3 0046FCC0 00472A50 00474EA0 |
| 002119A9 | sound\SE\STAGE2_SE\PDMG_MORR2_44.wav | 0043A990 |
| 002216A9 | sound\SE\COMMON\DOORKICK3_22K_1.WAV | 0046A360 0048FEB0 |
| 002225A9 | sound\SE\STAGE6_SE\MONITOR3_22.wav | 00426FD0 00450CF0 004534A0 |
| 002316A9 | sound\SE\COMMON\DOORKICK3_22K.WAV | 004408A0 0048FEB0 |
| 00231BA9 | sound\SE\STAGE4_SE\BOSS4_HASHIRI1.wav | 00493890 004952A0 |
| 002416A9 | sound\SE\COMMON\ENE_WALK1_11.WAV | 0044B180 0044C170 |
| 00241AA9 | sound\SE\STAGE3_SE\BOSS3_1.wav | 00422D30 |
| 00241BA9 | sound\SE\STAGE4_SE\BOSS4_TAORE_A.wav | 004952A0 |
| 002516A9 | sound\SE\COMMON\ENE_WALK2_11.WAV | 0044EC80 0046A360 |
| 002616A9 | sound\SE\COMMON\ENE_WALK3_16.WAV | 00439450 |
| 002625A9 | sound\SE\STAGE6_SE\WARP2_22.wav | 00428400 |
| 002716A9 | sound\SE\COMMON\ENE_WALK4_16.WAV | 0044A360 00450CF0 |
| 00271BA9 | sound\SE\STAGE4_SE\BOSS4_YARARE3.wav | 00493F30 |
| 002725A9 | sound\SE\STAGE6_SE\EXPLO1_22.wav | 00428400 |
| 002818A9 | sound\SE\STAGE1_SE\SMALL_BOSS_13_22.wav | 0049D4B0 |
| 00281BA9 | sound\SE\STAGE4_SE\BOSS4_YARARE4.wav | 004952A0 |
| 002916A9 | sound\SE\COMMON\ENE_WALK6_22.WAV | 00424D90 0044E410 0044E4C0 0044EC80 0044FAF0 0045DED0 |
| 002A16A9 | sound\SE\COMMON\ENE_WALK7_22.WAV | 0042C8D0 00454B90 00457FB0 00458A70 00477CD0 0049B620 |
| 002A18A9 | sound\SE\STAGE1_SE\SMALL_BOSS_4_22.wav | 0049CDF0 0049D4B0 |
| 002B16A9 | sound\SE\COMMON\GRASS1_22.WAV | 0046B040 0048FEB0 |
| 002D16A9 | sound\SE\COMMON\GRASS4_22.WAV | 0046B040 |
| 002F16A9 | sound\SE\COMMON\GRASS7_16.WAV | 00433FE0 004699C0 |
| 002F1BA9 | sound\SE\STAGE4_SE\MONITOR3.wav | 004408A0 |
| 002F23A9 | sound\SE\STAGE5_SE\SMALL_BOSS_4_22.wav | 0049CDF0 |
| 003016A9 | sound\SE\COMMON\GUN1_22.WAV | 00486020 004873E0 |
| 003018A9 | sound\SE\STAGE1_SE\WARAI_22.wav | 0049B620 |
| 003416A9 | sound\SE\COMMON\GUN5_22.WAV | 0049AF10 0049FA80 0049FF40 |
| 003425A9 | sound\SE\STAGE6_SE\SMALL_BOSS_4_22.wav | 0042C1A0 |
| 003516A9 | sound\SE\COMMON\HERTBEAT_22.WAV | 00477CD0 |
| 003517A9 | sound\SE\COMMON2\SHUTTER1_16.wav | 00467E50 |
| 003616A9 | sound\SE\COMMON\ITEM_22.WAV | 00466B40 004674C0 0046B040 0046FCC0 00471BD0 |
| 00361BA9 | sound\SE\STAGE4_SE\WOOD_DOOR_HARD_OPEN.wav | 00472A50 |
| 003716A9 | sound\SE\COMMON\KAERU4_22.WAV | 0043A990 |
| 003719A9 | sound\SE\STAGE2_SE\WORM_TUBU2_44.wav | 0042FC00 |
| 00371BA9 | sound\SE\STAGE4_SE\TR_CRASH_22.wav | 0046FCC0 |
| 00381BA9 | sound\SE\STAGE4_SE\TR_MOVE_22.wav | 0046FCC0 |
| 00391BA9 | sound\SE\STAGE4_SE\KAIDAN_22.wav | 0046FCC0 |
| 003A16A9 | sound\SE\COMMON\MEET01_22.WAV | 0043E720 |
| 003A1BA9 | sound\SE\STAGE4_SE\KAIDAN_22_OFF.wav | 0046FCC0 |
| 003B16A9 | sound\SE\COMMON\MEET02_22.WAV | 0043E720 |
| 003B17A9 | sound\SE\COMMON2\MONEY_22.wav | 00471BD0 004723F0 |
| 003C16A9 | sound\SE\COMMON\SWORD11_22.WAV | 0047ED10 0048FEB0 |
| 003D16A9 | sound\SE\COMMON\SWORD11_22_1.WAV | 00422D30 |
| 003E16A9 | sound\SE\COMMON\RELOAD1_44.WAV | 0040E730 00414B30 00414E40 00482070 00496890 00499FB0 0049FF40 |
| 003E25A9 | sound\SE\STAGE6_SE\BOSS3_1.wav | 0042D220 |
| 003F25A9 | sound\SE\STAGE6_SE\BOSS3_2_22.wav | 0042CDD0 |
| 004116A9 | sound\SE\COMMON\SIBUKI2_16.WAV | 004219E0 00422D30 00439EA0 0043FCA0 00440210 00456C50 00456D10 |
| 004216A9 | sound\SE\COMMON\SIBUKI3_16.WAV | 00422D30 00439EA0 |
| 004217A9 | sound\SE\COMMON2\FUKUROU2_22.wav | 00445C00 |
| 004316A9 | sound\SE\COMMON\SIBUKI4_16.WAV | 00466B40 |
| 004317A9 | sound\SE\COMMON2\HABATAK1_16.wav | 0049B260 0049B620 |
| 004417A9 | sound\SE\COMMON2\HABATAK1_16_1.wav | 0042C1A0 |
| 004517A9 | sound\SE\COMMON2\HABATAKI2_16.wav | 0049B260 |
| 004616A9 | sound\SE\COMMON\SIBUKI8_16.WAV | 00408370 0042DFD0 00448110 00467E50 |
| 004817A9 | sound\SE\COMMON2\HABATAKI4_11.wav | 0047D410 |
| 004A17A9 | sound\SE\COMMON2\HABATAKI6_16.wav | 004464C0 |
| 004C17A9 | sound\SE\COMMON2\KOUMORI2_22.wav | 0042DFD0 |
| 004D17A9 | sound\SE\COMMON2\CHAIN_SAW_22.wav | 0042D530 00493870 |
| 004E17A9 | sound\SE\COMMON2\CHAIN_SAW_22_OFF.wav | 0042D530 00493890 |
| 004F17A9 | sound\SE\COMMON2\KNIFE1_44.wav | 004595F0 |
| 005017A9 | sound\SE\COMMON2\KNIFE1_44_OFF.wav | 004595F0 00459D20 |
| 005117A9 | sound\SE\COMMON2\KNIFE2_44.wav | 0044FAF0 |
| 005217A9 | sound\SE\COMMON2\KNIFE2_44_OFF.wav | 0044FAF0 00450050 |
| 20000012 | sound\Voice\COM\209_M.WAV | 00422D30 |
| 20000015 | sound\Voice\COM\214_OM_B1.WAV | 00420550 00468CA0 |
| 2000001F | sound\Voice\COM\67_YNG_M.WAV | 00468CA0 |
| 20000022 | sound\Voice\COM\65_Y_W.WAV | 00499FB0 |
| 2000003B | sound\Voice\ORG\FAILED.WAV | 00497C30 |
| 2000003C | sound\Voice\ORG\GO.WAV | 00497640 |
| 2000003D | sound\Voice\ORG\READY.WAV | 00497640 |
| 2000003E | sound\Voice\ORG\SUCCESS.WAV | 00497C30 |
| 2000003F | sound\Voice\ORG\TIMEUP.WAV | 00498C10 |
| 20000085 | sound\Voice\ST1\30_ZE.WAV | 0049B620 |
| 80000000 | stop BGM | 0041D450 00425010 00480140 004869E0 004885B0 00496DE0 00497440 00497640 |
| 80000001 | stop all SE | 004869E0 004873E0 |
| 80000002 | stop voice | 0045EDD0 004869E0 004873E0 |
