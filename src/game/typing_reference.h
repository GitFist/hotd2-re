#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "game/anim.h"
#include "game/camera.h"
#include "game/event.h"
#include "game/texture.h"
#include <array>

namespace game {

struct TypingReferenceResource {
    uint32_t kind = 0, file = 0; // 0: pol, 1: texture
    std::string name;
    std::vector<uint8_t> bytes;
    std::vector<int> models;
    std::vector<TextureDesc> textures;
    std::vector<uint8_t> texturePalettes;
    std::vector<uint8_t> canonicalModels;
};
struct TypingReferenceSprite { uint32_t file = 0, texture = 0; };
struct TypingReferenceReward { int model = 0, sprite = 0; float x = 0, y = 0, scale = 1; };
struct TypingReferenceSound { std::string path; uint32_t flags = 0; }; // bit0 loop, bit1 stop looped SE
struct TypingReferenceDialogue { int duration = 0; uint32_t sound = 0; std::array<int,4> records = {}; };
struct TypingReferenceSubtitle { float x = 0; std::string text; int threshold = 0; };

// Data-only output of tools/import_typing_reference.py. Original executables are
// offline importer inputs; the native engine never loads or executes Tod_e.exe.
class TypingReference {
public:
    bool Load(const std::string& path, std::string* error, bool loadAudioData = true);
    bool Available() const { return available_; }
    bool Complete() const { return available_ && issues_.empty(); }
    const std::vector<std::string>& Issues() const { return issues_; }
    bool ApplyStage(int mode, EventMemory& memory, CameraBank& cameras, std::string* error) const;
    const Motion* ExtraMotion(int id) const;
    const std::vector<TypingReferenceResource>& Resources() const { return resources_; }
    const std::vector<std::array<uint16_t,16>>& Palettes() const { return palettes_; }
    const TypingReferenceResource* Resource(uint32_t file, uint32_t kind) const;
    int ModelFile(int model) const;
    const std::vector<int>* PolModels(uint32_t file) const;
    const std::vector<uint8_t>* SceneRoutes(int mode) const;
    uint32_t CivilianScript(int index) const;
    bool CivilianWord(uint32_t address, uint32_t* value) const;
    // Prepared ids use 0x4000 + the original sprite id; native sprite ids keep
    // their ordinary SpriteTables lookup.
    const TypingReferenceSprite* SourceSprite(int id) const;
    size_t SourceSpriteCount() const { return sprites_.size(); }
    const TypingReferenceReward* Reward(int item) const;
    int SourceModel(int id) const;
    // Native actor code retains semantic model ids for damage/state switches.
    // These authored bone/swap bindings apply only in a native enemy draw scope.
    int NativeEnemyModel(int kind, int node, int model) const;
    const TypingReferenceSound* Sound(uint32_t id) const;
    const std::vector<uint32_t>& SoundTestEffects() const { return soundTestEffects_; }
    const std::vector<uint32_t>& SoundTestMusic() const { return soundTestMusic_; }
    bool NativeSound(uint32_t id, uint32_t* source) const;
    const std::vector<uint8_t>* SoundData(const std::string& path) const;
    const std::map<std::string,std::vector<uint8_t>>& SoundFiles() const { return soundData_; }
    const TypingReferenceDialogue* Dialogue(int voice, int player) const;
    const TypingReferenceSubtitle* Subtitle(int record) const;
    int SubtitleGlyph(int character) const;

private:
    struct CameraData { std::vector<int> ids; std::vector<uint8_t> bytes; };
    bool available_ = false;
    std::vector<uint8_t> common_;
    std::map<int, std::vector<uint8_t>> stages_;
    std::map<int, std::vector<uint8_t>> stageCommon_, routes_;
    std::vector<CameraData> cameras_;
    std::map<int, int> motionMap_;
    std::map<int, Motion> motions_;
    std::vector<std::string> issues_;
    std::vector<TypingReferenceResource> resources_;
    std::vector<std::array<uint16_t,16>> palettes_;
    uint32_t civilianBase_ = 0;
    std::vector<uint8_t> civilianData_;
    std::vector<uint32_t> civilianScripts_;
    std::vector<TypingReferenceSprite> sprites_;
    std::map<int, TypingReferenceReward> rewards_;
    std::map<int, int> modelMap_;
    std::map<std::array<int,3>,int> enemyModels_;
    std::map<uint32_t, TypingReferenceSound> sounds_;
    std::vector<uint32_t> soundTestEffects_, soundTestMusic_;
    std::map<uint32_t, uint32_t> nativeSounds_;
    std::map<std::string, std::vector<uint8_t>> soundData_;
    std::vector<int16_t> dialogueChoices_, subtitleGlyphs_;
    std::vector<TypingReferenceDialogue> dialogue_;
    std::vector<TypingReferenceSubtitle> subtitles_;
};

} // namespace game
