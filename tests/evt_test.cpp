// evt_test <game dir> <stage table file> <capture with E lines>
// Validates game::EventMemory / DecodeEvent against an event VM trace of the ORIGINAL game
// (HOTD2_SCENECAP_EVENTS=1): the opcode at every traced address must match the relocated memory image, and
// the decoded length must equal the distance to the next traced instruction whenever execution was sequential.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "game/event.h"

namespace fs = std::filesystem;

static std::vector<uint8_t> ReadAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: evt_test <game dir> <stage table> <capture>\n");
        return 2;
    }
    const fs::path game = argv[1];
    game::EventMemory mem;
    mem.Load(game::EventMemory::kCommonBase, ReadAll(game / "evt" / "comevtbl.bin"));
    mem.Load(game::EventMemory::kStageBase, ReadAll(game / "evt" / argv[2]));

    struct Ev { uint32_t op, addr; };
    std::vector<Ev> ev;
    std::ifstream cap(argv[3]);
    for (std::string line; std::getline(cap, line);) {
        if (line.empty() || line[0] != 'E') continue;
        std::istringstream ss(line.substr(2));
        unsigned frame, op;
        std::string addr;
        ss >> frame >> op >> addr;
        ev.push_back({ op, static_cast<uint32_t>(std::stoul(addr, nullptr, 16)) });
    }
    int opOk = 0, opBad = 0, lenOk = 0, lenBad = 0, undecoded = 0;
    for (size_t i = 0; i < ev.size(); ++i) {
        game::EventOp op;
        uint32_t word = 0;
        if (!mem.Read(ev[i].addr, &word) || word != ev[i].op) {
            ++opBad;
            continue;
        }
        ++opOk;
        if (!game::DecodeEvent(mem, ev[i].addr, op)) {
            ++undecoded;
            continue;
        }
        if (i + 1 < ev.size()) {
            const int64_t d = static_cast<int64_t>(ev[i + 1].addr) - ev[i].addr;
            if (d > 0 && d <= 64) {
                if (d == op.length) ++lenOk;
                else if (++lenBad <= 5) printf("length mismatch %s at %08X: %u vs %lld\n", op.name, ev[i].addr, op.length, d);
            }
        }
    }
    printf("trace events %zu: opcode matches %d (mismatch %d), sequential lengths agree %d (mismatch %d), undecoded %d\n",
           ev.size(), opOk, opBad, lenOk, lenBad, undecoded);
    return opBad == 0 && lenBad == 0 && undecoded == 0 && opOk > 0 ? 0 : 1;
}
