// sprite_test <game dir> <capture with S, R, Q lines (HOTD2_SCENECAP_SPRITES=1)>
// Validates game/sprite.* against the ORIGINAL game's sprite pipeline:
//   1. definitions -> records: every S line (Spr_Draw input) is turned into a record with MakeSpriteRecord and
//      SpriteTables; per frame, every R line (Spr_DrawRecord input; the deferred list key bits 0-30 and the unused
//      word 2 are ignored) must match a definition, and normally the sets are equal.
//   2. records -> vertices: for every R line directly followed by its Q line (Draw_TLStrip), SpriteQuad with the
//      captured texture size must reproduce the 4 vertices (exact float equality counted separately).
//   4. draw order: the frame's records in definition order, sorted with CrtQsort + ComparePass1 (sprites only;
//      the game's list also holds translucent models), must give the captured R order.
//   5. layouts: every captured definition of a title logo sprite (Title_DrawLogo 0x004960A0) or of 0x900 (PRESS START,
//      0x004067D0) must equal TitleLogoSprites() / PressStartSprite().
//   3. texture size: the runtime size in the R line must equal the static descriptor of the texture id in the
//      sprite's texture file.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "game/crt_qsort.h"
#include "game/exe_image.h"
#include "game/sprite.h"

static std::vector<uint32_t> Hex(std::istringstream& ss, int n) {
    std::vector<uint32_t> w(n);
    for (auto& x : w) {
        std::string t;
        ss >> t;
        x = static_cast<uint32_t>(std::stoul(t, nullptr, 16));
    }
    return w;
}

