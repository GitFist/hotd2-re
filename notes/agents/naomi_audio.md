# Naomi Arcade audio

Arcade uses 17 original-driver music renders, with the original DTPK sequence
periods and one-shot flags. Original mode retains its PC music arrangement.
The 26 waveform-verified looping effects use original-driver renders too,
including their sample intros, sustain regions, pan and gain. Other effects and
voices keep their existing mappings: several native sound IDs differ from PC.

`tools/naomi_audio_source.py` verifies the USA cartridge and parses its AM2
sequence control lists. Sequence deltas advance every 44 AICA samples at 44,100
Hz. Loopback controls distinguish intros from repeating bodies; ending and
attract sequences terminate. All 13 repeating music cues were checked against
three captured passages each (within three samples); all 26 effect periods
matched at zero offset. The final bank preserves PCM16 stereo output losslessly.

`tools/render_naomi_audio.py` runs the cartridge's own Sound Test bank loaders
and ARM sound driver in official Flycast 2.7 with DSP enabled. It verifies the
emulator hash and changes only the SDL host-underrun fill to a sentinel in an
isolated executable. Removing those sentinel buffers preserves genuine rests.
It does not change AICA, sequence timing, instruments or synthesis. The emulator,
BIOS and cartridge are offline build inputs, not part of the client or audio bank.
This is an emulated Naomi reference, not a cabinet line-output recording.

`tools/import_naomi_audio.py` validates capture hashes and clock periods, chooses
matching phase boundaries, and retains the intro plus one steady-state cycle.
The intro splice and loop seam are checked separately. Playback repeats the
authored period, without repeating the musical opening or interrupting pause.

The bank lives at `game/naomi_audio.bank`. It contains a bounded 43-record index
and independent zlib-compressed canonical WAVs with verified Adler-32 checksums.
Recordings inflate on demand. Invalid or absent audio falls back to PC files.
Both SDL and XAudio2 support its frame-bounded loops; Extras previews play once.

Release 2.1 publishes the exact bank as
`hotd2re-naomi-audio-v1.bank` to the companion tag `updates-v2026.11.06-a`.
The pin is in `src/port/update/naomi_audio_content.h`: 235489234 bytes,
SHA-256 `bea90d1027d83f22773ac71aed04f76505e9ba9f5324276ae21086ebacb39647`.
It is intentionally outside the legacy program-file updater manifest. Updated
clients install it once with progress, SHA-256 verification, cancellation and
atomic installation. Complete packaging requires the same bank.

Validation: `test_naomi_audio_source.py`, `naomi_audio_test <game> <bank>`,
`typing_content_test --naomi-audio <bank>`, package tests, `sound_test`,
`extras_test`, `attraction_audio_test`, `typing_audio_test`, `strength_audio_test`
and `audio_loop_region_test`. The native-bank test opens every recording in both
audio backends and checks routing, transport, malformed data and PC fallback.
An isolated 900-frame Arcade run also captured the real SDL output: its first
three seconds of Chapter 1 music matched the original-driver reference exactly
(correlation 1.0, gain 1.0). The test used private save/settings paths and disabled
score submission.

Reference: [Flycast 2.7 SDL audio backend](https://github.com/flyinghead/flycast/blob/v2.7/core/audio/audiobackend_sdl2.cpp).
