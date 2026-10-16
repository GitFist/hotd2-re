#pragma once

// The MSVC (VC6) CRT qsort linked into Hod2.exe (0x004ADCF7, shortsort 0x004ADE4B, swap 0x004ADE99).
// Reproduced exactly because it is not stable: the game's draw order for equal sort keys depends on it
// (pass-1 list, Mdl_DrawPass1 0x004A88E0). cmp(a, b) returns <0, 0, >0 like the C comparator.

#include <cstddef>
#include <utility>

namespace game {

template <typename T, typename Cmp>
void CrtQsort(T* base, size_t num, Cmp cmp) {
    if (num < 2) return;
    T* lostk[30];
    T* histk[30];
    int stkptr = 0;
    T* lo = base;
    T* hi = base + (num - 1);
    for (;;) {
        const size_t size = static_cast<size_t>(hi - lo) + 1;
        if (size <= 8) {
            // shortsort: repeatedly move the maximum to the end.
            for (T* h = hi; h > lo; --h) {
                T* max = lo;
                for (T* p = lo + 1; p <= h; ++p)
                    if (cmp(*p, *max) > 0) max = p;
                std::swap(*max, *h);
            }
        } else {
            T* mid = lo + size / 2;
            std::swap(*mid, *lo);
            T* loguy = lo;
            T* higuy = hi + 1;
            for (;;) {
                do {
                    ++loguy;
                } while (loguy <= hi && cmp(*loguy, *lo) <= 0);
                do {
                    --higuy;
                } while (higuy > lo && cmp(*higuy, *lo) >= 0);
                if (higuy < loguy) break;
                std::swap(*loguy, *higuy);
            }
            std::swap(*lo, *higuy);
            if (higuy - 1 - lo >= hi - loguy) {
                if (lo + 1 < higuy) {
                    lostk[stkptr] = lo;
                    histk[stkptr] = higuy - 1;
                    ++stkptr;
                }
                if (loguy < hi) {
                    lo = loguy;
                    continue;
                }
            } else {
                if (loguy < hi) {
                    lostk[stkptr] = loguy;
                    histk[stkptr] = hi;
                    ++stkptr;
                }
                if (lo + 1 < higuy) {
                    hi = higuy - 1;
                    continue;
                }
            }
        }
        if (--stkptr < 0) return;
        lo = lostk[stkptr];
        hi = histk[stkptr];
    }
}

}  // namespace game
