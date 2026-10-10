/**
 * Shared game-type, difficulty, and scoring vocabulary for the games module.
 * Puzzle words are never invented here — generators consume a publication corpus.
 */
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "constants/PublicationTypes.h"

namespace gnp::services {

enum class GameTypeCode : int32_t {
    Sudoku = 1,
    WordSearch = 2,
    Crossword = 3,
    Riddle = 4
};

struct StartOptions {
    std::string userId;
    std::string gameType;
    std::string difficulty = "medium";
    std::string publicationId;
    std::string newspaperId;
    std::vector<std::string> topics;
    bool isMultiplayer = false;
    std::string opponentId;
    std::string dailyChallengeId;
    uint32_t seed = 0;
};

constexpr int kMistakePenalty = 3;
constexpr int kHintPenalty = 5;
constexpr int kStreakBonusPerDay = 10;
constexpr int kRivalryBonus = 25;
constexpr int kPartialDailyThreshold = 60; // percent accuracy required for a partial daily bonus

inline std::string lowerCopy(std::string value) {
    for (char &c : value) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc >= 'A' && uc <= 'Z') {
            c = static_cast<char>(uc - 'A' + 'a');
        }
    }
    return value;
}

inline std::string canonicalGameType(const std::string &raw) {
    const std::string value = lowerCopy(raw);
    if (value == "sudoku" || value == "1" || value == "wordoku") return "sudoku";
    if (value == "word_search" || value == "wordsearch" || value == "word-search" || value == "2") return "word_search";
    if (value == "crossword" || value == "cross_word" || value == "cross-word" || value == "3") return "crossword";
    if (value == "riddle" || value == "riddles" || value == "brain_teaser" || value == "brainteaser" ||
        value == "brain-teaser" || value == "4") {
        return "riddle";
    }
    if (value == "daily" || value == "daily_challenge" || value == "daily-challenge" || value == "5") return "daily";
    return "";
}

inline int32_t gameTypeCode(const std::string &canonical) {
    if (canonical == "sudoku") return static_cast<int32_t>(GameTypeCode::Sudoku);
    if (canonical == "word_search") return static_cast<int32_t>(GameTypeCode::WordSearch);
    if (canonical == "crossword") return static_cast<int32_t>(GameTypeCode::Crossword);
    if (canonical == "riddle") return static_cast<int32_t>(GameTypeCode::Riddle);
    return 0;
}

inline std::string gameTypeName(int32_t code) {
    switch (code) {
        case 1: return "sudoku";
        case 2: return "word_search";
        case 3: return "crossword";
        case 4: return "riddle";
        default: return "";
    }
}

inline std::string gameTypeTitle(const std::string &canonical) {
    if (canonical == "sudoku") return "Sudoku";
    if (canonical == "word_search") return "Word Search";
    if (canonical == "crossword") return "Crossword";
    if (canonical == "riddle") return "Riddles";
    if (canonical == "daily") return "Daily Challenge";
    return canonical;
}

inline std::string canonicalDifficulty(const std::string &raw) {
    const std::string value = lowerCopy(raw);
    // Numeric values follow constants::DifficultyMultipliers, not a 0-3 ladder.
    if (value.empty() || value == "medium" || value == "2" || value == "med") return "medium";
    if (value == "easy" || value == "0") return "easy";
    if (value == "hard" || value == "1") return "hard";
    if (value == "expert" || value == "3") return "expert";
    return "";
}

inline int32_t difficultyCode(const std::string &canonical) {
    if (canonical == "easy") return constants::DifficultyMultipliers::EASY;
    if (canonical == "hard") return constants::DifficultyMultipliers::HARD;
    if (canonical == "expert") return constants::DifficultyMultipliers::EXPERT;
    return constants::DifficultyMultipliers::MEDIUM;
}

inline std::string difficultyName(int32_t code) {
    if (code == constants::DifficultyMultipliers::EASY) return "easy";
    if (code == constants::DifficultyMultipliers::HARD) return "hard";
    if (code == constants::DifficultyMultipliers::EXPERT) return "expert";
    if (code == constants::DifficultyMultipliers::MEDIUM) return "medium";
    return "medium";
}

inline int difficultyMultiplier(const std::string &canonical) {
    if (canonical == "easy") return 1;
    if (canonical == "hard") return 3;
    if (canonical == "expert") return 4;
    return 2;
}

inline int basePointsFor(const std::string &canonical) {
    if (canonical == "sudoku") return constants::GamePoints::BASE_POINTS_SUDOKU;
    if (canonical == "word_search") return constants::GamePoints::BASE_POINTS_WORDSEARCH;
    if (canonical == "crossword") return constants::GamePoints::BASE_POINTS_CROSSWORD;
    if (canonical == "riddle") return constants::GamePoints::BASE_POINTS_BRAINTEASER;
    return 0;
}

inline int hintCapFor(const std::string &canonical) {
    if (canonical == "easy") return 5;
    if (canonical == "hard") return 2;
    if (canonical == "expert") return 1;
    return 3;
}

inline int targetSeconds(const std::string &gameType, const std::string &difficulty) {
    int base = 480;
    if (gameType == "crossword") base = 600;
    else if (gameType == "sudoku") base = 540;
    else if (gameType == "word_search") base = 360;
    else if (gameType == "riddle") base = 300;
    if (difficulty == "easy") return base + 180;
    if (difficulty == "hard") return std::max(120, base - 120);
    if (difficulty == "expert") return std::max(90, base - 200);
    return base;
}

inline bool isUuid(const std::string &value) {
    if (value.size() != 36) return false;
    for (size_t i = 0; i < value.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
            continue;
        }
        if (!std::isxdigit(c)) return false;
    }
    return true;
}

inline bool isPlayableGameType(const std::string &canonical) {
    return canonical == "sudoku" || canonical == "word_search" || canonical == "crossword" || canonical == "riddle";
}

} // namespace gnp::services