static std::string Key(const game::SpriteRecord& r) {
    char b[256];
    snprintf(b, sizeof(b), "%08X %.7g %08X %08X %d %08X %.7g %.7g %.7g %.7g %.7g %.7g %.7g %.7g", r.key & 0x80000000u, r.depth,
             r.texture, r.color, r.rot, r.flags, r.x, r.y, r.sx, r.sy, r.u0, r.v0, r.u1, r.v1);
    return b;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: sprite_test <game dir> <capture>\n");
        return 2;
    }
    game::ExeImage exe;
    if (!exe.Load(std::string(argv[1]) + "/Hod2.exe")) return 2;
    game::SpriteTables tables;
    tables.Load(exe);
    std::map<int, game::TextureFileEntry> files;
    for (const auto& f : game::ListTextureFiles(exe)) files[f.index] = f;
    const float white[3] = { 1, 1, 1 };
    std::map<int, game::SpriteDef> layout;
    for (const auto& d : game::TitleLogoSprites()) layout[d.id] = d;
    layout[0x900] = game::PressStartSprite();
    int layoutOk = 0, layoutBad = 0;
    auto same = [](const game::SpriteDef& a, const game::SpriteDef& b) {
        return a.id == b.id && a.x == b.x && a.y == b.y && a.depth == b.depth && a.sx == b.sx && a.sy == b.sy && a.rot == b.rot &&
               a.alpha == b.alpha && a.blend == b.blend && a.flags == b.flags;
    };

    std::multiset<std::string> fromDefs, fromRecs;
    std::vector<game::SpriteRecord> defOrder;
    std::vector<std::string> recOrder;
    int orderOk = 0, orderBad = 0;
    int framesOk = 0, framesBad = 0, framesUndrawn = 0, defs = 0, quads = 0, quadsExact = 0, quadsBad = 0, sizeOk = 0, sizeBad = 0;
    double worst = 0;
    std::map<uint32_t, int> recTexFile;  // texture id -> file slot, from the S lines
    game::SpriteRecord lastRec;
    int lastW = 0, lastH = 0;
    bool haveRec = false;
    std::ifstream cap(argv[2]);
    for (std::string line; std::getline(cap, line);) {
        if (line.size() < 2) continue;
        std::istringstream ss(line.substr(2));
        switch (line[0]) {
            case 'S': {
                const auto w = Hex(ss, 14);
                const game::SpriteDef d = game::SpriteDef::FromWords(w.data());
                if (layout.count(d.id)) same(d, layout[d.id]) ? ++layoutOk : ++layoutBad;
                const uint32_t tex = tables.TextureId(d.id);
                recTexFile[tex] = tables.TextureFile(d.id);
                fromDefs.insert(Key(game::MakeSpriteRecord(d, tex, white)));
                defOrder.push_back(game::MakeSpriteRecord(d, tex, white));
                ++defs;
                haveRec = false;
                break;
            }
            case 'R': {
                const auto w = Hex(ss, 15);
                ss >> lastW >> lastH;
                lastRec = game::SpriteRecord::FromWords(w.data());
                fromRecs.insert(Key(lastRec));
                recOrder.push_back(Key(lastRec));
                haveRec = true;
                auto it = recTexFile.find(lastRec.texture);
                if (it != recTexFile.end() && files.count(it->second)) {
                    const auto& f = files[it->second];
                    bool found = false;
                    for (int i = 0; i < f.count && !found; ++i) {
                        const game::TextureDesc td = game::ReadTextureDesc(exe, f, i);
                        if (td.id != lastRec.texture) continue;
                        found = true;
                        (td.width == lastW && td.height == lastH) ? ++sizeOk : ++sizeBad;
                    }
                    if (!found) ++sizeBad;
                }
                break;
            }
            case 'Q': {
                int n;
                ss >> n;
                if (!haveRec || n != 4) break;
                haveRec = false;
                game::TLVertex v[4];
                game::SpriteQuad(lastRec, lastW, lastH, 1.0001f, -0.80008f, v);
                bool exact = true, ok = true;
                for (int k = 0; k < 4; ++k) {
                    float q[4];
                    std::string dif, spec;
                    float u, vv;
                    ss >> q[0] >> q[1] >> q[2] >> q[3] >> dif >> spec >> u >> vv;
                    const float mine[4] = { v[k].x, v[k].y, v[k].z, v[k].rhw };
                    for (int j = 0; j < 4; ++j) {
                        const double d = std::fabs(static_cast<double>(mine[j]) - q[j]);
                        worst = std::max(worst, d);
                        if (d != 0) exact = false;
                        if (d > 1e-3) ok = false;
                    }
                    if (std::stoul(dif, nullptr, 16) != v[k].diffuse || std::fabs(u - v[k].u) > 1e-6 || std::fabs(vv - v[k].v) > 1e-6)
                        ok = false;
                }
                ++quads;
                if (exact) ++quadsExact;
                if (!ok && ++quadsBad <= 5) printf("quad mismatch: %s\n", Key(lastRec).c_str());
                break;
            }
            case 'F': {
                if (!fromDefs.empty() || !fromRecs.empty()) {
                    // Every drawn record must come from a definition. Frames where definitions were not (all) drawn
                    // occur at scene changes (the frame is not rendered); they are counted, not failed.
                    std::multiset<std::string> rest = fromDefs;
                    bool explained = true;
                    for (const auto& r : fromRecs) {
                        auto it = rest.find(r);
                        if (it == rest.end()) explained = false;
                        else rest.erase(it);
                    }
                    if (!explained) {
                        if (++framesBad <= 5) printf("frame %s: drawn record without matching definition\n",
                                                     line.substr(2, line.find(' ', 2) - 2).c_str());
                    } else if (rest.empty()) {
                        ++framesOk;
                        game::CrtQsort(defOrder.data(), defOrder.size(), [](const game::SpriteRecord& a, const game::SpriteRecord& b) {
                            return game::ComparePass1(a.key | 0xF, a.depth, b.key | 0xF, b.depth);
                        });
                        bool same = true;
                        for (size_t i = 0; i < defOrder.size(); ++i) same = same && Key(defOrder[i]) == recOrder[i];
                        same ? ++orderOk : ++orderBad;
                    } else {
                        ++framesUndrawn;
                    }
                }
                fromDefs.clear();
                fromRecs.clear();
                defOrder.clear();
                recOrder.clear();
                break;
            }
        }
    }
    printf("definitions %d; frames with matching record sets %d, with undrawn definitions %d, with unexplained records %d; quads %d: exact %d, within 1e-3 %d, "
           "differing %d (worst %.3g); texture sizes equal to descriptors %d, differing %d; draw order reproduced in %d frames, not in %d; title/press-start layouts equal %d, differing %d\n",
           defs, framesOk, framesUndrawn, framesBad, quads, quadsExact, quads - quadsBad, quadsBad, worst, sizeOk, sizeBad, orderOk, orderBad, layoutOk, layoutBad);
    return framesBad == 0 && quadsBad == 0 && sizeBad == 0 && layoutBad == 0 && quads > 0 ? 0 : 1;
}
