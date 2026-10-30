#include "game/typing_reference.h"

#include <cstring>
#include <fstream>
#include <limits>
#include <algorithm>

namespace game {
namespace {

#include "game/typing_sound_test_data.inc"

class Reader {
public:
    explicit Reader(std::vector<uint8_t> bytes) : data(std::move(bytes)) {}
    bool Word(uint32_t& value) {
        if (at > data.size() || data.size() - at < 4) return false;
        memcpy(&value, data.data() + at, 4); at += 4; return true;
    }
    bool Blob(std::vector<uint8_t>& value) {
        uint32_t size = 0;
        if (!Word(size) || size > 32u * 1024u * 1024u || at > data.size() || size > data.size() - at) return false;
        value.assign(data.begin() + at, data.begin() + at + size); at += size; return true;
    }
    bool SkipBlob() {
        uint32_t size = 0;
        if (!Word(size) || size > 32u*1024u*1024u || at > data.size() || size > data.size()-at) return false;
        at += size; return true;
    }
    std::vector<uint8_t> data;
    size_t at = 8;
};

} // namespace

bool TypingReference::Load(const std::string& path, std::string* error, bool loadAudioData) {
    *this = TypingReference{};
    auto fail = [&](const std::string& text) { if (error) *error = text; return false; };
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return fail("prepared typing reference not found: " + path);
    const auto size = file.tellg();
    if (size < 36 || size > 128 * 1024 * 1024) return fail("invalid prepared typing reference size");
    file.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        return fail("cannot read prepared typing reference");
    if (memcmp(bytes.data(), "TOTDREF1", 8) != 0) return fail("unknown prepared typing reference format");
    Reader reader(std::move(bytes));
    uint32_t version, complete, issueCount, stageCount, cameraCount, mapCount, motionCount, resourceCount, paletteCount;
    if (!reader.Word(version) || !reader.Word(complete) || !reader.Word(issueCount) || !reader.Word(stageCount) ||
        !reader.Word(cameraCount) || !reader.Word(mapCount) || !reader.Word(motionCount) || version != 5 ||
        !reader.Word(resourceCount) || !reader.Word(paletteCount) || resourceCount > 2048 || paletteCount > 256 ||
        issueCount > 100000 || stageCount > 16 || cameraCount > 64 || mapCount > 16384 || motionCount > 4096)
        return fail("invalid prepared typing reference header");
    TypingReference loaded;
    for (uint32_t i = 0; i < issueCount; ++i) {
        std::vector<uint8_t> text;
        if (!reader.Blob(text)) return fail("truncated prepared typing conversion issue");
        loaded.issues_.emplace_back(text.begin(), text.end());
    }
    if ((complete != 0) != loaded.issues_.empty()) return fail("inconsistent prepared typing conversion status");
    if (!reader.Blob(loaded.common_)) return fail("truncated prepared typing common table");
    for (uint32_t i = 0; i < stageCount; ++i) {
        uint32_t mode = 0;
        std::vector<uint8_t> data, common, routes;
        if (!reader.Word(mode) || mode >= 16 || !reader.Blob(common) || !reader.Blob(data) || !reader.Blob(routes) ||
            routes.size() % 8 || loaded.stages_.count(static_cast<int>(mode)))
            return fail("invalid prepared typing stage table");
        loaded.stageCommon_.emplace(static_cast<int>(mode), std::move(common));
        loaded.routes_.emplace(static_cast<int>(mode), std::move(routes));
        loaded.stages_.emplace(static_cast<int>(mode), std::move(data));
    }
    for (uint32_t i = 0; i < cameraCount; ++i) {
        uint32_t count = 0;
        if (!reader.Word(count) || count > 4096) return fail("invalid prepared typing camera count");
        CameraData camera;
        for (uint32_t k = 0; k < count; ++k) {
            uint32_t id = 0;
            if (!reader.Word(id) || id > static_cast<uint32_t>(std::numeric_limits<int>::max()))
                return fail("invalid prepared typing camera id");
            camera.ids.push_back(static_cast<int>(id));
        }
        if (!reader.Blob(camera.bytes)) return fail("truncated prepared typing camera data");
        loaded.cameras_.push_back(std::move(camera));
    }
    for (uint32_t i = 0; i < mapCount; ++i) {
        uint32_t source = 0, native = 0;
        if (!reader.Word(source) || !reader.Word(native) || source >= 16384 || native >= 32768)
            return fail("invalid prepared typing motion mapping");
        loaded.motionMap_[static_cast<int>(source)] = static_cast<int>(native);
    }
    for (uint32_t i = 0; i < motionCount; ++i) {
        uint32_t id = 0, ticks = 0;
        Motion motion;
        if (!reader.Word(id) || !reader.Word(ticks) || id >= 32768 || ticks > 65535 || !reader.Blob(motion.data))
            return fail("invalid prepared typing motion data");
        motion.id = static_cast<int>(id); motion.ticks = static_cast<int>(ticks);
        loaded.motions_.emplace(motion.id, std::move(motion));
    }
    for (uint32_t i = 0; i < resourceCount; ++i) {
        TypingReferenceResource resource;
        std::vector<uint8_t> name, textures;
        uint32_t count = 0;
        if (!reader.Word(resource.kind) || resource.kind > 1 || !reader.Word(resource.file) ||
            !reader.Blob(name) || name.empty() || name.size() > 128 || !reader.Blob(resource.bytes) ||
            !reader.Word(count) || count > 4096) return fail("invalid prepared typing resource");
        resource.name.assign(name.begin(), name.end());
        if (resource.name.find_first_of("/\\:") != std::string::npos || resource.name.find("..") != std::string::npos)
            return fail("invalid prepared typing resource name");
        for (uint32_t k = 0; k < count; ++k) {
            uint32_t id = 0;
            if (!reader.Word(id) || id >= 32768) return fail("invalid prepared typing model id");
            resource.models.push_back(static_cast<int>(id));
        }
        if (!reader.Blob(textures) || textures.size() % 16) return fail("invalid prepared typing texture descriptors");
        for (size_t at = 0; at < textures.size(); at += 16) {
            TextureDesc desc;
            memcpy(&desc.width, textures.data()+at, 2); memcpy(&desc.height, textures.data()+at+2, 2);
            memcpy(&desc.format, textures.data()+at+4, 4); memcpy(&desc.offset, textures.data()+at+8, 4);
            memcpy(&desc.id, textures.data()+at+12, 4);
            if (desc.width <= 0 || desc.height <= 0 || desc.width > 4096 || desc.height > 4096)
                return fail("invalid prepared typing texture dimensions");
            resource.textures.push_back(desc);
        }
        if (!reader.Blob(resource.texturePalettes) || resource.texturePalettes.size() != resource.textures.size())
            return fail("invalid prepared typing palette selection");
        if (!reader.Blob(resource.canonicalModels) || resource.canonicalModels.size() != resource.models.size())
            return fail("invalid prepared typing canonical model metadata");
        loaded.resources_.push_back(std::move(resource));
    }
    for (uint32_t i = 0; i < paletteCount; ++i) {
        std::array<uint16_t,16> palette;
        if (reader.at > reader.data.size() || reader.data.size()-reader.at < 32)
            return fail("truncated prepared typing palette");
        memcpy(palette.data(), reader.data.data()+reader.at, 32); reader.at += 32;
        loaded.palettes_.push_back(palette);
    }
    uint32_t civilianCount = 0;
    if (!reader.Word(loaded.civilianBase_) || loaded.civilianBase_ != 0x02000000 ||
        !reader.Blob(loaded.civilianData_) || loaded.civilianData_.empty() ||
        loaded.civilianData_.size() > 1024u * 1024u || !reader.Word(civilianCount) || civilianCount > 256)
        return fail("invalid prepared typing civilian program bank");
    for (uint32_t i = 0; i < civilianCount; ++i) {
        uint32_t address = 0;
        if (!reader.Word(address) || address < loaded.civilianBase_ ||
            static_cast<size_t>(address - loaded.civilianBase_) + 8 > loaded.civilianData_.size())
            return fail("invalid prepared typing civilian script pointer");
        loaded.civilianScripts_.push_back(address);
    }
    uint32_t spriteCount = 0;
    if (!reader.Word(spriteCount) || spriteCount > 16384) return fail("invalid prepared typing sprite count");
    for (uint32_t i = 0; i < spriteCount; ++i) {
        TypingReferenceSprite sprite;
        if (!reader.Word(sprite.file) || !reader.Word(sprite.texture)) return fail("truncated prepared typing sprite table");
        const auto* resource = loaded.Resource(sprite.file, 1);
        if (!resource || std::none_of(resource->textures.begin(), resource->textures.end(), [&](const auto& texture) {
            return texture.id == (sprite.texture & 0x7FFFFFFF);
        })) return fail("prepared typing sprite has no texture descriptor");
        loaded.sprites_.push_back(sprite);
    }
    uint32_t rewardCount = 0;
    if (!reader.Word(rewardCount) || rewardCount > 32) return fail("invalid prepared typing reward count");
    for (uint32_t i = 0; i < rewardCount; ++i) {
        uint32_t item = 0, model = 0, sprite = 0, bits[3] = {};
        if (!reader.Word(item) || !reader.Word(model) || !reader.Word(sprite) || item > 32 || model >= 32768 ||
            !loaded.SourceSprite(int(sprite)) || !reader.Word(bits[0]) || !reader.Word(bits[1]) || !reader.Word(bits[2]))
            return fail("invalid prepared typing reward metadata");
        TypingReferenceReward reward;
        reward.model = int(model); reward.sprite = int(sprite);
        memcpy(&reward.x, &bits[0], 4); memcpy(&reward.y, &bits[1], 4); memcpy(&reward.scale, &bits[2], 4);
        if (!(reward.scale > 0 && reward.scale < 10)) return fail("invalid prepared typing reward scale");
        loaded.rewards_[int(item)] = reward;
    }
    uint32_t modelCount = 0;
    if (!reader.Word(modelCount) || modelCount > 16384) return fail("invalid prepared typing model map");
    for (uint32_t i = 0; i < modelCount; ++i) {
        uint32_t source = 0, mapped = 0;
        if (!reader.Word(source) || !reader.Word(mapped) || source >= 16384 || mapped >= 32768)
            return fail("invalid prepared typing model mapping");
        loaded.modelMap_[int(source)] = int(mapped);
    }
    uint32_t soundCount = 0, soundFiles = 0;
    if (!reader.Word(soundCount) || soundCount > 8192) return fail("invalid prepared typing sound count");
    auto audioPath = [](const std::string& path) {
        return (path.empty() || path.rfind("Sound/",0) == 0 || path.rfind("TypingSound/",0) == 0) &&
            path.find("..") == std::string::npos && path.find_first_of("\\:") == std::string::npos;
    };
    for (uint32_t i = 0; i < soundCount; ++i) {
        uint32_t id = 0, flags = 0;
        std::vector<uint8_t> text;
        if (!reader.Word(id) || (id >> 28 != 0 && id >> 28 != 1 && id >> 28 != 2 && id >> 28 != 9) ||
            !reader.Word(flags) || flags > 3 || !reader.Blob(text))
            return fail("invalid prepared typing sound metadata");
        std::string path(text.begin(),text.end());
        if (!audioPath(path)) return fail("invalid prepared typing sound path");
        loaded.sounds_[id] = {std::move(path),flags};
    }
    uint32_t nativeSoundCount = 0;
    if (!reader.Word(nativeSoundCount) || nativeSoundCount > 8192) return fail("invalid prepared typing native sound map");
    for (uint32_t i = 0; i < nativeSoundCount; ++i) {
        uint32_t native = 0, source = 0;
        if (!reader.Word(native) || !reader.Word(source) || !loaded.Sound(source)) return fail("invalid prepared typing native sound identity");
        loaded.nativeSounds_[native] = source;
    }
    if (!reader.Word(soundFiles) || soundFiles > 2048) return fail("invalid prepared typing sound file count");
    for (uint32_t i = 0; i < soundFiles; ++i) {
        std::vector<uint8_t> name, data;
        if (!reader.Blob(name)) return fail("truncated prepared typing sound name");
        std::string path(name.begin(),name.end());
        if (!audioPath(path) || path.rfind("TypingSound/",0) != 0) return fail("invalid prepared typing sound file path");
        if (loadAudioData) {
            if (!reader.Blob(data)) return fail("truncated prepared typing sound payload");
            loaded.soundData_.emplace(std::move(path),std::move(data));
        } else if (!reader.SkipBlob()) return fail("truncated prepared typing sound payload");
    }
    std::vector<uint8_t> choices, dialogue, records, segments, glyphs;
    if (!reader.Blob(choices) || choices.size() != 732*2 || !reader.Blob(dialogue) || dialogue.size() != 402*16 ||
        !reader.Blob(records) || records.size() != 375*64 || !reader.Blob(segments) || segments.size() != 402*8 ||
        !reader.Blob(glyphs) || glyphs.size() != 128*2) return fail("invalid prepared typing dialogue metadata");
    loaded.dialogueChoices_.resize(732); memcpy(loaded.dialogueChoices_.data(),choices.data(),choices.size());
    loaded.subtitleGlyphs_.resize(128); memcpy(loaded.subtitleGlyphs_.data(),glyphs.data(),glyphs.size());
    for (size_t i = 0; i < 402; ++i) {
        TypingReferenceDialogue line;
        int16_t duration = 0;
        memcpy(&duration,dialogue.data()+i*16+2,2); line.duration = duration;
        memcpy(&line.sound,dialogue.data()+i*16+12,4);
        for (size_t k = 0; k < 4; ++k) {
            int16_t record = 0; memcpy(&record,segments.data()+i*8+k*2,2);
            if (record < -1 || record >= 375) return fail("invalid prepared typing subtitle segment");
            line.records[k] = record;
        }
        loaded.dialogue_.push_back(line);
    }
    for (size_t i = 0; i < 375; ++i) {
        TypingReferenceSubtitle record;
        int16_t threshold = 0;
        memcpy(&record.x,records.data()+i*64,4); memcpy(&threshold,records.data()+i*64+62,2); record.threshold = threshold;
        const char* begin = reinterpret_cast<const char*>(records.data()+i*64+4);
        record.text.assign(begin, std::find(begin,begin+58,'\0'));
        loaded.subtitles_.push_back(std::move(record));
    }
    uint32_t enemyModelCount = 0;
    if (!reader.Word(enemyModelCount) || enemyModelCount > 32768)
        return fail("invalid prepared typing enemy model binding count");
    for (uint32_t i = 0; i < enemyModelCount; ++i) {
        uint32_t kind = 0, node = 0, native = 0, mapped = 0;
        if (!reader.Word(kind) || !reader.Word(node) || !reader.Word(native) || !reader.Word(mapped) ||
            (kind != 0xFFFFFFFFu && kind >= 86) || (node != 0xFFFFFFFFu && node >= 32) ||
            native >= 16384 || mapped >= 32768)
            return fail("invalid prepared typing enemy model binding");
        const std::array<int,3> key{static_cast<int32_t>(kind),static_cast<int32_t>(node),int(native)};
        if (!loaded.enemyModels_.emplace(key,int(mapped)).second)
            return fail("duplicate prepared typing enemy model binding");
    }
    if (reader.at != reader.data.size()) return fail("unexpected bytes after prepared typing reference");
    loaded.soundTestEffects_.assign(kTypingOptionEffects.begin(),kTypingOptionEffects.end());
    loaded.soundTestMusic_.assign(kTypingOptionMusic.begin(),kTypingOptionMusic.end());
    for (const auto* selector : {&loaded.soundTestEffects_,&loaded.soundTestMusic_})
        for (uint32_t id : *selector)
            if ((id >> 28) <= 2 && !loaded.Sound(id)) return fail("typing option sound has no prepared recording");
    loaded.available_ = true;
    *this = std::move(loaded);
    return true;
}

bool TypingReference::ApplyStage(int mode, EventMemory& memory, CameraBank& cameras, std::string* error) const {
    auto fail = [&](const std::string& text) { if (error) *error = text; return false; };
    if (!Complete()) {
        return fail(!Available() ? "original typing campaign data has not been prepared" :
                    "typing campaign conversion is incomplete (" + std::to_string(issues_.size()) +
                    " unresolved fields); first: " + issues_.front());
    }
    const auto found = stages_.find(mode);
    if (found == stages_.end()) return fail("prepared typing campaign has no chapter " + std::to_string(mode + 1));
    for (const CameraData& camera : cameras_)
        if (!cameras.AddPreparedFile(camera.bytes, camera.ids)) return fail("invalid prepared typing camera curves");
    memory.Clear();
    memory.Load(EventMemory::kCommonBase, stageCommon_.at(mode));
    memory.Load(EventMemory::kStageBase, found->second);
    if (const auto* routes = SceneRoutes(mode)) memory.SetSceneRoutes(mode == 9 ? 11 : mode, *routes);
    memory.SetTypingDialect(true);
    return true;
}

const Motion* TypingReference::ExtraMotion(int id) const {
    const auto found = motions_.find(id);
    return found == motions_.end() ? nullptr : &found->second;
}

const TypingReferenceResource* TypingReference::Resource(uint32_t file, uint32_t kind) const {
    for (const auto& r : resources_) if (r.file == file && r.kind == kind) return &r;
    return nullptr;
}
int TypingReference::ModelFile(int model) const {
    if (model < 0x4000) return -1;
    for (const auto& r : resources_) if (r.kind == 0)
        for (size_t i = 0; i < r.models.size(); ++i)
            if (r.models[i] == model && r.canonicalModels[i]) return int(r.file);
    return -1;
}
const std::vector<int>* TypingReference::PolModels(uint32_t file) const {
    const auto* resource = Resource(file, 0);
    return resource ? &resource->models : nullptr;
}
const std::vector<uint8_t>* TypingReference::SceneRoutes(int mode) const {
    const auto route = routes_.find(mode);
    return route == routes_.end() ? nullptr : &route->second;
}

uint32_t TypingReference::CivilianScript(int index) const {
    return index >= 0 && static_cast<size_t>(index) < civilianScripts_.size() ? civilianScripts_[index] : 0;
}

bool TypingReference::CivilianWord(uint32_t address, uint32_t* value) const {
    if (!value || address < civilianBase_ || static_cast<size_t>(address - civilianBase_) > civilianData_.size() ||
        civilianData_.size() - static_cast<size_t>(address - civilianBase_) < sizeof(*value)) return false;
    memcpy(value, civilianData_.data() + (address - civilianBase_), sizeof(*value));
    return true;
}

const TypingReferenceSprite* TypingReference::SourceSprite(int id) const {
    if (id < 0x4000 || static_cast<size_t>(id - 0x4000) >= sprites_.size()) return nullptr;
    return &sprites_[static_cast<size_t>(id - 0x4000)];
}

const TypingReferenceReward* TypingReference::Reward(int item) const {
    const auto found = rewards_.find(item);
    return found == rewards_.end() ? nullptr : &found->second;
}

int TypingReference::SourceModel(int id) const {
    const auto found = modelMap_.find(id); return found == modelMap_.end() ? 0 : found->second;
}
int TypingReference::NativeEnemyModel(int kind, int node, int model) const {
    for (const std::array<int,3>& key : {std::array<int,3>{kind,node,model},
                                      std::array<int,3>{kind,-1,model},
                                      std::array<int,3>{-1,-1,model}}) {
        const auto found = enemyModels_.find(key);
        if (found != enemyModels_.end()) return found->second;
    }
    return model;
}
const TypingReferenceSound* TypingReference::Sound(uint32_t id) const {
    const auto found = sounds_.find(id); return found == sounds_.end() ? nullptr : &found->second;
}
bool TypingReference::NativeSound(uint32_t id, uint32_t* source) const {
    const auto found = nativeSounds_.find(id);
    if (!source || found == nativeSounds_.end()) return false;
    *source = found->second; return true;
}
const std::vector<uint8_t>* TypingReference::SoundData(const std::string& path) const {
    const auto found = soundData_.find(path); return found == soundData_.end() ? nullptr : &found->second;
}
const TypingReferenceDialogue* TypingReference::Dialogue(int voice, int player) const {
    if (voice < 0 || player < 0 || player > 2 || size_t(voice)*3+size_t(player) >= dialogueChoices_.size()) return nullptr;
    const int group = dialogueChoices_[size_t(voice)*3+size_t(player)];
    return group > 0 && size_t(group) < dialogue_.size() ? &dialogue_[size_t(group)] : nullptr;
}
const TypingReferenceSubtitle* TypingReference::Subtitle(int record) const {
    return record >= 0 && size_t(record) < subtitles_.size() ? &subtitles_[size_t(record)] : nullptr;
}
int TypingReference::SubtitleGlyph(int c) const {
    if (c == '~') return 0x69F;
    return c >= 0 && size_t(c) < subtitleGlyphs_.size() ? subtitleGlyphs_[size_t(c)] : 0;
}

} // namespace game
