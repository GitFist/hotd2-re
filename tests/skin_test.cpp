// skin_test <game dir>: loads every skinned-attachment definition (0x0052ED08) and checks that all vertex occurrence
// offsets of its vertex table land on vertex records of the model (relative to the model's block start or root),
// i.e. that game::SkinDeform can address the vertices the original rewrites (0x0041A480 / 0x0041A4D0).

#include <cstdio>
#include <string>

#include "game/exe_image.h"
#include "game/skin.h"
#include "render/asset_store.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: skin_test <game dir>\n");
        return 2;
    }
    port::AssetStore assets;
    std::string err;
    if (!assets.Open(argv[1], &err)) {
        fprintf(stderr, "%s\n", err.c_str());
        return 2;
    }
    const auto defs = game::SkinDefsByModel(assets.Exe());
    int ok = 0, bad = 0, missing = 0, blockBase = 0, rootBase = 0;
    for (const auto& [model, va] : defs) {
        game::SkinDef def;
        game::LoadSkinDef(assets.Exe(), va, def);
        game::Model m;
        std::string pol;
        if (!assets.ModelById(model, m, &pol)) {
            ++missing;
            continue;
        }
        int hit = 0, total = 0;
        const uint32_t base = game::SkinOffsetBase(def, m, &hit, &total);
        if (base == UINT32_MAX) {
            ++bad;
            printf("def %08X model %d (%s): %d of %d offsets on vertex records\n", va, model, pol.c_str(), hit, total);
        } else {
            ++ok;
            (base == m.blockStart ? blockBase : rootBase)++;
        }
    }
    printf("skin defs %zu: all offsets on vertex records %d (base = block start %d, root %d), not %d, model missing %d\n",
           defs.size(), ok, blockBase, rootBase, bad, missing);
    return bad == 0 && ok > 0 ? 0 : 1;
}
