#pragma once
#include <cstdint>
#include <span>
#include <string_view>

namespace game {

struct TypingWordRecord {
    uint16_t measure, flags;
    std::string_view text, glyphs; // FF glyph markers retain original emphasis.
};
struct TypingWordBank {
    std::string_view name;
    int group, level; // The original group and one-based bank selector.
    uint32_t first, count;
};
struct TypingBuiltinBank {
    std::string_view name;
    uint32_t first, count;
};

std::span<const TypingWordBank> TypingWordBanks();
std::span<const TypingBuiltinBank> TypingBuiltinBanks();
std::span<const TypingWordRecord> TypingWords(const TypingWordBank& bank);
std::span<const TypingWordRecord> TypingWords(const TypingBuiltinBank& bank);
const TypingWordBank* FindTypingWordBank(int group, int level);
const TypingWordBank* FindTypingWordBank(std::string_view name);
const TypingBuiltinBank* FindTypingBuiltinBank(std::string_view name);
std::span<const uint32_t> TypingBonusBankLevels(int chapter);

// The supplied PC build's alphabet-mode selector conversion. Dynamic rank is
// a separate original game value; it is not the five-position options setting.
int TypingWordBankLevel(int selector, int dynamicRank, int minimum = 0, int maximum = 200);

} // namespace game
