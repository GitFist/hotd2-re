#include "game/typing_reference.h"
#include "game/model.h"
#include "game/prs.h"
#include "game/sound_table.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
void Require(bool condition, const std::string& message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message.c_str()); std::exit(1); }
}
std::vector<uint8_t> Decode(const std::vector<uint8_t>& raw) {
    uint32_t size = 0;
    if (raw.size() >= 8) memcpy(&size,raw.data(),4);
    if (size && size <= (64u << 20)) {
        std::vector<uint8_t> out(size);
        if (game::DecompressPrsSafe(raw.data()+4,raw.size()-4,out.data(),out.size()) == int(size)) return out;
    }
    return raw;
}
}

int main(int argc, char** argv) {
    Require(argc == 2, "usage: typing_reference_test <prepared-bank>");
    game::TypingReference reference;
    std::string error;
    Require(reference.Load(argv[1], &error), error);
    size_t models = 0, textures = 0;
    Require(!reference.Resources().empty() && !reference.Palettes().empty(), "prepared resources and palettes present");
    Require(reference.SourceSpriteCount() == 0xFD5 && !reference.SourceSprite(0x3FFF) && !reference.SourceSprite(0x4FD5),
            "original sprite table, prepared name glyphs and clean wordmark have bounded namespaced ids");
    const auto* wordmark=reference.SourceSprite(0x4FD4);
    const auto* wordmarkResource=wordmark?reference.Resource(wordmark->file,1):nullptr;
    Require(wordmarkResource && wordmarkResource->textures.size()==1 &&
            wordmarkResource->textures[0].width==512 && wordmarkResource->textures[0].height==320,
            "the character-free Typing wordmark is embedded in the prepared bank");
    for (int id = 0; id < 0xF74; ++id) {
        const auto* sprite = reference.SourceSprite(0x4000 + id);
        Require(sprite && reference.Resource(sprite->file, 1), "each original sprite uses a prepared texture bank");
    }
    const auto* glyph=reference.SourceSprite(0x4F74+'A'-32);
    const auto* glyphResource=glyph ? reference.Resource(glyph->file,1) : nullptr;
    Require(glyphResource && glyphResource->textures.size()==1 && glyphResource->textures[0].width==512 &&
            glyphResource->textures[0].height==256 && glyphResource->textures[0].id==glyph->texture,
            "prepared name-entry glyphs use their self-contained original-font atlas");
    Require(reference.SourceModel(0x129E) > 0 && reference.SourceModel(-1) == 0,
            "source HUD models have an exact bounded identity map");
    Require(reference.NativeEnemyModel(0x4A,1,0x440) == reference.SourceModel(0x471) &&
            reference.NativeEnemyModel(0x4A,2,0x3CE) == reference.SourceModel(0x3FF) &&
            reference.NativeEnemyModel(0x4A,5,0x444) == reference.SourceModel(0x475) &&
            reference.NativeEnemyModel(0x4A,8,0x442) == reference.SourceModel(0x473),
            "Strength's body, helmet, chainsaw and carried hand use source model identities");
    Require(reference.NativeEnemyModel(0x49,1,0x392) == reference.SourceModel(0x3C4) &&
            reference.NativeEnemyModel(0x49,9,0x392) == reference.SourceModel(0x3C3) &&
            reference.NativeEnemyModel(0x49,1,0x392) != reference.NativeEnemyModel(0x49,9,0x392),
            "Tower's shared native body model retains separate source dragon-half bindings");
    Require(reference.NativeEnemyModel(0x48,1,0x2CB) == reference.SourceModel(0x2FC) &&
            reference.NativeEnemyModel(0x48,1,0x315) == reference.SourceModel(0x346),
            "Hierophant's dynamic body and chest frames use verified source ranges");
    Require(reference.SoundFiles().size() == 334, "all source-only and changed sound payloads are prepared");
    Require(reference.SoundTestEffects().size() == 1039 && reference.SoundTestMusic().size() == 39,
            "typing sound test exposes the original mixed voice/effect and selected music slots");
    Require(reference.SoundTestEffects()[0] == 0x80000000u &&
            reference.SoundTestEffects()[1] == 0x20000131u && reference.SoundTestEffects()[2] == 0x116A9u &&
            reference.SoundTestMusic()[0] == 0x80000000u && reference.SoundTestMusic()[1] == 0x1000001Au,
            "typing option slots retain No0's stop command and the source's custom selector order");
    for (const auto& [path, data] : reference.SoundFiles()) {
        game::WavInfo wave;
        Require(game::ParseWavHeader(data.data(),data.size(),wave) && wave.formatTag == 1 &&
                wave.channels >= 1 && wave.channels <= 2 && wave.sampleRate > 0 &&
                wave.dataOffset <= data.size() && wave.dataSize <= data.size()-wave.dataOffset,
                path+": prepared sound contains a bounded PCM recording");
    }
    game::SoundTable audio;
    audio.SetTypingReference(&reference);
    for (uint32_t id : {0x836A9u,0x20000146u,0x20000147u,0x20000137u}) {
        const auto request = game::ResolveSound(audio,id|game::kTypingSoundFlag);
        Require(request.action == game::SoundAction::PlayFile && !request.path.empty(), "wave cue resolves through source sound metadata");
        if (request.path.rfind("TypingSound/",0) == 0)
            Require(reference.SoundData(request.path) != nullptr, "wave cue needs no original installation at runtime");
    }
    Require(reference.Sound(0x20000146)->path.find("start44.wav") != std::string::npos &&
            reference.Sound(0x20000137)->path.find("failed44.wav") != std::string::npos,
            "source countdown/failure cues cannot resolve to unrelated native story voices");
    Require(game::ResolveSound(audio,0x836A9u).action == game::SoundAction::None,
            "native sound lookup is unchanged by the prepared overlay");
    uint32_t mappedSound = 0;
    Require(reference.NativeSound(0x2000003Eu,&mappedSound) && mappedSound == 0x20000147u,
            "native actor cue maps to the source slot by recording identity");
    Require(reference.NativeSound(0x20000147u,&mappedSound) && mappedSound != 0x20000147u,
            "native and source voice indices cannot be confused by a global table swap");
    const auto music = game::ResolveSound(audio,0x10000014u|game::kTypingSoundFlag,{6,1});
    Require(music.action == game::SoundAction::PlayFile && music.loop && music.channel == game::kSndBgmChannel,
            "source music keeps its original looping contract");
    Require(reference.Dialogue(207,0) && reference.Dialogue(207,0)->duration == 120 &&
            !reference.Dialogue(-1,0) && !reference.Dialogue(244,0), "source dialogue selector includes its inserted line and bounds");
    const auto* subtitleGlyph = reference.SourceSprite(0x4000 + reference.SubtitleGlyph('T'));
    const auto* subtitleFont = subtitleGlyph ? reference.Resource(subtitleGlyph->file,1) : nullptr;
    bool fontPalette = false;
    if (subtitleFont) for (size_t i = 0; i < subtitleFont->textures.size(); ++i)
        if (subtitleFont->textures[i].id == subtitleGlyph->texture)
            fontPalette = subtitleFont->texturePalettes[i] == 1 &&
                          reference.Palettes()[1][8] == 0xFFFF;
    Require(fontPalette, "English captions use uploaded source palette1 (inner selection0 plus loader offset)");
    const auto* wordFont = reference.Resource(0x10000 + 234,1); // tod_boxetc.bin
    bool wordPalette = wordFont && !wordFont->texturePalettes.empty();
    if (wordFont) for (uint8_t palette : wordFont->texturePalettes) wordPalette &= palette == 0;
    Require(wordPalette, "3D word glyphs and meter labels use the model loader's default source palette0");
    for (const auto& resource : reference.Resources()) {
        const auto bytes = Decode(resource.bytes);
        if (resource.kind == 0) {
            const auto headers = game::ReadPolHeader(bytes.data(),bytes.size());
            Require(headers.size() == resource.models.size(), resource.name+": model count matches imported identities");
            for (size_t index = 0; index < headers.size(); ++index) {
                game::Model model;
                Require(game::ParseModel(bytes.data(),bytes.size(),headers[index],model,&error),resource.name+": "+error);
                if (resource.models[index] >= 0x4000 && resource.canonicalModels[index])
                    Require(reference.ModelFile(resource.models[index]) == int(resource.file), "model id resolves to its prepared file");
                ++models;
            }
        } else for (size_t i = 0; i < resource.textures.size(); ++i) {
            const auto& desc = resource.textures[i];
            const auto palette = resource.texturePalettes[i];
            Require(palette < reference.Palettes().size(), resource.name+": palette bank exists");
            uint16_t colours[16];
            for (int k = 0; k < 16; ++k)
                colours[k] = game::ConvertPaletteEntry(reference.Palettes()[palette][k],game::PixelFormatOf(desc));
            std::vector<uint16_t> texels;
            Require(game::DecodeTexels(bytes.data(),bytes.size(),desc,colours,texels),resource.name+": texture data decodes");
            std::vector<uint8_t> rgba(texels.size()*4);
            Require(game::TexelsToRgba(texels.data(),texels.size(),game::PixelFormatOf(desc),rgba.data()),
                    resource.name+": texture format supported");
            ++textures;
        }
    }
    for (int chapter = 0; chapter < 6; ++chapter) {
        const auto* route = reference.SceneRoutes(chapter);
        Require(route && !route->empty() && route->size()%8 == 0,"all six chapter routes prepared");
    }
    game::EventMemory stageMemory;
    game::CameraBank stageCameras;
    uint32_t riseMotion = 0;
    Require(reference.ApplyStage(0, stageMemory, stageCameras, &error), error);
    Require(stageMemory.Read(0x0097AFD4, &riseMotion) && riseMotion == 0x128,
            "stage-one leaper behavior19 uses the converted authored rise motion");
    uint32_t riderCleanup = 0, mountCleanup = 0;
    Require(stageMemory.Read(0x0097DDC4, &riderCleanup) && (riderCleanup >> 16) == 0x4041 &&
            stageMemory.Read(0x0097DDFC, &mountCleanup) && (mountCleanup >> 16) == 0x4041,
            "Judgment rider and its nested mount use the prepared cleanup camera");
    game::EventMemory endingMemory;
    game::CameraBank endingCameras;
    Require(reference.ApplyStage(9, endingMemory, endingCameras, &error), error);
    bool sourceEndingCamera = false;
    game::EventOp endingHalt;
    Require(game::DecodeEvent(endingMemory, endingMemory.Script(9, 0, 0), endingHalt) && endingHalt.opcode == 0x4E,
            "source common mode9 remains the stage-end halt");
    uint32_t endingScript = endingMemory.Script(11, 0, 1);
    for (int guard = 0; guard < 16 && endingScript; ++guard) {
        game::EventOp op;
        Require(game::DecodeEvent(endingMemory, endingScript, op), "prepared ending script decodes");
        if (op.opcode == 0x30 && op.args.size() >= 4 && op.args[0] == 0x40) {
            sourceEndingCamera = op.args[3] == 0x4101;
            break;
        }
        endingScript += op.length;
    }
    int16_t endingDestination = 0;
    Require(endingMemory.TypingDialect() && sourceEndingCamera && reference.SceneRoutes(9) &&
            endingMemory.SceneRoute(11, 1, &endingDestination) && endingDestination == 3,
            "ending mode retains source dialect, original camera101 and its authored routes");
    Require(reference.CivilianScript(-1) == 0 && reference.CivilianScript(67) == 0,
            "civilian script index bounds are enforced");
    uint32_t ignored = 0;
    Require(!reference.CivilianWord(0x01FFFFFF, &ignored) && !reference.CivilianWord(0x02100000, &ignored),
            "civilian data reads cannot escape their prepared bank");
    bool sourceCamera = false;
    for (int index = 0; index < 67; ++index) {
        uint32_t address = reference.CivilianScript(index);
        Require(address >= 0x02000000, "all original civilian scripts have prepared entries");
        bool ended = false;
        for (int guard = 0; guard < 4096; ++guard) {
            uint32_t op = 0, operand = 0;
            Require(reference.CivilianWord(address, &op) && op <= 0x2D &&
                    reference.CivilianWord(address+4, &operand), "civilian command operands stay within the bank");
            if (op == 0x2D) { ended = true; break; }
            if (index == 2 && op == 0xD && operand == 0x4000 + 60) sourceCamera = true;
            uint32_t length = 8;
            switch (op) {
            case 1: length = 16; break;
            case 0: case 5: case 0xD: case 0x13: case 0x16: case 0x1A: case 0x1F:
            case 0x21: case 0x24: case 0x25: case 0x26: length = 12; break;
            case 0x10: length = operand == 0x0048DB90 ? 12 : operand == 0x0048DBD0 ? 20 : 8; break;
            case 0x2B: length = 24; break;
            default: break;
            }
            address += length;
        }
        Require(ended, "each original civilian program reaches its authored terminator");
    }
    Require(sourceCamera, "civilian program uses original camera 60 rather than HOD2 camera 44");
    uint32_t alternate = 0, alternateCamera = 0;
    Require(reference.CivilianWord(reference.CivilianScript(0)+24, &alternate) &&
            reference.CivilianWord(alternate+24, &alternateCamera) && alternateCamera == 0x4037,
            "civilian alternate program camera wait uses the prepared camera namespace");
    std::printf("typing_reference_test: %zu models, %zu textures, six campaign routes and ending passed; %zu unresolved conversions\n",
                models,textures,reference.Issues().size());
}
