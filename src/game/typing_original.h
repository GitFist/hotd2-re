#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace game {

// English PC item identities. IDs1..16 are immediate pickups; only17..21
// enter the four function-key boxes (source454200/4ACF30).
enum class TypingOriginalItem : uint8_t {
    None = 0, GoldTranquilizer = 17, HandOfGod = 18,
    MolotovCocktail = 19, SulphuricAcid = 20, PainKiller = 21
};
inline std::string_view TypingOriginalItemName(TypingOriginalItem item) {
    switch (item) {
    case TypingOriginalItem::GoldTranquilizer: return "GOLD TRANQUILIZER";
    case TypingOriginalItem::HandOfGod: return "HAND OF GOD";
    case TypingOriginalItem::SulphuricAcid: return "SULPHURIC ACID";
    case TypingOriginalItem::MolotovCocktail: return "MOLOTOV COCKTAIL";
    case TypingOriginalItem::PainKiller: return "PAIN KILLER";
    default: return "EMPTY";
    }
}

struct TypingOriginalProgress {
    std::array<uint8_t,6> chapterCoins{};
    unsigned coins() const {
        unsigned result = 0;
        for (uint8_t mask : chapterCoins) result += std::popcount(unsigned(mask & 31));
        return result;
    }
    // Source49F800 awards one cumulative unlock at each five-coin boundary.
    uint8_t unlocks() const {
        constexpr uint8_t flags[] = {1,8,2,32,4,16};
        uint8_t result = 0;
        for (unsigned tier=0;tier<std::min(6u,coins()/5);++tier) result |= flags[tier];
        return result;
    }
};

// Source49FD40/5E4808..4821: chapter-specific achievement requirements.
inline uint8_t TypingOriginalChapterCoins(int chapter, uint64_t points,
    unsigned accuracy, unsigned rankAs, bool usedContinue) {
    constexpr unsigned scores[] = {3000,4000,3500,3500,4000,4000};
    constexpr unsigned accuracies[] = {80,85,90,90,93,95};
    constexpr unsigned ranks[] = {20,30,30,30,30,30};
    if (chapter<1 || chapter>6) return 0;
    const unsigned index=unsigned(chapter-1);
    return uint8_t(1u | (points>=scores[index]?2u:0u) |
        (accuracy>=accuracies[index]?4u:0u) | (rankAs>=ranks[index]?8u:0u) |
        (!usedContinue?16u:0u));
}

inline int TypingOriginalCharacterKind(uint32_t flags,int kind) {
    if (kind!=0x39 && kind!=0x3A) return kind;
    if (flags&64) return kind==0x39?7:8;
    if (flags&128) return kind==0x39?0x34:0x21;
    return kind;
}

inline std::array<float,3> TypingOriginalNodeScale(uint32_t flags,int kind,bool civilian,int node) {
    if (!(flags&2)) return {1,1,1};
    if (civilian) {
        if (node==2) return {1.5f,1,1.5f};
        if (node==5 || node==8 || node==12 || node==15) return {2,2,2};
        return {1,1,1};
    }
    if (node!=2) return {1,1,1};
    return kind==14?std::array<float,3>{1.8f,1.8f,1}:std::array<float,3>{2,2,2};
}

