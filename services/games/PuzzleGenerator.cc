#include "services/games/PuzzleGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "services/games/GameTypes.h"

namespace gnp::services {
namespace {

class Rng {
public:
    explicit Rng(uint32_t seed) : gen_(seed == 0 ? std::random_device{}() : seed) {}

    int uniform(int lo, int hi) {
        if (hi <= lo) return lo;
        std::uniform_int_distribution<int> dist(lo, hi);
        return dist(gen_);
    }

    template <typename T>
    void shuffle(std::vector<T> &values) {
        std::shuffle(values.begin(), values.end(), gen_);
    }

    template <size_t N>
    void shuffle(std::array<int, N> &values) {
        std::shuffle(values.begin(), values.end(), gen_);
    }

    char letterFrom(const std::string &bag) {
        if (bag.empty()) return static_cast<char>('A' + uniform(0, 25));
        return bag[static_cast<size_t>(uniform(0, static_cast<int>(bag.size()) - 1))];
    }

private:
    std::mt19937 gen_;
};

std::string upperAscii(std::string value) {
    for (char &c : value) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc >= 'a' && uc <= 'z') c = static_cast<char>(uc - 'a' + 'A');
    }
    return value;
}

std::string trim(const std::string &value) {
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(begin, end - begin);
}

bool isLetter(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

std::string blankWord(const std::string &sentence, const std::string &word) {
    if (sentence.empty() || word.empty()) return "";
    const std::string upper = upperAscii(sentence);
    std::string out = sentence;
    bool found = false;
    for (size_t i = 0; i + word.size() <= upper.size();) {
        if (upper.compare(i, word.size(), word) != 0) {
            ++i;
            continue;
        }
        const bool left = i == 0 || !isLetter(static_cast<unsigned char>(upper[i - 1]));
        const bool right = i + word.size() == upper.size() || !isLetter(static_cast<unsigned char>(upper[i + word.size()]));
        if (!left || !right) {
            ++i;
            continue;
        }
        out.replace(i, word.size(), std::string(word.size(), '_'));
        found = true;
        i += word.size();
    }
    return found ? out : "";
}

std::string publicationLabel(const SourceRef &source) {
    if (!source.publicationName.empty()) return source.publicationName;
    if (!source.newspaperTitle.empty()) return source.newspaperTitle;
    return "the paper";
}

std::string makeClue(const Lexeme &lexeme) {
    const std::string base = !lexeme.sentence.empty() ? lexeme.sentence : lexeme.snippet;
    std::string blanked = blankWord(base, lexeme.word);
    const std::string label = publicationLabel(lexeme.source);
    if (blanked.empty() || upperAscii(blanked).find(lexeme.word) != std::string::npos) {
        std::ostringstream clue;
        clue << "A " << lexeme.word.size() << "-letter word from " << label;
        if (lexeme.source.pageNumber > 0) clue << ", page " << lexeme.source.pageNumber;
        clue << ".";
        return clue.str();
    }
    if (blanked.size() > 170) blanked = blanked.substr(0, 167) + "...";
    std::ostringstream clue;
    clue << label;
    if (lexeme.source.pageNumber > 0) clue << " p." << lexeme.source.pageNumber;
    clue << ": \"" << blanked << "\"";
    return clue.str();
}

Json::Value sourceJson(const Lexeme &lexeme) {
    Json::Value json = lexeme.source.toJson();
    if (!lexeme.snippet.empty()) json["snippet"] = lexeme.snippet;
    return json;
}

SourceRef primarySource(const Corpus &corpus) {
    if (!corpus.lexemes.empty()) return corpus.lexemes.front().source;
    if (!corpus.sources.empty()) return corpus.sources.front();
    return {};
}

int wordTarget(const std::string &difficulty, const std::string &kind) {
    if (kind == "riddle") {
        if (difficulty == "easy") return 3;
        if (difficulty == "hard") return 7;
        if (difficulty == "expert") return 8;
        return 5;
    }
    if (difficulty == "easy") return kind == "word_search" ? 6 : 5;
    if (difficulty == "hard") return kind == "word_search" ? 10 : 8;
    if (difficulty == "expert") return kind == "word_search" ? 12 : 10;
    return kind == "word_search" ? 8 : 6;
}

int gridSizeFor(const std::string &difficulty, const std::string &kind) {
    if (kind == "word_search") {
        if (difficulty == "easy") return 10;
        if (difficulty == "hard") return 14;
        if (difficulty == "expert") return 15;
        return 12;
    }
    if (difficulty == "easy") return 9;
    if (difficulty == "hard") return 13;
    if (difficulty == "expert") return 15;
    return 11;
}

std::vector<const Lexeme *> pickWords(const Corpus &corpus, int count, int minLen, int maxLen, Rng &rng, bool rejectSubstrings) {
    std::vector<const Lexeme *> pool;
    pool.reserve(corpus.lexemes.size());
    const size_t poolLimit = std::min<size_t>(corpus.lexemes.size(), 120);
    for (size_t i = 0; i < poolLimit; ++i) {
        const auto &lexeme = corpus.lexemes[i];
        if (static_cast<int>(lexeme.word.size()) < minLen || static_cast<int>(lexeme.word.size()) > maxLen) continue;
        pool.push_back(&lexeme);
    }
    rng.shuffle(pool);

    std::vector<const Lexeme *> chosen;
    for (const Lexeme *candidate : pool) {
        if (static_cast<int>(chosen.size()) >= count) break;
        if (rejectSubstrings) {
            bool clash = false;
            for (const Lexeme *existing : chosen) {
                if (candidate->word.find(existing->word) != std::string::npos ||
                    existing->word.find(candidate->word) != std::string::npos) {
                    clash = true;
                    break;
                }
            }
            if (clash) continue;
        }
        chosen.push_back(candidate);
    }
    std::sort(chosen.begin(), chosen.end(), [](const Lexeme *a, const Lexeme *b) {
        return a->word.size() > b->word.size();
    });
    return chosen;
}

Json::Value charGridJson(const std::vector<std::string> &grid) {
    Json::Value rows(Json::arrayValue);
    for (const auto &row : grid) {
        Json::Value cells(Json::arrayValue);
        for (char c : row) cells.append(std::string(1, c));
        rows.append(cells);
    }
    return rows;
}

std::vector<std::string> readGrid(const Json::Value &node, int expected) {
    std::vector<std::string> grid;
    if (!node.isArray()) return grid;
    for (const auto &row : node) {
        std::string line;
        if (row.isString()) {
            line = upperAscii(row.asString());
        } else if (row.isArray()) {
            for (const auto &cell : row) {
                std::string value = upperAscii(cell.isString() ? cell.asString() : "");
                line.push_back(value.empty() ? ' ' : value[0]);
            }
        }
        if (expected > 0) {
            if (static_cast<int>(line.size()) < expected) line.append(static_cast<size_t>(expected) - line.size(), ' ');
            if (static_cast<int>(line.size()) > expected) line.resize(static_cast<size_t>(expected));
        }
        grid.push_back(line);
    }
    return grid;
}

bool inBounds(int size, int row, int col) {
    return row >= 0 && col >= 0 && row < size && col < size;
}

// --- Crossword --------------------------------------------------------------

bool canPlace(const std::vector<std::string> &grid, const std::string &word, int row, int col, bool across, bool needIntersect) {
    const int size = static_cast<int>(grid.size());
    const int dr = across ? 0 : 1;
    const int dc = across ? 1 : 0;
    const int beforeR = row - dr;
    const int beforeC = col - dc;
    if (inBounds(size, beforeR, beforeC) && grid[beforeR][beforeC] != '#') return false;
    const int afterR = row + dr * static_cast<int>(word.size());
    const int afterC = col + dc * static_cast<int>(word.size());
    if (inBounds(size, afterR, afterC) && grid[afterR][afterC] != '#') return false;

    int intersections = 0;
    for (size_t i = 0; i < word.size(); ++i) {
        const int r = row + dr * static_cast<int>(i);
        const int c = col + dc * static_cast<int>(i);
        if (!inBounds(size, r, c) || grid[r][c] == '#') return false;
        if (grid[r][c] != '.' && grid[r][c] != word[i]) return false;
        if (grid[r][c] == word[i]) {
            ++intersections;
            continue;
        }
        const int pr = across ? 1 : 0;
        const int pc = across ? 0 : 1;
        auto blocked = [&](int rr, int cc) {
            return inBounds(size, rr, cc) && grid[rr][cc] != '.' && grid[rr][cc] != '#';
        };
        if (blocked(r - pr, c - pc) || blocked(r + pr, c + pc)) return false;
    }
    return !needIntersect || intersections > 0;
}

void paint(std::vector<std::string> &grid, const std::string &word, int row, int col, bool across) {
    const int dr = across ? 0 : 1;
    const int dc = across ? 1 : 0;
    for (size_t i = 0; i < word.size(); ++i) {
        grid[row + dr * static_cast<int>(i)][col + dc * static_cast<int>(i)] = word[i];
    }
}

struct Placed {
    const Lexeme *lexeme = nullptr;
    int row = 0;
    int col = 0;
    bool across = true;
    int number = 0;
};

std::vector<std::string> &placedGrid() {
    static thread_local std::vector<std::string> grid;
    return grid;
}

std::vector<Placed> placeCrossword(const std::vector<const Lexeme *> &words, int size, Rng &rng) {
    std::vector<std::string> grid(static_cast<size_t>(size), std::string(static_cast<size_t>(size), '.'));
    std::vector<Placed> placed;
    if (words.empty()) return placed;

    const int mid = size / 2;
    const std::string &first = words.front()->word;
    int startCol = std::max(0, mid - static_cast<int>(first.size()) / 2);
    if (startCol + static_cast<int>(first.size()) > size) startCol = 0;
    if (!canPlace(grid, first, mid, startCol, true, false)) return placed;
    paint(grid, first, mid, startCol, true);
    placed.push_back(Placed{words.front(), mid, startCol, true, 0});

    for (size_t w = 1; w < words.size(); ++w) {
        const std::string &word = words[w]->word;
        int bestScore = -1;
        Placed best;
        std::vector<Placed> options;
        for (const auto &existing : placed) {
            for (size_t i = 0; i < existing.lexeme->word.size(); ++i) {
                for (size_t j = 0; j < word.size(); ++j) {
                    if (existing.lexeme->word[i] != word[j]) continue;
                    const bool across = !existing.across;
                    int row = existing.row;
                    int col = existing.col;
                    if (existing.across) {
                        row = existing.row - static_cast<int>(j);
                        col = existing.col + static_cast<int>(i);
                    } else {
                        row = existing.row + static_cast<int>(i);
                        col = existing.col - static_cast<int>(j);
                    }
                    if (!canPlace(grid, word, row, col, across, true)) continue;
                    int score = 2 + rng.uniform(0, 3);
                    options.push_back(Placed{words[w], row, col, across, score});
                }
            }
        }
        if (options.empty()) continue;
        best = options[static_cast<size_t>(rng.uniform(0, static_cast<int>(options.size()) - 1))];
        bestScore = best.number;
        (void)bestScore;
        paint(grid, word, best.row, best.col, best.across);
        best.number = 0;
        placed.push_back(best);
    }

    for (auto &row : grid) {
        for (char &cell : row) {
            if (cell == '.') cell = '#';
        }
    }

    // Number starts.
    int number = 1;
    std::vector<std::vector<int>> numbers(static_cast<size_t>(size), std::vector<int>(static_cast<size_t>(size), 0));
    for (int r = 0; r < size; ++r) {
        for (int c = 0; c < size; ++c) {
            if (grid[r][c] == '#') continue;
            const bool acrossStart = (c == 0 || grid[r][c - 1] == '#') && c + 1 < size && grid[r][c + 1] != '#';
            const bool downStart = (r == 0 || grid[r - 1][c] == '#') && r + 1 < size && grid[r + 1][c] != '#';
            if (!acrossStart && !downStart) continue;
            numbers[r][c] = number++;
        }
    }
    for (auto &item : placed) item.number = numbers[item.row][item.col];

    // Stash the finished grid in the first placed word's number field? No — return grid via a side channel.
    // Encode grid by writing it back through a function-local static? Cleaner to return a pair.
    // The caller rebuilds from placements, so keep the grid on a well-known side store.
    placedGrid() = grid;
    return placed;
}

BuiltPuzzle buildCrossword(const Corpus &corpus, const std::string &difficulty, Rng &rng) {
    const int size = gridSizeFor(difficulty, "crossword");
    const int want = wordTarget(difficulty, "crossword");
    std::vector<Placed> placed;
    for (int attempt = 0; attempt < 10 && static_cast<int>(placed.size()) < 4; ++attempt) {
        auto words = pickWords(corpus, want + attempt, 4, std::min(12, size - 1), rng, false);
        placed = placeCrossword(words, size, rng);
    }
    if (placed.size() < 4 || placedGrid().size() != static_cast<size_t>(size)) {
        throw std::runtime_error("PUZZLE: Could not place a crossword from the available publication words.");
    }
    const auto solutionGrid = placedGrid();

    Json::Value numbers(Json::arrayValue);
    Json::Value clientGrid(Json::arrayValue);
    int number = 1;
    std::vector<std::vector<int>> numberMap(static_cast<size_t>(size), std::vector<int>(static_cast<size_t>(size), 0));
    for (int r = 0; r < size; ++r) {
        Json::Value numberRow(Json::arrayValue);
        Json::Value clientRow(Json::arrayValue);
        for (int c = 0; c < size; ++c) {
            if (solutionGrid[r][c] == '#') {
                numberRow.append(0);
                clientRow.append("#");
                continue;
            }
            const bool acrossStart = (c == 0 || solutionGrid[r][c - 1] == '#') && c + 1 < size && solutionGrid[r][c + 1] != '#';
            const bool downStart = (r == 0 || solutionGrid[r - 1][c] == '#') && r + 1 < size && solutionGrid[r + 1][c] != '#';
            int n = 0;
            if (acrossStart || downStart) {
                n = number++;
                numberMap[r][c] = n;
            }
            numberRow.append(n);
            clientRow.append("");
        }
        numbers.append(numberRow);
        clientGrid.append(clientRow);
    }
    for (auto &item : placed) item.number = numberMap[item.row][item.col];

    Json::Value across(Json::arrayValue);
    Json::Value down(Json::arrayValue);
    Json::Value solutionWords(Json::arrayValue);
    for (const auto &item : placed) {
        if (item.number <= 0 || item.lexeme == nullptr) continue;
        Json::Value clue;
        clue["number"] = item.number;
        clue["clue"] = makeClue(*item.lexeme);
        clue["row"] = item.row;
        clue["col"] = item.col;
        clue["length"] = static_cast<int>(item.lexeme->word.size());
        clue["direction"] = item.across ? "across" : "down";
        clue["source"] = sourceJson(*item.lexeme);
        if (item.across) across.append(clue);
        else down.append(clue);

        Json::Value answer;
        answer["number"] = item.number;
        answer["direction"] = item.across ? "across" : "down";
        answer["row"] = item.row;
        answer["col"] = item.col;
        answer["answer"] = item.lexeme->word;
        solutionWords.append(answer);
    }

    Json::Value puzzle;
    puzzle["size"] = size;
    puzzle["grid"] = clientGrid;
    puzzle["numbers"] = numbers;
    puzzle["clues"]["across"] = across;
    puzzle["clues"]["down"] = down;
    puzzle["wordCount"] = static_cast<int>(placed.size());
    puzzle["source"] = primarySource(corpus).toJson();

    Json::Value solution;
    solution["grid"] = charGridJson(solutionGrid);
    solution["words"] = solutionWords;

    BuiltPuzzle built;
    built.kind = "crossword";
    built.summary = std::to_string(placed.size()) + "-word crossword from " + publicationLabel(primarySource(corpus));
    built.stored["kind"] = built.kind;
    built.stored["difficulty"] = difficulty;
    built.stored["summary"] = built.summary;
    built.stored["publication"] = primarySource(corpus).toJson();
    built.stored["puzzle"] = puzzle;
    built.stored["solution"] = solution;
    built.stored["player"] = Json::Value(Json::objectValue);
    built.stored["player"]["grid"] = clientGrid;
    built.stored["player"]["reveals"] = Json::Value(Json::arrayValue);
    return built;
}

// --- Word search ------------------------------------------------------------

struct Direction {
    int dr;
    int dc;
    const char *name;
};

std::vector<Direction> directionsFor(const std::string &difficulty) {
    std::vector<Direction> dirs{{0, 1, "E"}, {1, 0, "S"}};
    if (difficulty == "easy") return dirs;
    dirs.push_back({1, 1, "SE"});
    dirs.push_back({1, -1, "SW"});
    if (difficulty == "medium") return dirs;
    dirs.push_back({0, -1, "W"});
    dirs.push_back({-1, 0, "N"});
    dirs.push_back({-1, 1, "NE"});
    dirs.push_back({-1, -1, "NW"});
    return dirs;
}

BuiltPuzzle buildWordSearch(const Corpus &corpus, const std::string &difficulty, Rng &rng) {
    const int size = gridSizeFor(difficulty, "word_search");
    const int maxLen = std::min(12, size);
    auto words = pickWords(corpus, wordTarget(difficulty, "word_search") + 4, 4, maxLen, rng, true);
    if (words.size() < 4) {
        throw std::runtime_error("PUZZLE: Not enough distinct publication words for a word search.");
    }

    std::vector<std::string> grid(static_cast<size_t>(size), std::string(static_cast<size_t>(size), '.'));
    const auto dirs = directionsFor(difficulty);
    Json::Value placements(Json::arrayValue);
    Json::Value wordList(Json::arrayValue);

    for (const Lexeme *lexeme : words) {
        if (wordList.size() >= static_cast<Json::ArrayIndex>(wordTarget(difficulty, "word_search"))) break;
        bool placed = false;
        for (int attempt = 0; attempt < 80 && !placed; ++attempt) {
            const Direction dir = dirs[static_cast<size_t>(rng.uniform(0, static_cast<int>(dirs.size()) - 1))];
            const int row = rng.uniform(0, size - 1);
            const int col = rng.uniform(0, size - 1);
            bool fits = true;
            for (size_t i = 0; i < lexeme->word.size(); ++i) {
                const int r = row + dir.dr * static_cast<int>(i);
                const int c = col + dir.dc * static_cast<int>(i);
                if (!inBounds(size, r, c) || (grid[r][c] != '.' && grid[r][c] != lexeme->word[i])) {
                    fits = false;
                    break;
                }
            }
            if (!fits) continue;
            for (size_t i = 0; i < lexeme->word.size(); ++i) {
                grid[row + dir.dr * static_cast<int>(i)][col + dir.dc * static_cast<int>(i)] = lexeme->word[i];
            }
            Json::Value placement;
            placement["word"] = lexeme->word;
            placement["row"] = row;
            placement["col"] = col;
            placement["dRow"] = dir.dr;
            placement["dCol"] = dir.dc;
            placement["direction"] = dir.name;
            placements.append(placement);

            Json::Value listed;
            listed["word"] = lexeme->word;
            listed["length"] = static_cast<int>(lexeme->word.size());
            listed["clue"] = makeClue(*lexeme);
            listed["source"] = sourceJson(*lexeme);
            wordList.append(listed);
            placed = true;
        }
    }
    if (wordList.size() < 4) {
        throw std::runtime_error("PUZZLE: Could not hide enough publication words in the word search grid.");
    }

    for (auto &row : grid) {
        for (char &cell : row) {
            if (cell == '.') cell = rng.letterFrom(corpus.letterBag);
        }
    }

    Json::Value dirNames(Json::arrayValue);
    for (const auto &dir : dirs) dirNames.append(dir.name);

    Json::Value puzzle;
    puzzle["size"] = size;
    puzzle["grid"] = charGridJson(grid);
    puzzle["words"] = wordList;
    puzzle["directions"] = dirNames;
    puzzle["source"] = primarySource(corpus).toJson();

    Json::Value solution;
    solution["placements"] = placements;

    BuiltPuzzle built;
    built.kind = "word_search";
    built.summary = std::to_string(wordList.size()) + "-word search from " + publicationLabel(primarySource(corpus));
    built.stored["kind"] = built.kind;
    built.stored["difficulty"] = difficulty;
    built.stored["summary"] = built.summary;
    built.stored["publication"] = primarySource(corpus).toJson();
    built.stored["puzzle"] = puzzle;
    built.stored["solution"] = solution;
    built.stored["player"] = Json::Value(Json::objectValue);
    built.stored["player"]["found"] = Json::Value(Json::arrayValue);
    built.stored["player"]["reveals"] = Json::Value(Json::arrayValue);
    return built;
}

// --- Sudoku / wordoku -------------------------------------------------------

using SudokuGrid = std::array<std::array<int, 9>, 9>;

bool sudokuValid(const SudokuGrid &grid, int row, int col, int value) {
    for (int i = 0; i < 9; ++i) {
        if (grid[row][i] == value || grid[i][col] == value) return false;
    }
    const int boxR = (row / 3) * 3;
    const int boxC = (col / 3) * 3;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            if (grid[boxR + r][boxC + c] == value) return false;
        }
    }
    return true;
}

