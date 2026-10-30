#include "game/typing_words.h"
#include <algorithm>
#include <array>

namespace game {
namespace {
#include "game/typing_word_data.inc"
}

std::span<const TypingWordBank> TypingWordBanks() { return kBanks; }
std::span<const TypingBuiltinBank> TypingBuiltinBanks() { return kBuiltinBanks; }
std::span<const uint32_t> TypingBonusBankLevels(int chapter) {
    return chapter <= 3 ? std::span<const uint32_t>(kBonusBanksEarly) : std::span<const uint32_t>(kBonusBanksLate);
}
std::span<const TypingWordRecord> TypingWords(const TypingWordBank& b) {
    return std::span(kWords).subspan(b.first, b.count);
}
std::span<const TypingWordRecord> TypingWords(const TypingBuiltinBank& b) {
    return std::span(kWords).subspan(b.first, b.count);
}
const TypingWordBank* FindTypingWordBank(int group, int level) {
    for (const auto& bank : kBanks) if (bank.group == group && bank.level == level) return &bank;
    return nullptr;
}
const TypingWordBank* FindTypingWordBank(std::string_view name) {
    for (const auto& bank : kBanks) if (bank.name == name) return &bank;
    return nullptr;
}
const TypingBuiltinBank* FindTypingBuiltinBank(std::string_view name) {
    for (const auto& bank : kBuiltinBanks) if (bank.name == name) return &bank;
    return nullptr;
}

int TypingWordBankLevel(int selector, int dynamicRank, int minimum, int maximum) {
    // Original adaptive word difficulty has 16 rank entries. The two smallest
    // ranges use additive offsets; the remaining ranges use a multiplier.
    constexpr std::array<float,16> adjustment = {
        -.4f,-.3f,-.2f,-.15f,-.1f,-.05f,0,.05f,.1f,.1f,.15f,.15f,.2f,.2f,.25f,.3f};
    constexpr std::array<int,16> low = {-10,-5,-5,-5,0,0,0,0,5,5,10,10,15,15,20,20};
    constexpr std::array<int,16> middle = {-10,-10,-5,-5,0,0,0,5,5,10,10,15,15,20,20,25};
    const int rank = std::clamp(dynamicRank, 0, 15);
    float value = selector * .1f;
    if (value <= 15) value += low[rank];
    else if (value <= 30) value += middle[rank];
    else {
        const float scale = value <=45 ? 1.5f : value<=65 ? 1.4f : value<=85 ? 1.3f :
                            value<=120 ? 1.2f : value<=150 ? 1.0f : value<=180 ? .9f :
                            value<=210 ? .8f : value<=240 ? .7f : .6f;
        value *= 1.0f + adjustment[rank] * scale;
    }
    value = std::clamp(value, float(minimum), float(maximum));
    return value <= 3.0f ? 0 : std::min(19, int((value + 5.0f) * .1f));
}
} // namespace game
