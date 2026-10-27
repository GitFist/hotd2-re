// sdl_audio_test <game dir> <out.raw> [wav]: channel semantics of the SDL3 sound backend (src/port/audio/sdl_audio_backend.*)
// on SDL's disk audio driver (no sound card needed; the output, F32 stereo 44100, is written to out.raw for analysis).
// Checks IsPlaying across play / end / loop / pause / resume / stop / duplicate; prints the event times so the raw
// output can be checked against them (build/cmp_audio.py for the one-shot part).

#include <SDL3/SDL.h>

#include <cstdio>
#include <string>

#include "audio/sdl_audio_backend.h"

namespace {
int g_fail = 0;
void Check(bool ok, const char* what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_fail;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: sdl_audio_test <game dir> <out.raw> [wav, default Sound\\SE\\ETC\\vo_RELOAD_16.wav]\n");
        return 2;
    }
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "disk");
    SDL_SetHint(SDL_HINT_AUDIO_DISK_OUTPUT_FILE, argv[2]);
    const std::string wav = argc > 3 ? argv[3] : "Sound\\SE\\ETC\\vo_RELOAD_16.wav";  // 0.76 s
    auto a = port::CreateSdlAudio();
    if (!a->Init(argv[1])) {
        fprintf(stderr, "init failed: %s\n", SDL_GetError());
        return 1;
    }
    Check(a->Open(0, wav, false), "open");
    Check(!a->Open(0, wav, false), "open on a used channel fails");
    Check(!a->IsPlaying(0), "not playing before Play");
    Check(a->Play(0, false), "play one-shot");
    SDL_Delay(200);
    Check(a->IsPlaying(0), "playing at 0.2 s");
    SDL_Delay(1000);
    Check(!a->IsPlaying(0), "finished at 1.2 s (0.76 s sound)");
    Check(a->Play(0, false), "restart one-shot for resume-position check");
    SDL_Delay(400);
    a->Pause(0);
    Check(!a->IsPlaying(0), "one-shot paused before its end");
    SDL_Delay(300);
    Check(a->Play(0, false), "resume paused one-shot");
    SDL_Delay(500);
    Check(!a->IsPlaying(0), "resume finishes remaining audio instead of replaying the 0.76 s recording");
    Check(a->Play(0, true), "play looped (restart)");
    SDL_Delay(2000);
    Check(a->IsPlaying(0), "loop still playing at 2.0 s");
    a->Pause(0);
    Check(!a->IsPlaying(0), "paused");
    SDL_Delay(300);
    Check(a->Play(0, true), "resume");
    Check(a->IsPlaying(0), "playing after resume");
    SDL_Delay(500);
    a->Stop(0);
    Check(!a->IsPlaying(0), "stopped");
    Check(a->Duplicate(0, 1), "duplicate into channel 1");
    Check(!a->Duplicate(0, 1), "duplicate into a used channel fails");
    Check(a->Play(1, false), "play the duplicate");
    SDL_Delay(200);
    Check(a->IsPlaying(1), "duplicate playing");
    a->Release(1);
    a->Release(0);
    Check(!a->Play(0, false), "play after release fails");
    a.reset();
    printf("%s\n", g_fail ? "FAILED" : "sdl_audio_test: all passed");
    return g_fail ? 1 : 0;
}
