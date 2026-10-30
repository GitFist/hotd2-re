#include "game/world_internal.h"
#include "game/sound_table.h"

namespace game {
namespace {
struct TypingDialogueTask : WTask {
    const TypingReferenceDialogue* dialogue = nullptr;
    int remaining = 0, segment = 0;
};
void DialogueTask(Task& task) {
    auto& world = *CurrentWorld();
    auto& voice = task.As<TypingDialogueTask>();
    if (--voice.remaining == 0 || world.runner->skipping || world.skip2230 != 0) {
        // Source4233F4 enters the task terminator/longjmp4BDCE0; no final
        // subtitle submission follows an expiry or cutscene skip.
        world.ts.KillCurrent();
        return;
    }
    const auto* record = world.typingReference.Subtitle(voice.dialogue->records[voice.segment]);
    if (!record) return;
    if (voice.remaining < record->threshold && voice.segment < 3)
        record = world.typingReference.Subtitle(voice.dialogue->records[++voice.segment]);
    if (!record) return;
    // Source English subtitle renderer 0x00423B30: no native lowercase baseline
    // offsets; the source glyph images themselves carry their authored layout.
    float x = 320.0f - float(record->text.size())*5.6f + record->x;
    for (unsigned char c : record->text) {
        const int glyph = world.typingReference.SubtitleGlyph(c);
        if (glyph != 0) {
            const size_t before = world.hudSprites.size();
            world.TypingSprite(glyph, x, 392, 1, 1, 0, 0x004233D0);
            if (world.hudSprites.size() != before) {
                world.hudSprites.back().sx = .7f;
                world.hudSprites.back().sy = .7f;
            }
        }
        x += 11.2f;
    }
}
uint32_t SourceId(uint32_t id) {
    const uint32_t kind = id >> 28;
    return kind <= 2 ? id | kTypingSoundFlag : id;
}
}

void TypingWordCancelAudio(World::Impl& world, bool hadSelection) {
    if (!world.cfg.portTyping || !world.typingReferenceActive || !hadSelection) return;
    // Source 0x4B1683 requires a selected word before either Esc or the
    // prop-window cancellation path reaches 0x4B16B6.
    world.TypingSound(0x003E16A9u);
}

void TypingWordCompletionAudio(World::Impl& world, uint32_t actorFlags, int player) {
    if (!world.cfg.portTyping || !world.typingReferenceActive || player < 0 || player > 1) return;
    // Source 0x4B20C2/0x4B21B6/0x4B2301 take flags 2, 1 and 4 first.
    // Only the remaining flag8 completion path (0x4B2378) emits this cue.
    // Ordinary accepted letters and initial word selection emit no sound.
    if ((actorFlags & 0xFu) == 8)
        world.TypingSound(player == 0 ? 0x003416A9u : 0x003316A9u);
}

void World::Impl::TypingSound(uint32_t id) {
    Sound(cfg.portTyping && typingReferenceActive ? SourceId(id) : id);
}
void World::Impl::TypingSoundRepeat(uint32_t id, int count, int interval) {
    SpawnSoundRepeat(cfg.portTyping && typingReferenceActive ? SourceId(id) : id, count, interval);
}
void World::Impl::TypingVoice(int id) {
    if (!cfg.portTyping || !typingReferenceActive) { Voice(id); return; }
    if (runner->skipping || skip2230 != 0) return;
    const int player = gameMode == 10 || gameMode == 11 ? 0 : playersInGame;
    const auto* dialogue = typingReference.Dialogue(id, player);
    if (!dialogue || dialogue->duration <= 0) return;
    if (dialogue->sound) TypingSound(dialogue->sound);
    auto* voice = NewTask<TypingDialogueTask>(DialogueTask, 0x004233D0);
    voice->dialogue = dialogue;
    voice->remaining = dialogue->duration;
}
} // namespace game