bool fillSudoku(SudokuGrid &grid, Rng &rng) {
    for (int r = 0; r < 9; ++r) {
        for (int c = 0; c < 9; ++c) {
            if (grid[r][c] != 0) continue;
            std::array<int, 9> values{1, 2, 3, 4, 5, 6, 7, 8, 9};
            rng.shuffle(values);
            for (int value : values) {
                if (!sudokuValid(grid, r, c, value)) continue;
                grid[r][c] = value;
                if (fillSudoku(grid, rng)) return true;
                grid[r][c] = 0;
            }
            return false;
        }
    }
    return true;
}

int countSudoku(SudokuGrid grid, int limit) {
    int count = 0;
    std::function<void(int)> walk = [&](int index) {
        if (count >= limit) return;
        while (index < 81 && grid[index / 9][index % 9] != 0) ++index;
        if (index == 81) {
            ++count;
            return;
        }
        const int row = index / 9;
        const int col = index % 9;
        for (int value = 1; value <= 9 && count < limit; ++value) {
            if (!sudokuValid(grid, row, col, value)) continue;
            grid[row][col] = value;
            walk(index + 1);
            grid[row][col] = 0;
        }
    };
    walk(0);
    return count;
}

std::string nineSymbols(const Corpus &corpus, std::string &themeWord, std::string &anchor) {
    for (const auto &lexeme : corpus.lexemes) {
        std::string unique;
        std::array<bool, 26> seen{};
        for (char c : lexeme.word) {
            if (c < 'A' || c > 'Z' || seen[c - 'A']) continue;
            seen[c - 'A'] = true;
            unique.push_back(c);
        }
        if (unique.size() >= 9) {
            themeWord = lexeme.word;
            anchor = lexeme.sentence.empty() ? lexeme.snippet : lexeme.sentence;
            return unique.substr(0, 9);
        }
    }
    std::string unique;
    std::array<bool, 26> seen{};
    for (const auto &lexeme : corpus.lexemes) {
        if (themeWord.empty()) {
            themeWord = lexeme.word;
            anchor = lexeme.sentence.empty() ? lexeme.snippet : lexeme.sentence;
        }
        for (char c : lexeme.word) {
            if (c < 'A' || c > 'Z' || seen[c - 'A']) continue;
            seen[c - 'A'] = true;
            unique.push_back(c);
            if (unique.size() == 9) return unique;
        }
    }
    return unique.size() == 9 ? unique : "";
}

