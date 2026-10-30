// Offline, data-only identity comparison for the supplied PC model resources.
#include "game/exe_image.h"
#include "game/model.h"
#include "game/prs.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace fs = std::filesystem;
struct Entry { int id; bool canonical; std::vector<uint8_t> identity; };
using Files = std::map<std::string,std::vector<Entry>>;

std::vector<uint8_t> Decode(const fs::path& path) {
    std::ifstream input(path,std::ios::binary);
    if (!input) throw std::runtime_error("cannot read "+path.string());
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(input)),{});
    uint32_t size = 0;
    if (raw.size() >= 8) memcpy(&size,raw.data(),4);
    if (size && size < (64u << 20)) {
        std::vector<uint8_t> decoded(size);
        if (game::DecompressPrsSafe(raw.data()+4,raw.size()-4,decoded.data(),decoded.size()) == int(size)) return decoded;
    }
    return raw;
}

Files Catalog(const fs::path& root,bool source) {
    game::ExeImage exe;
    if (!exe.Load((root/(source ? "Tod_e.exe" : "Hod2.exe")).string())) throw std::runtime_error("cannot read model tables");
    Files result;
    const uint32_t names = source ? 0x4EFB78 : 0x4D0EF4;
    const uint32_t end = source ? 0x4F0108 : 0x4D1410;
    const uint32_t counts = source ? 0x508F20 : 0x4E803C;
    const uint32_t lists = source ? 0x508688 : 0x4E794C;
    for (uint32_t index = 1; names+index*4 < end; ++index) {
        const std::string name = exe.CStr(exe.U32(names+index*4));
        if (name.size() < 4 || !name.ends_with(".bin")) continue;
        const auto data = Decode(root/"pol"/name);
        const auto headers = game::ReadPolHeader(data.data(),data.size());
        const int count = exe.S16(counts+index*2);
        if (count == 0) continue; // Unregistered file payloads have no global model identities.
        if (count != int(headers.size())) throw std::runtime_error(name+": header count differs from model identity count");
        const auto list = exe.U32(lists+index*4);
        for (int slot = 0; slot < count; ++slot) {
            Entry entry;
            entry.id = uint16_t(exe.S16(list+slot*2));
            entry.canonical = uint16_t(exe.S16((source ? 0x509370 : 0x4E83B4)+entry.id*2)) == index;
            game::Model model;
            std::string error;
            if (!game::ParseModel(data.data(),data.size(),headers[slot],model,&error))
                throw std::runtime_error(name+": "+error);
            auto append = [&](const void* p,size_t size) {
                const auto* bytes = static_cast<const uint8_t*>(p);
                entry.identity.insert(entry.identity.end(),bytes,bytes+size);
            };
            auto word = [&](uint32_t value) { append(&value,4); };
            word(model.word0); word(model.flags); append(model.sphere,sizeof(model.sphere));
            word(uint32_t(model.meshes.size()));
            for (const auto& mesh : model.meshes) {
                append(mesh.material.words,sizeof(mesh.material.words));
                word(uint32_t(mesh.strips.size()));
                for (const auto& strip : mesh.strips) {
                    word(strip.ctrl); word(uint32_t(strip.vertices.size()));
                    append(strip.vertices.data(),strip.vertices.size()*sizeof(game::ModelVertex));
                }
            }
            result[name].push_back(std::move(entry));
        }
    }
    return result;
}

int main(int argc,char** argv) try {
    if (argc != 4) throw std::runtime_error("usage: typing_model_map <reference-dir> <native-data-dir> <output.json>");
    const auto source = Catalog(argv[1],true), native = Catalog(argv[2],false);
    std::ofstream output(argv[3],std::ios::binary);
    if (!output) throw std::runtime_error("cannot write model identity map");
    output << "{\"models\":{\"0\":0";
    unsigned matched=0,additional=0;
    for (const auto& [name,entries] : source) for (const auto& entry : entries) {
        if (!entry.id || !entry.canonical) continue;
        int mapped = 0x4000+entry.id;
        if (const auto file = native.find(name); file != native.end())
            for (const auto& candidate : file->second) if (candidate.canonical && entry.identity == candidate.identity) {
                mapped = candidate.id;
                break;
            }
        if (mapped >= 0x8000) throw std::runtime_error("prepared model id does not fit authored signed-short fields");
        mapped >= 0x4000 ? ++additional : ++matched;
        output << ",\"" << entry.id << "\":" << mapped;
    }
    output << "},\"matched\":" << matched << ",\"additional\":" << additional << "}\n";
    std::cout << matched << " exact decoded model matches, " << additional << " models require prepared resources\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
