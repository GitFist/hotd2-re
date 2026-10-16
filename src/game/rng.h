#pragma once

// HOTD2 random-number stream (see notes/agents/effects.md, "RNG stream").
//
// The game uses the statically linked MSVC6 multithreaded CRT rand()/srand():
//   rand  0x004ADED2 (every game call site goes through the jmp thunk 0x004ABE60)
//   srand 0x004ADEC5 (thunk 0x004ABE70, only caller Game_SeedRandom 0x004157D0)
// State = the per-thread _holdrand (per-thread data +0x14, _getptd 0x004B02A1); it starts at 1.
// Every consumer runs on the game thread, so one stream reproduces the whole game.

#include <cstdint>

namespace game {

// The one CRT stream type of the port (enemy.h and shooting.h use it too). `state` is public so captured states
// (Z lines) can be loaded directly.
class CrtRand {
public:
    static constexpr uint32_t kMul = 214013u;
    static constexpr uint32_t kAdd = 2531011u;

    // Optional per-call observer (game::World logs every call with its call site, like the capture's Y lines).
    struct Observer {
        virtual ~Observer() = default;
        virtual void OnRand(int value) = 0;
    };

    explicit CrtRand(uint32_t s = 1) : state(s) {}

    void Seed(uint32_t seed) { state = seed; }  // srand 0x004ADEC5

    // rand 0x004ADED2: 0..32767
    int Next() {
        state = state * kMul + kAdd;
        const int v = static_cast<int>((state >> 16) & 0x7FFF);
        if (observer) observer->OnRand(v);
        return v;
    }

    void Skip(uint64_t n) { state = Advance(state, n); }
    uint32_t State() const { return state; }

    // State after n calls, O(log n).
    static uint32_t Advance(uint32_t state, uint64_t n);

    uint32_t state;
    Observer* observer = nullptr;
};

// Game_IsDemoMode 0x00413280: game mode 0x009C8E98 is 5, 9, 10 or 11, or 6 with 0x009C90A4 == 8.
bool GameIsDemoMode(int mode, int subMode);

// Game_SeedRandom 0x004157D0: srand(GameIsDemoMode() ? 0 : *(u32*)0x009A32A0). Called by the stage start
// (0x00460030) and by event opcode 0x4D (0x0045EEC0; in mode 5 only the first time after a stage start, flag
// 0x009A26A4). In the attract demo the seed is therefore always 0.
inline uint32_t GameRandomSeed(bool demoMode, uint32_t counter) { return demoMode ? 0u : counter; }

// Player_DemoInput 0x00415710: the attract demo's fake gun, run once per player per frame from the player task
// (0x00413E90 -> 0x00414940 / 0x00414B90) while 0x009C71C4 == 1. One rand() decides whether to fire:
// rand() % 60 < 1, or < 6 when the "busy" flag 0x009C7006 is set (return addresses 0x0041572E / 0x00415740).
// On a shot two more calls pick the aim offset: x = (rand() % 150) * 2 - 150, y = (rand() % 100) * 2 - 100
// (0x00415757 / 0x0041576B). Without a shot the original stores the low 16 bits of a pointer as the aim (unused
// because the shot flag is 0); this returns 0, 0 instead.
struct DemoInput {
    bool fire = false;
    int16_t x = 0, y = 0;
};
DemoInput PlayerDemoInput(CrtRand& rng, bool busy);

}  // namespace game