BuiltPuzzle buildSudoku(const Corpus &corpus, const std::string &difficulty, Rng &rng) {
    std::string themeWord;
    std::string anchor;
    const std::string symbols = nineSymbols(corpus, themeWord, anchor);
    if (symbols.size() != 9) {
        throw std::runtime_error("PUZZLE: Publication text did not contain nine distinct letters for Sudoku.");
    }

    SudokuGrid solved{};
    for (int box = 0; box < 3; ++box) {
        std::array<int, 9> values{1, 2, 3, 4, 5, 6, 7, 8, 9};
        rng.shuffle(values);
        int cursor = 0;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) solved[box * 3 + r][box * 3 + c] = values[cursor++];
        }
    }
    if (!fillSudoku(solved, rng)) {
        throw std::runtime_error("PUZZLE: Failed to generate a Sudoku solution.");
    }

    int givens = 46;
    if (difficulty == "medium") givens = 36;
    if (difficulty == "hard") givens = 32;
    if (difficulty == "expert") givens = 28;

    SudokuGrid puzzle = solved;
    std::vector<int> cells(81);
    for (int i = 0; i < 81; ++i) cells[i] = i;
    rng.shuffle(cells);
    int filled = 81;
    for (int cell : cells) {
        if (filled <= givens) break;
        const int row = cell / 9;
        const int col = cell % 9;
        const int saved = puzzle[row][col];
        puzzle[row][col] = 0;
        if (countSudoku(puzzle, 2) != 1) puzzle[row][col] = saved;
        else --filled;
    }

    auto toLetterGrid = [&](const SudokuGrid &grid, bool maskEmpties) {
        Json::Value rows(Json::arrayValue);
        for (int r = 0; r < 9; ++r) {
            Json::Value line(Json::arrayValue);
            for (int c = 0; c < 9; ++c) {
                if (grid[r][c] == 0) line.append(maskEmpties ? "" : "?");
                else line.append(std::string(1, symbols[static_cast<size_t>(grid[r][c] - 1)]));
            }
            rows.append(line);
        }
        return rows;
    };

    Json::Value given(Json::arrayValue);
    Json::Value symbolJson(Json::arrayValue);
    for (char c : symbols) symbolJson.append(std::string(1, c));
    for (int r = 0; r < 9; ++r) {
        Json::Value line(Json::arrayValue);
        for (int c = 0; c < 9; ++c) line.append(puzzle[r][c] != 0);
        given.append(line);
    }

    std::ostringstream rules;
    rules << "Fill every row, column and 3x3 box with the nine letters ";
    for (size_t i = 0; i < symbols.size(); ++i) {
        if (i) rules << (i + 1 == symbols.size() ? " and " : ", ");
        rules << symbols[i];
    }
    rules << ". They are taken from \"" << themeWord << "\" in " << publicationLabel(primarySource(corpus)) << ".";

    Json::Value puzzleJson;
    puzzleJson["size"] = 9;
    puzzleJson["symbols"] = symbolJson;
    puzzleJson["symbolWord"] = themeWord;
    puzzleJson["anchorQuote"] = anchor.size() > 180 ? anchor.substr(0, 177) + "..." : anchor;
    puzzleJson["rules"] = rules.str();
    puzzleJson["grid"] = toLetterGrid(puzzle, true);
    puzzleJson["given"] = given;
    puzzleJson["source"] = primarySource(corpus).toJson();

    Json::Value solution;
    solution["grid"] = toLetterGrid(solved, false);
    solution["symbols"] = symbolJson;

    BuiltPuzzle built;
    built.kind = "sudoku";
    built.summary = "Letter Sudoku on \"" + themeWord + "\" from " + publicationLabel(primarySource(corpus));
    built.stored["kind"] = built.kind;
    built.stored["difficulty"] = difficulty;
    built.stored["summary"] = built.summary;
    built.stored["publication"] = primarySource(corpus).toJson();
    built.stored["puzzle"] = puzzleJson;
    built.stored["solution"] = solution;
    built.stored["player"] = Json::Value(Json::objectValue);
    built.stored["player"]["grid"] = puzzleJson["grid"];
    built.stored["player"]["reveals"] = Json::Value(Json::arrayValue);
    return built;
}

