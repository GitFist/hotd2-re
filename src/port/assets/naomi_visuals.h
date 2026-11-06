#pragma once

// Data-only arcade visual resources produced by tools/import_naomi_visuals.py.
// The runtime never reads a Naomi ROM, BIOS or executable.
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "game/model.h"
#include "game/texture.h"

namespace port {
class NaomiVisuals {
public:
    struct TextureRef { std::string bank; uint32_t index = 0; };
    struct Resource {
        uint32_t kind = 0, unpackedSize = 0;
        bool prs = false;
        std::vector<game::TextureDesc> textures;
        std::vector<uint8_t> bytes;
    };
    struct ModelRef { std::string bank; uint32_t index = 0, evidence = 0; };
    struct MeshRef { std::string bank; uint32_t model = 0, mesh = 0, evidence = 0; };
    struct Palette { game::PixelFormat format = game::PixelFormat::Argb1555; std::vector<uint16_t> colors; };
    bool Load(const std::filesystem::path& path, std::string* error);
    static bool Inspect(const std::filesystem::path& path, std::string* error);
    bool Available() const { return available_; }
    size_t TextureMappingCount() const { return textureMap_.size(); }
    size_t ModelMappingCount() const { return modelMap_.size(); }
    const TextureRef* TextureMapping(const std::string& pcBank, uint32_t index) const;
    const ModelRef* ModelMapping(const std::string& pcBank, uint32_t index) const;
    const Resource* Find(uint32_t kind, const std::string& bank) const;
    const std::vector<uint8_t>* Data(uint32_t kind, const std::string& bank);
    const game::TextureDesc* Descriptor(const std::string& bank, uint32_t index) const;
    const TextureRef* BloodTexture(uint32_t nativeId, bool red) const;
    bool Model(const std::string& pcBank, uint32_t index, game::Model& out, std::string* nativeBank);
    // Evidence12 is a UV/material-only source;16 has original skin-layout proof
    // for authored UV changes. Both require an evidence-aware caller.
    bool Mesh(const std::string& pcBank,uint32_t model,uint32_t mesh,game::ModelMesh& out,std::string* nativeBank,uint32_t* evidence=nullptr);
    bool CanDecodeTexture(const std::string& bank,uint32_t index) const;
    bool DecodeRgba(const std::string& bank,uint32_t index,std::vector<uint8_t>& out);
    size_t PaletteTextureCount() const { return paletteMap_.size(); }
    size_t MeshMappingCount() const { return meshMap_.size(); }
    void ClearDecoded();
    static bool SupportedTexture(const game::TextureDesc& desc);
private:
    using Key = std::pair<std::string,uint32_t>;
    using ResourceKey = std::pair<uint32_t,std::string>;
    bool available_ = false;
    std::map<ResourceKey,Resource> resources_;
    std::map<Key,TextureRef> textureMap_;
    std::map<Key,ModelRef> modelMap_;
    std::map<std::tuple<std::string,uint32_t,uint32_t>,MeshRef> meshMap_;
    std::map<uint32_t,Palette> palettes_;
    std::map<Key,uint32_t> paletteMap_;
    std::map<uint32_t,TextureRef> red_, green_;
    std::map<ResourceKey,std::vector<uint8_t>> decoded_;
    std::map<Key,game::Model> parsed_;
    const game::Model* NativeModel(const std::string& bank,uint32_t index);
};
} // namespace port
