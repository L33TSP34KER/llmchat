#pragma once
#include <string>
#include <vector>
#include <chrono>
#include <random>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

enum class SlotSymbol {
    CHERRY,
    LEMON,
    ORANGE,
    PLUM,
    BELL,
    BAR,
    SEVEN,
    DIAMOND,
    COUNT
};

struct SlotResult {
    std::vector<SlotSymbol> reels;
    int payout;
    std::string description;
};

static const std::vector<SlotSymbol> REEL_1 = {
    SlotSymbol::CHERRY, SlotSymbol::CHERRY, SlotSymbol::CHERRY, SlotSymbol::CHERRY, SlotSymbol::CHERRY,
    SlotSymbol::ORANGE, SlotSymbol::ORANGE, SlotSymbol::ORANGE, SlotSymbol::ORANGE,
    SlotSymbol::BELL, SlotSymbol::BELL, SlotSymbol::BELL,
    SlotSymbol::PLUM, SlotSymbol::PLUM, SlotSymbol::PLUM,
    SlotSymbol::LEMON, SlotSymbol::LEMON, SlotSymbol::LEMON,
    SlotSymbol::BAR,
    SlotSymbol::SEVEN,
    SlotSymbol::DIAMOND
};

static const std::vector<SlotSymbol> REEL_2 = {
    SlotSymbol::CHERRY, SlotSymbol::CHERRY,
    SlotSymbol::ORANGE, SlotSymbol::ORANGE, SlotSymbol::ORANGE, SlotSymbol::ORANGE,
    SlotSymbol::BELL, SlotSymbol::BELL, SlotSymbol::BELL, SlotSymbol::BELL,
    SlotSymbol::PLUM, SlotSymbol::PLUM, SlotSymbol::PLUM,
    SlotSymbol::LEMON, SlotSymbol::LEMON, SlotSymbol::LEMON, SlotSymbol::LEMON, SlotSymbol::LEMON,
    SlotSymbol::BAR,
    SlotSymbol::SEVEN,
    SlotSymbol::DIAMOND
};

static const std::vector<SlotSymbol> REEL_3 = {
    SlotSymbol::CHERRY, SlotSymbol::CHERRY, SlotSymbol::CHERRY,
    SlotSymbol::ORANGE, SlotSymbol::ORANGE, SlotSymbol::ORANGE, SlotSymbol::ORANGE,
    SlotSymbol::BELL, SlotSymbol::BELL, SlotSymbol::BELL, SlotSymbol::BELL,
    SlotSymbol::PLUM,
    SlotSymbol::LEMON, SlotSymbol::LEMON, SlotSymbol::LEMON, SlotSymbol::LEMON, SlotSymbol::LEMON, SlotSymbol::LEMON,
    SlotSymbol::BAR,
    SlotSymbol::SEVEN,
    SlotSymbol::DIAMOND
};

static std::string symbol_name(SlotSymbol s) {
    switch (s) {
        case SlotSymbol::CHERRY:  return "CHERRY";
        case SlotSymbol::LEMON:   return "LEMON";
        case SlotSymbol::ORANGE:  return "ORANGE";
        case SlotSymbol::PLUM:    return "PLUM";
        case SlotSymbol::BELL:    return "BELL";
        case SlotSymbol::BAR:     return "BAR";
        case SlotSymbol::SEVEN:   return "SEVEN";
        case SlotSymbol::DIAMOND: return "DIAMOND";
        default:                  return "???";
    }
}

static std::string symbol_emoji(SlotSymbol s) {
    switch (s) {
        case SlotSymbol::CHERRY:  return "\xF0\x9F\x8D\x92"; // 🍒
        case SlotSymbol::LEMON:   return "\xF0\x9F\x8D\x8B"; // 🍋
        case SlotSymbol::ORANGE:  return "\xF0\x9F\x8D\x8A"; // 🍊
        case SlotSymbol::PLUM:    return "\xF0\x9F\x8D\x86"; // 🍆
        case SlotSymbol::BELL:    return "\xF0\x9F\x94\x94"; // 🔔
        case SlotSymbol::BAR:     return "\xF0\x9F\x8E\xB0"; // 🎰
        case SlotSymbol::SEVEN:   return "\xEF\xBC\x97";    // ７
        case SlotSymbol::DIAMOND: return "\xF0\x9F\x92\x8E"; // 💎
        default:                  return "?";
    }
}

class Casino {
public:
    Casino();

    SlotResult spin(int bet);
    int get_balance() const { return balance_; }
    void set_balance(int b) { balance_ = b; }
    void save(const std::string& filepath);
    void load(const std::string& filepath);
    int get_total_spins() const { return total_spins_; }
    int get_total_wins() const { return total_wins_; }

private:
    int balance_;
    int total_spins_;
    int total_wins_;
    std::mt19937 rng_;

    int evaluate_payout(const std::vector<SlotSymbol>& reels, int bet);
    SlotSymbol random_symbol(const std::vector<SlotSymbol>& reel);
};