// --- Riddles ----------------------------------------------------------------

std::string scramble(const std::string &word, Rng &rng) {
    std::string mixed = word;
    for (int attempt = 0; attempt < 8; ++attempt) {
        std::shuffle(mixed.begin(), mixed.end(), std::mt19937(static_cast<uint32_t>(rng.uniform(1, 2000000000))));
        if (mixed != word) return mixed;
    }
    if (mixed.size() > 1) std::swap(mixed[0], mixed[1]);
    return mixed;
}

BuiltPuzzle buildRiddle(const Corpus &corpus, const std::string &difficulty, Rng &rng) {
    const int want = wordTarget(difficulty, "riddle");
    auto words = pickWords(corpus, std::max(want + 8, 12), 4, 12, rng, true);
    if (static_cast<int>(words.size()) < std::min(want, 3)) {
        throw std::runtime_error("PUZZLE: Not enough publication sentences to write riddles.");
    }

    const bool hideChoices = difficulty == "hard" || difficulty == "expert";
    Json::Value items(Json::arrayValue);
    Json::Value answers(Json::arrayValue);
    std::vector<const Lexeme *> used;
    for (const Lexeme *lexeme : words) {
        if (static_cast<int>(items.size()) >= want) break;
        const bool anagram = (items.size() % 3 == 2);
        if (anagram && lexeme->word.size() < 5) continue;
        std::string prompt;
        if (anagram) {
            prompt = "Unscramble this word from " + publicationLabel(lexeme->source) + ": " + scramble(lexeme->word, rng) +
                     " (" + std::to_string(lexeme->word.size()) + " letters).";
            if (!lexeme->snippet.empty()) prompt += " Context: \"" + (lexeme->snippet.size() > 120 ? lexeme->snippet.substr(0, 117) + "..." : lexeme->snippet) + "\"";
        } else {
            std::string blanked = blankWord(lexeme->sentence.empty() ? lexeme->snippet : lexeme->sentence, lexeme->word);
            if (blanked.empty()) continue;
            if (blanked.size() > 180) blanked = blanked.substr(0, 177) + "...";
            prompt = publicationLabel(lexeme->source);
            if (lexeme->source.pageNumber > 0) prompt += " p." + std::to_string(lexeme->source.pageNumber);
            prompt += " — fill the blank: \"" + blanked + "\"";
        }
        if (upperAscii(prompt).find(lexeme->word) != std::string::npos) continue;

        Json::Value item;
        item["id"] = std::to_string(items.size() + 1);
        item["type"] = anagram ? "anagram" : "cloze";
        item["prompt"] = prompt;
        item["length"] = static_cast<int>(lexeme->word.size());
        item["source"] = sourceJson(*lexeme);
        if (!hideChoices || !anagram) {
            std::vector<std::string> choices{lexeme->word};
            for (const Lexeme *other : words) {
                if (other->word == lexeme->word) continue;
                if (std::abs(static_cast<int>(other->word.size()) - static_cast<int>(lexeme->word.size())) > 2) continue;
                choices.push_back(other->word);
                if (choices.size() == 4) break;
            }
            rng.shuffle(choices);
            Json::Value choiceJson(Json::arrayValue);
            for (const auto &choice : choices) choiceJson.append(choice);
            if (choiceJson.size() >= 2) item["choices"] = choiceJson;
        }
        items.append(item);

        Json::Value answer;
        answer["id"] = item["id"];
        answer["answer"] = lexeme->word;
        answers.append(answer);
        used.push_back(lexeme);
    }
    if (items.size() < 3) {
        throw std::runtime_error("PUZZLE: Could not write three publication riddles.");
    }

    Json::Value puzzle;
    puzzle["items"] = items;
    puzzle["questionCount"] = static_cast<int>(items.size());
    puzzle["source"] = primarySource(corpus).toJson();

    Json::Value solution;
    solution["items"] = answers;

    BuiltPuzzle built;
    built.kind = "riddle";
    built.summary = std::to_string(items.size()) + " news riddles from " + publicationLabel(primarySource(corpus));
    built.stored["kind"] = built.kind;
    built.stored["difficulty"] = difficulty;
    built.stored["summary"] = built.summary;
    built.stored["publication"] = primarySource(corpus).toJson();
    built.stored["puzzle"] = puzzle;
    built.stored["solution"] = solution;
    built.stored["player"] = Json::Value(Json::objectValue);
    built.stored["player"]["answers"] = Json::Value(Json::arrayValue);
    built.stored["player"]["reveals"] = Json::Value(Json::arrayValue);
    return built;
}

