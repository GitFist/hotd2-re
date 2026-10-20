// skin_test <game dir>: loads every skinned-attachment definition (0x0052ED08) and checks that all vertex occurrence
// offsets of its vertex table land on vertex records of the model (relative to the model's block start or root),
// i.e. that game::SkinDeform can address the vertices the original rewrites (0x0041A480 / 0x0041A4D0).

#include <cstdio>
#include <cstring>
#include <string>

#include "game/exe_image.h"
#include "game/skin.h"
#include "render/asset_store.h"

// Original-output fixtures from 0x00419980, run in Unicorn with FPU control
// word 0x007F (24-bit precision). The fixture has one logical vertex;
// 0x0041A520 / 0x0041A580 also write its reflected twin. This test duplicates
// the occurrence reference to check that every copy receives the captured value.
// Expected words are captured outputs, independent of the native matrix helpers.
static int DeformationFixtures() {
    struct Fixture { uint32_t base[16], node[16], source[6], expected[6], twin[6]; };
    static const Fixture fixtures[] = {
      {
        {0x3f6ac86eu,0x00000000u,0xbecc1686u,0x00000000u,0x00000000u,0x3f800000u,0x00000000u,0x00000000u,0x3ecc1686u,0x00000000u,0x3f6ac86eu,0x00000000u,0x41aa0000u,0x41fc0000u,0xc2e58000u,0x3f800000u},
        {0x3f5ce0edu,0x00000000u,0xbf016aa2u,0x00000000u,0x00000000u,0x3f800000u,0x00000000u,0x00000000u,0x3f016aa2u,0x00000000u,0x3f5ce0edu,0x00000000u,0x41b4147bu,0x41f1c28fu,0xc2e2e666u,0x3f800000u},
        {0x3ed3f7cfu,0xbe6c8b44u,0x3f245a1du,0x3e2e147bu,0x3f6e147bu,0x3ea3d70au},
        {0x3f900869u,0xbfc16878u,0x401211a2u,0x3e540e2fu,0x3f6e147bu,0x3e983df5u},
        {0x3f900869u,0xbfc16878u,0x401211a2u,0xbe540e2fu,0xbf6e147bu,0xbe983df5u}
      },
      {
        {0x3f56226bu,0x00000000u,0xbf0c4b4cu,0x00000000u,0x00000000u,0x3f800000u,0x00000000u,0x00000000u,0x3f0c4b4cu,0x00000000u,0x3f56226bu,0x00000000u,0x41aaf5c3u,0x41f9999au,0xc2e4cccdu,0x3f800000u},
        {0x3f60a940u,0x00000000u,0xbef57744u,0x00000000u,0x00000000u,0x3f800000u,0x00000000u,0x00000000u,0x3ef57744u,0x00000000u,0x3f60a940u,0x00000000u,0x41b1d70au,0x41f28f5cu,0xc2e28000u,0x3f800000u},
        {0x3f039581u,0xbe4dd2f2u,0x3f0d4fdfu,0x3e2e147bu,0x3f6e147bu,0x3ea3d70au},
        {0x3f0eb009u,0xbf8a5e1eu,0x40019228u,0x3e135637u,0x3f6e147bu,0x3eaa4593u},
        {0x3f0eb009u,0xbf8a5e1eu,0x40019228u,0xbe135637u,0xbf6e147bu,0xbeaa4593u}
      },
      {
        {0x3f3b4ff6u,0x00000000u,0xbf2e7fe1u,0x00000000u,0x00000000u,0x3f800000u,0x00000000u,0x00000000u,0x3f2e7fe1u,0x00000000u,0x3f3b4ff6u,0x00000000u,0x41abeb85u,0x41f73333u,0xc2e4199au,0x3f800000u},
        {0x3f643dd2u,0x00000000u,0xbee7e0b6u,0x00000000u,0x00000000u,0x3f800000u,0x00000000u,0x00000000u,0x3ee7e0b6u,0x00000000u,0x3f643dd2u,0x00000000u,0x41af999au,0x41f35c29u,0xc2e2199au,0x3f800000u},
        {0x3f1d2f1bu,0xbe2f1aa0u,0x3eec8b44u,0x3e2e147bu,0x3f6e147bu,0x3ea3d70au},
        {0x3df054f1u,0xbf26a7c8u,0x3fd45825u,0x3d997d16u,0x3f6e147bu,0x3eb58369u},
        {0x3df054f1u,0xbf26a7c8u,0x3fd45825u,0xbd997d16u,0xbf6e147bu,0xbeb58369u}
      },
      {
        {0x3f1b1708u,0x00000000u,0xbf4bac8cu,0x00000000u,0x00000000u,0x3f800000u,0x00000000u,0x00000000u,0x3f4bac8cu,0x00000000u,0x3f1b1708u,0x00000000u,0x41ace148u,0x41f4cccdu,0xc2e36666u,0x3f800000u},
        {0x3f679dceu,0x00000000u,0xbeda14bdu,0x00000000u,0x00000000u,0x3f800000u,0x00000000u,0x00000000u,0x3eda14bdu,0x00000000u,0x3f679dceu,0x00000000u,0x41ad5c29u,0x41f428f6u,0xc2e1b333u,0x3f800000u},
        {0x3f36c8b4u,0xbe10624eu,0x3ebe76c9u,0x3e2e147bu,0x3f6e147bu,0x3ea3d70au},
        {0xbe36aa05u,0xbe624e4eu,0x3f9c7626u,0x3b45e840u,0x3f6e147bu,0x3eb984bdu},
        {0xbe36aa05u,0xbe624e4eu,0x3f9c7626u,0xbb45e840u,0xbf6e147bu,0xbeb984bdu}
      },
    };
    int bad = 0, checked = 0;
    for (const Fixture& f : fixtures) {
        game::Mat4 base, node;
        std::memcpy(base.m, f.base, sizeof(f.base));
        std::memcpy(node.m, f.node, sizeof(f.node));
        const game::Mat4 nodes[4] = {node,node,node,node};
        game::SkinDef def;
        def.vertexOffsets = {{0x10}};
        def.rec[0].count = 1;
        def.rec[0].src.resize(6);
        std::memcpy(def.rec[0].src.data(), f.source, sizeof(f.source));
        def.rec[0].groups = {0};
        for (bool twin : {false,true}) {
            game::Model model;
            model.blockStart = twin ? 0x92C0 : 0x1000;
            game::ModelMesh mesh;
            game::ModelStrip strip;
            strip.vertices.resize(3);
            strip.vertices[1].pos[0] = .123f;
            strip.vertices[1].normal[2] = .75f;
            const game::ModelVertex untouched = strip.vertices[1];
            strip.offsets = {model.blockStart+0x10,model.blockStart+0x20,model.blockStart+0x10};
            mesh.strips.push_back(strip);
            model.meshes.push_back(mesh);
            game::SkinDeform(def, model.blockStart, base, nodes, 4, model, twin);
            const auto& vertices = model.meshes[0].strips[0].vertices;
            if (std::memcmp(&vertices[1], &untouched, sizeof(untouched)) != 0) ++bad;
            for (int index : {0,2}) {
                uint32_t actual[6];
                std::memcpy(actual, vertices[index].pos, 12);
                std::memcpy(actual+3, vertices[index].normal, 12);
                const uint32_t* expected = twin ? f.twin : f.expected;
                for (int word=0;word<6;++word) {
                    ++checked;
                    if (actual[word] != expected[word]) {
                        ++bad;
                        std::printf("skin fixture %s word %d: %08X expected %08X\n",
                                    twin ? "twin" : "core",word,actual[word],expected[word]);
                    }
                }
            }
        }
    }
    std::printf("skin deformation original-output words: %d checked, %d differences\n",checked,bad);
    return bad;
}

int main(int argc, char** argv) {
    if (DeformationFixtures()) return 1;
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
