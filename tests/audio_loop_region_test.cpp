// Verify bounded looping in actual SDL output, including the intro and transport behavior.
// The generated fixture and disk-driver recording are removed when the test finishes.
#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "audio/loop_region.h"
#include "audio/sdl_audio_backend.h"

namespace {
constexpr uint32_t kRate = 44100;
constexpr uint32_t kBegin = 9542 * 2;
constexpr uint32_t kEnd = 13162 * 2;
constexpr uint32_t kFrames = 30000;
int failures = 0;

bool Check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
    return ok;
}

struct Scratch {
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("hotd2-loop-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path wav = directory / "loop.wav";
    std::filesystem::path raw = directory / "output.raw";
    ~Scratch() {
        std::error_code ignored;
        std::filesystem::remove(raw, ignored);
        std::filesystem::remove(wav, ignored);
        std::filesystem::remove(directory, ignored);
    }
};

void Write16(std::ostream& out, uint16_t value) {
    const char bytes[] = {static_cast<char>(value), static_cast<char>(value >> 8)};
    out.write(bytes, sizeof(bytes));
}

void Write32(std::ostream& out, uint32_t value) {
    Write16(out, static_cast<uint16_t>(value));
    Write16(out, static_cast<uint16_t>(value >> 16));
}

bool WriteFixture(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    out.write("RIFF", 4);
    Write32(out, 36 + kFrames * 2);
    out.write("WAVEfmt ", 8);
    Write32(out, 16);
    Write16(out, 1);  // PCM16, mono, 44.1 kHz
    Write16(out, 1);
    Write32(out, kRate);
    Write32(out, kRate * 2);
    Write16(out, 2);
    Write16(out, 16);
    out.write("data", 4);
    Write32(out, kFrames * 2);
    for (uint32_t frame = 0; frame < kFrames; ++frame) {
        // +0.25 identifies the intro; two negative levels identify the loop and
        // its period; +0.75 is an outro that bounded playback must never reach.
        const int16_t sample = frame < kBegin ? 8192 :
            frame < (kBegin + kEnd) / 2 ? -8192 : frame < kEnd ? -4096 : 24576;
        Write16(out, static_cast<uint16_t>(sample));
    }
    return bool(out);
}

enum class Signal { Silence, Intro, Tail, Other };
struct Run { Signal signal; size_t frames; };

Signal Classify(float value) {
    if (std::fabs(value) < 0.01f) return Signal::Silence;
    if (std::fabs(value - 0.25f) < 0.03f) return Signal::Intro;
    if (std::fabs(value + 0.25f) < 0.03f || std::fabs(value + 0.125f) < 0.03f) return Signal::Tail;
    return Signal::Other;
}

void CheckRecording(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!Check(bool(input), "disk output exists")) return;
    const auto size = input.tellg();
    if (!Check(size > 0 && size % (sizeof(float) * 2) == 0, "F32 stereo output contains whole frames")) return;
    std::vector<float> samples(static_cast<size_t>(size) / sizeof(float));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(samples.data()), static_cast<std::streamsize>(size));
    if (!Check(bool(input), "read complete disk output")) return;

    std::vector<Run> runs;
    bool stereoMatches = true;
    bool validAmplitude = true;
    bool correctPeriod = true;
    size_t previousWrap = 0;
    size_t measuredPeriods = 0;
    for (size_t i = 0; i < samples.size(); i += 2) {
        stereoMatches &= std::fabs(samples[i] - samples[i + 1]) < 0.0001f;
        // A full-file loop would reach the +0.75 outro, well above every legal sample.
        validAmplitude &= std::isfinite(samples[i]) && std::fabs(samples[i]) < 0.4f;
        const Signal signal = Classify(samples[i]);
        if (signal != Signal::Tail) previousWrap = 0;
        else if (i >= 2 && Classify(samples[i - 2]) == Signal::Tail &&
                 samples[i - 2] > -0.18f && samples[i] < -0.18f) {
            const size_t frame = i / 2;
            if (previousWrap) {
                const size_t period = frame - previousWrap;
                correctPeriod &= period >= kEnd - kBegin - 64 && period <= kEnd - kBegin + 64;
                ++measuredPeriods;
            }
            previousWrap = frame;
        }
        if (!runs.empty() && runs.back().signal == signal) ++runs.back().frames;
        else runs.push_back({signal, 1});
    }
    Check(stereoMatches, "mono fixture reaches both output channels equally");
    Check(validAmplitude, "bounded loop never plays the outro beyond its end");
    Check(correctPeriod && measuredPeriods >= 3, "output repeats the complete 7240-frame resampled loop");

    // Ignore sub-10 ms conversion boundaries. Device buffering may shift wall-clock
    // timestamps, but cannot change this sequence or the recorded intro's duration.
    std::vector<Run> sustained;
    for (const Run& run : runs) {
        if (run.frames < kRate / 100) continue;
        if (sustained.empty() && run.signal == Signal::Silence) continue;
        if (!sustained.empty() && sustained.back().signal == run.signal) sustained.back().frames += run.frames;
        else sustained.push_back(run);
    }
    const Signal expected[] = {Signal::Intro, Signal::Tail, Signal::Silence,
        Signal::Tail, Signal::Silence, Signal::Intro, Signal::Tail, Signal::Silence};
    bool correctSequence = sustained.size() == std::size(expected);
    for (size_t i = 0; i < sustained.size() && i < std::size(expected); ++i)
        correctSequence &= sustained[i].signal == expected[i];
    Check(correctSequence, "intro once; pause/resume preserves tail; stop/restart repeats intro");
    if (!correctSequence) {
        std::printf("  output runs (0=silence, 1=intro, 2=tail, 3=other):");
        for (const Run& run : sustained) std::printf(" %d:%zu", int(run.signal), run.frames);
        std::printf("\n");
    }
    if (correctSequence) {
        for (size_t index : {size_t(0), size_t(5)})
            Check(sustained[index].frames >= kBegin - 256 && sustained[index].frames <= kBegin + 256,
                  "intro ends at the rate-scaled Dreamcast loop start");
        Check(sustained[1].frames > 8000 && sustained[3].frames > 8000 && sustained[6].frames > 8000,
              "tail repeats beyond a single loop before and after transport changes");
        Check(sustained[2].frames > 2000 && sustained[4].frames > 2000 && sustained[7].frames > 2000,
              "paused and stopped channels produce silence");
    }
}
}  // namespace