class TypingOriginalState {
public:
    void reset(uint8_t unlocks=0) {
        slots_={}; handFrames_=painFrames_=0; frameRemainder_=0; usedContinue_=false; newlyEarned_=0;
        sourceFlags_=temporaryScenes_=0;
        unlimitedHand_=(unlocks&16)!=0;
        // Source4834F7: ten coins begin with two Molotovs; thirty coins
        // reserve the first slot for the repeatedly restored Hand of God.
        if (unlocks&8) {
            slots_[1]=TypingOriginalItem::MolotovCocktail;
            slots_[(unlocks&16)?2:0]=TypingOriginalItem::MolotovCocktail;
        }
        replenishHand();
    }
    void beginChapter() { usedContinue_=false; newlyEarned_=0; }
    void noteContinue() { usedContinue_=true; }
    bool usedContinue() const { return usedContinue_; }
    bool collect(int item) {
        if (item<17 || item>21) return false;
        const auto value=static_cast<TypingOriginalItem>(item);
        const auto free=std::find(slots_.begin(),slots_.end(),TypingOriginalItem::None);
        if (free!=slots_.end()) *free=value;
        else { std::move(slots_.begin()+1,slots_.end(),slots_.begin());slots_.back()=value; }
        return true;
    }
    bool immediate(int item) {
        switch (item) {
        case 5: sourceFlags_|=2;break;  // Meat: enlarged heads.
        case 6: sourceFlags_|=4;break;  // Genre dictionary.
        case 11: sourceFlags_|=8;temporaryScenes_+=2;break; // Alien dictionary.
        case 12: sourceFlags_|=16;break;
        case 13: sourceFlags_|=32;break; // Hamburger: muted weapon sound.
        case 14: sourceFlags_=(sourceFlags_&~128u)|64;break;
        case 15: sourceFlags_=(sourceFlags_&~64u)|128;break;
        default: return false;
        }
        return true;
    }
    uint32_t sourceFlags() const { return sourceFlags_; }
    void clearSourceFlags(uint32_t mask) { sourceFlags_&=~mask; }
    bool genreDictionary() const { return (sourceFlags_&4)!=0; }
    bool alienDictionary() const { return (sourceFlags_&8)!=0; }
    void finishGenreDictionary() { sourceFlags_&=~4u; }
    void noteRankEffect() { temporaryScenes_+=2; }
    void sceneClear() {
        // Source40B120 restores transient item flags when its shared scene
        // counter expires; permanent presentation flags and genre survive.
        if (temporaryScenes_ && --temporaryScenes_==0) sourceFlags_&=0xE6u;
    }
    TypingOriginalItem use(unsigned slot,bool hasTarget) {
        if (slot>=slots_.size() || handFrames_ || painFrames_) return TypingOriginalItem::None;
        const auto item=slots_[slot];
        if ((item==TypingOriginalItem::SulphuricAcid || item==TypingOriginalItem::MolotovCocktail) && !hasTarget)
            return TypingOriginalItem::None;
        if (item==TypingOriginalItem::HandOfGod) handFrames_=900;
        if (item==TypingOriginalItem::PainKiller) painFrames_=900;
        if (item==TypingOriginalItem::GoldTranquilizer) noteRankEffect();
        slots_[slot]=TypingOriginalItem::None;
        return item;
    }
    void updateFrames(unsigned frames) {
        handFrames_-=std::min(handFrames_,frames);
        painFrames_-=std::min(painFrames_,frames);
        replenishHand();
    }
    void updateSeconds(double seconds) {
        if (!std::isfinite(seconds) || seconds<=0) return;
        frameRemainder_+=std::min(seconds,60.0)*60.0;
        const unsigned frames=unsigned(frameRemainder_+1e-8);
        frameRemainder_-=frames;
        updateFrames(frames);
    }
    bool anyKeyCorrect() const { return handFrames_!=0; }
    bool invulnerable() const { return painFrames_!=0; }
    unsigned remainingFrames() const { return std::max(handFrames_,painFrames_); }
    const auto& slots() const { return slots_; }
    uint8_t newlyEarnedCoins() const { return newlyEarned_; }
    void setNewlyEarnedCoins(uint8_t mask) { newlyEarned_=mask; }
private:
    std::array<TypingOriginalItem,4> slots_{};
    unsigned handFrames_=0,painFrames_=0;
    double frameRemainder_=0;
    bool usedContinue_=false,unlimitedHand_=false;
    uint8_t newlyEarned_=0;
    uint32_t sourceFlags_=0;
    unsigned temporaryScenes_=0;
    void replenishHand() {
        if (unlimitedHand_ && !handFrames_ && !painFrames_) slots_[0]=TypingOriginalItem::HandOfGod;
    }
};
} // namespace game
