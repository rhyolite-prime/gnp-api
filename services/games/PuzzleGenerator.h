/**
 * Builds crossword, letter-sudoku, word search, and riddle puzzles from a
 * publication corpus. Solutions stay in `stored`; `clientPuzzle` is safe to send.
 */
#pragma once

#include <cstdint>
#include <string>

#include <json/json.h>

#include "services/games/Corpus.h"

namespace gnp::services {

struct BuiltPuzzle {
    std::string kind;
    Json::Value stored;
    std::string summary;
};

struct GradeResult {
    int correct = 0;
    int total = 0;
    int wrong = 0;
    bool solved = false;
    double accuracy = 0.0;
    Json::Value review;
    std::string summary;
};

struct HintResult {
    bool ok = false;
    std::string error;
    Json::Value hint;
};

class PuzzleGenerator {
public:
    static BuiltPuzzle build(const std::string &gameType,
                             const std::string &difficulty,
                             const Corpus &corpus,
                             uint32_t seed);

    static GradeResult grade(const Json::Value &stored, const Json::Value &submission);

    /// Mutates player state inside `stored` and returns the revealed fragment.
    static HintResult revealHint(Json::Value &stored);

    static Json::Value clientView(const Json::Value &stored);
    static Json::Value publicMeta(const Json::Value &stored);
};

} // namespace gnp::services
