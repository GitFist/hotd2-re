#pragma once
#include "port/render/asset_store.h"
#include "game/world_prototype.h"
#include <cstring>

namespace port {
// Small authored building blocks textured with the original game's industrial surfaces.
// IDs are outside the original model table and are resolved before AssetStore::ModelById.
inline bool PrototypeModel(AssetStore& assets, int id, game::Model& model, std::string& pol) {
    const int index = id - game::kPrototypeMaterialBase;
    if (index < 0 || index >= game::kPrototypeMaterialCount) return false;
    pol = "st6_01.bin";
    const auto& exe = assets.Exe();
    int file = -1;
    for (int i = 0; i < 0x200; ++i)
        if (exe.CStr(exe.U32(0x004D1410 + i * 4)) == "st6_01.bin") { file = i; break; }
    if (file < 0 || index >= exe.S16(0x0055C0A8 + file * 2)) return false;
    game::ModelMesh mesh;
    auto f = [&](int word, float value) { memcpy(&mesh.material.words[word], &value, 4); };
    mesh.material.words[1] = 6u << 29; // greater/equal in the original reversed-depth convention
    mesh.material.words[2] = 0x20882000u; // opaque, bilinear, no fog
    mesh.material.words[8] = static_cast<uint32_t>(index); // material references use the descriptor index, not its global sprite ID
    f(7, 1.0f); f(10, 1.0f); for (int i = 11; i <= 14; ++i) f(i, 1.0f);
    // Outward faces; double sided so interiors are visible too. Unit cube is centred at the origin.
    constexpr float vertices[8][3] = {{-.5f,-.5f,-.5f},{.5f,-.5f,-.5f},{.5f,.5f,-.5f},{-.5f,.5f,-.5f},
                                     {-.5f,-.5f,.5f},{.5f,-.5f,.5f},{.5f,.5f,.5f},{-.5f,.5f,.5f}};
    constexpr int faces[6][4] = {{0,1,2,3},{5,4,7,6},{4,0,3,7},{1,5,6,2},{3,2,6,7},{4,5,1,0}};
    constexpr float normals[6][3] = {{0,0,-1},{0,0,1},{-1,0,0},{1,0,0},{0,1,0},{0,-1,0}};
    constexpr float uv[4][2] = {{0,1},{1,1},{1,0},{0,0}};
    game::ModelStrip strip; strip.triangleList = true; strip.ctrl = 8;
    for (int face = 0; face < 6; ++face) for (int corner : {0,1,2,0,2,3}) {
        game::ModelVertex v{};
        memcpy(v.pos, vertices[faces[face][corner]], 12); memcpy(v.normal, normals[face], 12);
        memcpy(v.uv, uv[corner], 8); strip.vertices.push_back(v);
    }
    mesh.strips.push_back(std::move(strip)); model.meshes.push_back(std::move(mesh)); model.sphere[3] = 1;
    return true;
}
}