std::string cellAt(const Json::Value &grid, int row, int col) {
    if (!grid.isArray() || row < 0 || col < 0 || row >= static_cast<int>(grid.size())) return "";
    const auto &line = grid[row];
    if (line.isArray()) {
        if (col >= static_cast<int>(line.size())) return "";
        return upperAscii(line[col].isString() ? line[col].asString() : "");
    }
    if (line.isString()) {
        const std::string value = upperAscii(line.asString());
        if (col >= static_cast<int>(value.size())) return "";
        return std::string(1, value[static_cast<size_t>(col)]);
    }
    return "";
}

Json::Value submissionGrid(const Json::Value &stored, const Json::Value &submission) {
    if (submission.isMember("grid")) return submission["grid"];
    if (submission.isMember("player") && submission["player"].isMember("grid")) return submission["player"]["grid"];
    if (stored.isMember("player") && stored["player"].isMember("grid")) return stored["player"]["grid"];
    return Json::Value(Json::arrayValue);
}

} // namespace

BuiltPuzzle PuzzleGenerator::build(const std::string &gameType, const std::string &difficulty, const Corpus &corpus, uint32_t seed) {
    if (corpus.lexemes.size() < 4) {
        throw std::runtime_error("PUZZLE: Publication corpus is too small.");
    }
    Rng rng(seed == 0 ? static_cast<uint32_t>(std::random_device{}()) : seed);
    BuiltPuzzle built;
    if (gameType == "crossword") built = buildCrossword(corpus, difficulty, rng);
    else if (gameType == "word_search") built = buildWordSearch(corpus, difficulty, rng);
    else if (gameType == "sudoku") built = buildSudoku(corpus, difficulty, rng);
    else if (gameType == "riddle") built = buildRiddle(corpus, difficulty, rng);
    else throw std::runtime_error("PUZZLE: Unsupported game type.");

    built.stored["status"] = "active";
    built.stored["daily"] = false;
    built.stored["challengeId"] = "";
    Json::Value sources(Json::arrayValue);
    int added = 0;
    for (const auto &source : corpus.sources) {
        sources.append(source.toJson());
        if (++added >= 5) break;
    }
    built.stored["sources"] = sources;
    return built;
}