int main() {
    port::PcmLoopRegion region;
    Check(port::ResolveLoopRegion(9542, 13162, 22050, 22050, 13162, region) &&
          region.begin == 9542 && region.end == 13162 && region.Active(), "exact Dreamcast frame bounds");
    Check(port::ResolveLoopRegion(9542, 13162, 22050, kRate, kFrames, region) &&
          region.begin == kBegin && region.end == kEnd, "bounds scale to a resampled asset");
    Check(!port::ResolveLoopRegion(9542, 9542, 22050, kRate, kFrames, region) &&
          !port::ResolveLoopRegion(13162, 9542, 22050, kRate, kFrames, region), "empty/reversed bounds rejected");
    Check(!port::ResolveLoopRegion(9542, 13162, 0, kRate, kFrames, region) &&
          !port::ResolveLoopRegion(9542, 13162, 22050, 0, kFrames, region), "zero rates rejected");
    Check(!port::ResolveLoopRegion(9542, 13162, 22050, kRate, kEnd - 1, region), "truncated loop rejected");

    Scratch scratch;
    if (!Check(std::filesystem::create_directory(scratch.directory), "create temporary fixture directory") ||
        !Check(WriteFixture(scratch.wav), "write distinctive PCM fixture")) return 1;
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "disk");
    SDL_SetHint(SDL_HINT_AUDIO_DISK_OUTPUT_FILE, scratch.raw.string().c_str());
    auto audio = port::CreateSdlAudio();
    if (!Check(audio->Init(scratch.directory.string()), "initialize SDL disk output")) return 1;
    if (!Check(audio->Open(0, "loop.wav", false), "open generated fixture")) return 1;
    Check(!audio->SetLoopRegion(0, 9542, kFrames, 22050), "backend rejects loop beyond recording");
    if (!Check(audio->SetLoopRegion(0, 9542, 13162, 22050), "configure Dreamcast loop points")) return 1;
    Check(audio->Play(0, true), "play bounded loop");
    SDL_Delay(800);
    Check(audio->IsPlaying(0), "bounded loop remains playing beyond full recording duration");
    Check(!audio->SetLoopRegion(0, 0, 100, kRate), "cannot change region while queued");
    audio->Pause(0);
    Check(!audio->IsPlaying(0), "paused channel reports stopped");
    SDL_Delay(150);
    Check(audio->Play(0, true), "resume bounded loop");
    SDL_Delay(350);
    audio->Stop(0);
    Check(!audio->IsPlaying(0), "stop ends bounded loop");
    SDL_Delay(150);
    Check(audio->Play(0, true), "restart bounded loop");
    SDL_Delay(800);
    audio->Stop(0);
    SDL_Delay(150);
    audio.reset();  // Flush and close the raw output before reading it.
    CheckRecording(scratch.raw);
    std::printf("audio_loop_region_test: %s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
