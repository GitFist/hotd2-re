// pass1_test <capture with P/p lines (HOTD2_SCENECAP_PASS1=1)>
// Validates game::CrtQsort + game::ComparePass1 against the ORIGINAL pass-1 lists: the unsorted list logged at
// Mdl_DrawPass1 0x004A88E0 entry (P line: key and depth of every deferred sprite/model) is sorted natively and
// must give exactly the order the game drew (p line).

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "game/crt_qsort.h"
#include "game/sprite.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: pass1_test <capture>\n");
        return 2;
    }
    struct Entry {
        int index;
        uint32_t key;
        float depth;
    };
    std::vector<Entry> list;
    int lists = 0, ok = 0, bad = 0, mixed = 0, maxLen = 0;
    std::ifstream cap(argv[1]);
    for (std::string line; std::getline(cap, line);) {
        if (line.size() < 2) continue;
        std::istringstream ss(line.substr(2));
        int n;
        ss >> n;
        if (line[0] == 'P') {
            list.clear();
            for (int i = 0; i < n; ++i) {
                std::string tok;
                ss >> tok;
                // kind:id:key:depth
                const size_t a = tok.find(':'), b = tok.find(':', a + 1), c = tok.find(':', b + 1);
                Entry e;
                e.index = i;
                e.key = static_cast<uint32_t>(std::stoul(tok.substr(b + 1, c - b - 1), nullptr, 16));
                const uint32_t bits = static_cast<uint32_t>(std::stoul(tok.substr(c + 1), nullptr, 16));
                memcpy(&e.depth, &bits, 4);
                list.push_back(e);
            }
        } else if (line[0] == 'p') {
            if (n < 2) continue;
            ++lists;
            maxLen = std::max(maxLen, n);
            bool hasModel = false, hasSprite = false;
            for (const auto& e : list) (e.key & 0x40000000 ? hasModel : hasSprite) = true;
            if (hasModel && hasSprite) ++mixed;
            std::vector<Entry> sorted = list;
            game::CrtQsort(sorted.data(), sorted.size(),
                           [](const Entry& x, const Entry& y) { return game::ComparePass1(x.key, x.depth, y.key, y.depth); });
            bool same = true;
            for (int i = 0; i < n; ++i) {
                int idx;
                ss >> idx;
                same = same && sorted[i].index == idx;
            }
            if (same) ++ok;
            else if (++bad <= 5) printf("list %d (%d entries) differs\n", lists, n);
        }
    }
    printf("pass-1 lists with >= 2 entries: %d (with models and sprites: %d, longest %d); order reproduced %d, differing %d\n",
           lists, mixed, maxLen, ok, bad);
    return lists > 0 && bad == 0 ? 0 : 1;
}