Json::Value PuzzleGenerator::clientView(const Json::Value &stored) {
    Json::Value view;
    view["kind"] = stored.get("kind", "");
    view["difficulty"] = stored.get("difficulty", "");
    view["summary"] = stored.get("summary", "");
    view["publication"] = stored.get("publication", Json::Value(Json::objectValue));
    view["sources"] = stored.get("sources", Json::Value(Json::arrayValue));
    view["puzzle"] = stored.get("puzzle", Json::Value(Json::objectValue));
    view["player"] = stored.get("player", Json::Value(Json::objectValue));
    view["daily"] = stored.get("daily", false);
    view["challengeId"] = stored.get("challengeId", "");
    view["status"] = stored.get("status", "active");
    return view;
}

Json::Value PuzzleGenerator::publicMeta(const Json::Value &stored) {
    Json::Value meta;
    meta["kind"] = stored.get("kind", "");
    meta["difficulty"] = stored.get("difficulty", "");
    meta["summary"] = stored.get("summary", "");
    const auto publication = stored.get("publication", Json::Value(Json::objectValue));
    Json::Value brief;
    brief["publicationName"] = publication.get("publicationName", "");
    brief["newspaperTitle"] = publication.get("newspaperTitle", "");
    brief["publicationDate"] = publication.get("publicationDate", "");
    brief["newspaperId"] = publication.get("newspaperId", "");
    meta["publication"] = brief;
    if (stored.isMember("puzzle") && stored["puzzle"].isMember("wordCount")) meta["wordCount"] = stored["puzzle"]["wordCount"];
    if (stored.isMember("puzzle") && stored["puzzle"].isMember("questionCount")) meta["questionCount"] = stored["puzzle"]["questionCount"];
    return meta;
}

GradeResult PuzzleGenerator::grade(const Json::Value &stored, const Json::Value &submission) {
    GradeResult result;
    const std::string kind = stored.get("kind", "").asString();
    const auto &solution = stored["solution"];
    const auto &puzzle = stored["puzzle"];

    if (kind == "sudoku" || kind == "crossword") {
        const auto expected = solution["grid"];
        auto actual = submissionGrid(stored, submission);
        if (!expected.isArray() || expected.empty()) return result;
        const int rows = static_cast<int>(expected.size());
        result.review["grid"] = expected;
        result.review["wrongCells"] = Json::Value(Json::arrayValue);
        for (int r = 0; r < rows; ++r) {
            const int cols = expected[r].isArray() ? static_cast<int>(expected[r].size()) : static_cast<int>(expected[r].asString().size());
            for (int c = 0; c < cols; ++c) {
                const std::string want = cellAt(expected, r, c);
                if (want.empty() || want == "#" || want == ".") continue;
                ++result.total;
                std::string got = cellAt(actual, r, c);
                if (got.empty() || got == " ") {
                    if (stored["player"].isMember("reveals")) {
                        for (const auto &reveal : stored["player"]["reveals"]) {
                            if (reveal.get("row", -1).asInt() == r && reveal.get("col", -1).asInt() == c) {
                                got = upperAscii(reveal.get("value", "").asString());
                            }
                        }
                    }
                }
                if (!got.empty() && got[0] == want[0]) ++result.correct;
                else if (!got.empty() && got != " " && got != "#") {
                    ++result.wrong;
                    Json::Value cell;
                    cell["row"] = r;
                    cell["col"] = c;
                    result.review["wrongCells"].append(cell);
                }
            }
        }
    } else if (kind == "word_search") {
        const auto placements = solution["placements"];
        result.total = placements.isArray() ? static_cast<int>(placements.size()) : 0;
        std::unordered_set<std::string> already;
        auto remember = [&](const Json::Value &node) {
            if (!node.isArray()) return;
            for (const auto &item : node) {
                const std::string word = item.isString() ? item.asString() : item.get("word", "").asString();
                if (!word.empty()) already.insert(upperAscii(word));
            }
        };
        remember(stored["player"]["found"]);
        remember(stored["player"]["reveals"]);
        result.review["placements"] = placements;
        for (const auto &placement : placements) {
            const std::string word = upperAscii(placement.get("word", "").asString());
            bool hit = already.count(word) > 0;
            if (!hit && submission.isMember("found") && submission["found"].isArray()) {
                for (const auto &item : submission["found"]) {
                    if (!item.isObject()) continue;
                    if (upperAscii(item.get("word", "").asString()) != word) continue;
                    const bool coords =
                        item.get("row", -999).asInt() == placement.get("row", -1).asInt() &&
                        item.get("col", -999).asInt() == placement.get("col", -1).asInt() &&
                        item.get("dRow", 99).asInt() == placement.get("dRow", 0).asInt() &&
                        item.get("dCol", 99).asInt() == placement.get("dCol", 0).asInt();
                    if (coords) hit = true;
                    else ++result.wrong;
                }
            }
            if (hit) ++result.correct;
        }
    } else if (kind == "riddle") {
        const auto expected = solution["items"];
        result.total = expected.isArray() ? static_cast<int>(expected.size()) : 0;
        std::unordered_map<std::string, std::string> given;
        auto absorbAnswers = [&](const Json::Value &node) {
            if (!node.isArray()) return;
            for (const auto &item : node) {
                if (!item.isObject()) continue;
                given[item.get("id", "").asString()] = upperAscii(trim(item.get("value", item.get("answer", "")).asString()));
            }
        };
        absorbAnswers(submission["answers"]);
        absorbAnswers(stored["player"]["answers"]);
        result.review["items"] = expected;
        for (const auto &item : expected) {
            const std::string id = item.get("id", "").asString();
            const std::string want = upperAscii(item.get("answer", "").asString());
            const auto it = given.find(id);
            if (it != given.end() && it->second == want) ++result.correct;
            else if (it != given.end() && !it->second.empty()) ++result.wrong;
        }
    }

    result.solved = result.total > 0 && result.correct == result.total;
    result.accuracy = result.total == 0 ? 0.0 : static_cast<double>(result.correct) / static_cast<double>(result.total);
    std::ostringstream summary;
    summary << result.correct << "/" << result.total << " correct";
    result.summary = summary.str();
    result.review["summary"] = result.summary;
    result.review["solved"] = result.solved;
    (void)puzzle;
    return result;
}

HintResult PuzzleGenerator::revealHint(Json::Value &stored) {
    HintResult result;
    const std::string kind = stored.get("kind", "").asString();
    if (!stored.isMember("player") || !stored["player"].isObject()) stored["player"] = Json::Value(Json::objectValue);
    if (!stored["player"].isMember("reveals") || !stored["player"]["reveals"].isArray()) {
        stored["player"]["reveals"] = Json::Value(Json::arrayValue);
    }

    if (kind == "sudoku" || kind == "crossword") {
        const auto &solution = stored["solution"]["grid"];
        const auto &playerGrid = stored["player"].isMember("grid") ? stored["player"]["grid"] : stored["puzzle"]["grid"];
        const int rows = solution.isArray() ? static_cast<int>(solution.size()) : 0;
        for (int r = 0; r < rows; ++r) {
            const int cols = solution[r].isArray() ? static_cast<int>(solution[r].size()) : 0;
            for (int c = 0; c < cols; ++c) {
                const std::string want = cellAt(solution, r, c);
                if (want.empty() || want == "#") continue;
                const std::string got = cellAt(playerGrid, r, c);
                if (!got.empty() && got[0] == want[0]) continue;
                bool already = false;
                for (const auto &reveal : stored["player"]["reveals"]) {
                    if (reveal.get("row", -1).asInt() == r && reveal.get("col", -1).asInt() == c) already = true;
                }
                if (already) continue;
                Json::Value hint;
                hint["type"] = "letter";
                hint["row"] = r;
                hint["col"] = c;
                hint["value"] = want.substr(0, 1);
                stored["player"]["reveals"].append(hint);
                if (!stored["player"].isMember("grid")) stored["player"]["grid"] = stored["puzzle"]["grid"];
                if (stored["player"]["grid"].isArray() && r < static_cast<int>(stored["player"]["grid"].size()) &&
                    stored["player"]["grid"][r].isArray() && c < static_cast<int>(stored["player"]["grid"][r].size())) {
                    stored["player"]["grid"][r][c] = want.substr(0, 1);
                }
                result.ok = true;
                result.hint = hint;
                return result;
            }
        }
        result.error = "Nothing left to reveal.";
        return result;
    }

    if (kind == "word_search") {
        for (const auto &placement : stored["solution"]["placements"]) {
            const std::string word = upperAscii(placement.get("word", "").asString());
            bool known = false;
            for (const auto &found : stored["player"]["found"]) {
                const std::string value = found.isString() ? found.asString() : found.get("word", "").asString();
                if (upperAscii(value) == word) known = true;
            }
            for (const auto &reveal : stored["player"]["reveals"]) {
                if (upperAscii(reveal.get("word", "").asString()) == word) known = true;
            }
            if (known) continue;
            Json::Value hint = placement;
            hint["type"] = "placement";
            stored["player"]["reveals"].append(hint);
            if (!stored["player"].isMember("found") || !stored["player"]["found"].isArray()) {
                stored["player"]["found"] = Json::Value(Json::arrayValue);
            }
            stored["player"]["found"].append(word);
            result.ok = true;
            result.hint = hint;
            return result;
        }
        result.error = "Every word has already been found or revealed.";
        return result;
    }

    if (kind == "riddle") {
        std::unordered_map<std::string, std::string> given;
        for (const auto &answer : stored["player"]["answers"]) {
            given[answer.get("id", "").asString()] = upperAscii(answer.get("value", "").asString());
        }
        for (const auto &item : stored["solution"]["items"]) {
            const std::string id = item.get("id", "").asString();
            const std::string answer = upperAscii(item.get("answer", "").asString());
            if (given[id] == answer) continue;
            int revealed = 0;
            for (const auto &reveal : stored["player"]["reveals"]) {
                if (reveal.get("id", "").asString() == id) revealed = std::max(revealed, reveal.get("count", 1).asInt());
            }
            if (revealed >= static_cast<int>(answer.size())) continue;
            Json::Value hint;
            hint["type"] = "letters";
            hint["id"] = id;
            hint["count"] = revealed + 1;
            hint["prefix"] = answer.substr(0, static_cast<size_t>(revealed + 1));
            stored["player"]["reveals"].append(hint);
            result.ok = true;
            result.hint = hint;
            return result;
        }
        result.error = "Nothing left to reveal.";
        return result;
    }

    result.error = "This puzzle does not support hints.";
    return result;
}

} // namespace gnp::services
