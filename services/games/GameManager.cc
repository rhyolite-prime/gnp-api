#include "services/games/GameManager.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <ctime>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include <drogon/orm/Exception.h>

#include "constants/ErrorCodes.h"
#include "services/games/PublicationLexicon.h"
#include "services/games/PuzzleGenerator.h"
#include "utils/IdGeneratorUtils.h"

namespace gnp::services {
namespace {

std::atomic<bool> gCatalogReady{false};

dto::BaseApiResponse fail(int code, const std::string &message, const std::string &detail = "") {
    dto::BaseApiResponse response;
    response.success = false;
    response.message = message;
    response.error["code"] = code;
    response.error["message"] = message;
    if (!detail.empty()) response.error["detail"] = detail;
    return response;
}

dto::BaseApiResponse ok(const std::string &message, const Json::Value &result = Json::Value(Json::objectValue)) {
    dto::BaseApiResponse response;
    response.success = true;
    response.message = message;
    response.result = result;
    return response;
}

std::string jsonToString(const Json::Value &value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

Json::Value parseJson(const std::string &text) {
    Json::Value value(Json::objectValue);
    if (text.empty()) return value;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::istringstream stream(text);
    if (!Json::parseFromStream(builder, stream, &value, &errs) || !value.isObject()) {
        return Json::Value(Json::objectValue);
    }
    return value;
}

std::string cellText(const drogon::orm::Row &row, const char *name) {
    try {
        if (row[name].isNull()) return "";
        return row[name].as<std::string>();
    } catch (...) {
        return "";
    }
}

int cellInt(const drogon::orm::Row &row, const char *name, int fallback = 0) {
    try {
        if (row[name].isNull()) return fallback;
        return row[name].as<int>();
    } catch (...) {
        try {
            return std::stoi(cellText(row, name));
        } catch (...) {
            return fallback;
        }
    }
}

bool cellBool(const drogon::orm::Row &row, const char *name, bool fallback = false) {
    try {
        if (row[name].isNull()) return fallback;
        return row[name].as<bool>();
    } catch (...) {
        return fallback;
    }
}

double cellDouble(const drogon::orm::Row &row, const char *name, double fallback = 0.0) {
    try {
        if (row[name].isNull()) return fallback;
        return row[name].as<double>();
    } catch (...) {
        try {
            return std::stod(cellText(row, name));
        } catch (...) {
            return fallback;
        }
    }
}

std::string trimName(const std::string &first, const std::string &last) {
    std::string name = first;
    if (!last.empty()) {
        if (!name.empty()) name.push_back(' ');
        name += last;
    }
    return name;
}

std::string displayName(const std::string &username, const std::string &first, const std::string &last) {
    if (!username.empty()) return username;
    std::string name = trimName(first, last);
    return name.empty() ? "Reader" : name;
}

uint32_t hashSeed(const std::string &text) {
    uint32_t hash = 2166136261u;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 16777619u;
    }
    return hash == 0 ? 1u : hash;
}

std::string datePrefix(const std::string &value) {
    if (value.size() >= 10) return value.substr(0, 10);
    return value;
}

std::string previousDay(const std::string &ymd) {
    if (ymd.size() < 10) return "";
    std::tm tm{};
    tm.tm_year = std::stoi(ymd.substr(0, 4)) - 1900;
    tm.tm_mon = std::stoi(ymd.substr(5, 2)) - 1;
    tm.tm_mday = std::stoi(ymd.substr(8, 2));
    tm.tm_hour = 12;
    tm.tm_mday -= 1;
    std::mktime(&tm);
    char buffer[16];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &tm);
    return buffer;
}

std::chrono::system_clock::time_point fromTrantor(const trantor::Date &date) {
    return std::chrono::system_clock::time_point(std::chrono::microseconds(date.microSecondsSinceEpoch()));
}

std::string isoFromTrantor(const drogon::orm::Row &row, const char *name) {
    try {
        if (row[name].isNull()) return "";
        return trantor::Date::fromDbStringLocal(row[name].as<std::string>()).toDbStringLocal();
    } catch (...) {
        return cellText(row, name);
    }
}

int clampPage(int page) { return page < 1 ? 1 : page; }

int clampSize(int size) {
    if (size < 1) return 10;
    return std::min(size, 100);
}

std::string catalogTitle(const std::string &gameType) {
    if (gameType == "word_search") return "Word Search";
    if (gameType == "riddle") return "Riddles";
    return gameTypeTitle(gameType);
}

Json::Value scoringRules() {
    Json::Value rules;
    rules["mistakePenalty"] = kMistakePenalty;
    rules["hintPenalty"] = kHintPenalty;
    rules["perfectBonus"] = constants::GamePoints::PERFECT_BONUS;
    rules["dailyChallengeBonus"] = constants::GamePoints::DAILY_CHALLENGE_BONUS;
    rules["streakBonusPerDay"] = kStreakBonusPerDay;
    rules["maxStreakDays"] = constants::GamePoints::MAX_STREAK_BONUS;
    rules["rivalryBonus"] = kRivalryBonus;
    rules["achievementBonus"] = constants::GamePoints::ACHIEVEMENT_BONUS;
    rules["partialDailyThresholdPercent"] = kPartialDailyThreshold;
    Json::Value multipliers;
    multipliers["easy"] = 1;
    multipliers["medium"] = 2;
    multipliers["hard"] = 3;
    multipliers["expert"] = 4;
    rules["difficultyMultipliers"] = multipliers;
    rules["notes"] = "Points scale with difficulty and accuracy. Faster perfect solves earn a time bonus. "
                     "Daily puzzles add a one-time bonus. Streaks count consecutive days with a win.";
    return rules;
}

struct CompletionInput {
    std::string userId;
    std::string sessionId;
    std::string gameType;
    int32_t gameTypeCode = 0;
    std::string difficulty;
    bool won = false;
    bool perfect = false;
    bool countWinLoss = true;
    int puzzlePoints = 0;
    int bonusPoints = 0;
    int mistakes = 0;
    int hints = 0;
    int elapsed = 0;
    double accuracy = 0;
    std::string challengeId;
    int dailyBonusAvailable = 0;
    bool allowPartialDaily = false;
    Json::Value progress;
};

int nextStreak(int current, const std::string &lastPlayed, const std::string &today, bool won) {
    if (!won) return current;
    const std::string last = datePrefix(lastPlayed);
    if (last == today) return std::max(current, 1);
    if (!last.empty() && last == previousDay(today)) return current + 1;
    return 1;
}

bool achievementMatches(const std::string &required, const std::string &actual) {
    return required.empty() || required == "any" || required == actual;
}

Json::Value sessionClientJson(const GameSession &session, bool includeReview) {
    Json::Value json;
    json["sessionId"] = session.sessionId;
    json["userId"] = session.userId;
    json["gameType"] = session.gameType;
    json["gameTypeCode"] = gameTypeCode(session.gameType);
    json["puzzleId"] = session.puzzleId;
    json["difficulty"] = session.difficulty;
    json["isMultiplayer"] = session.isMultiplayer;
    json["opponentId"] = session.opponentId;
    json["isComplete"] = session.isComplete;
    json["totalPoints"] = session.totalPoints;
    json["currentScore"] = session.currentScore;
    json["mistakes"] = session.mistakes;
    json["hintsUsed"] = session.hintsUsed;
    json["hintsRemaining"] = std::max(0, hintCapFor(session.difficulty) - session.hintsUsed);
    json["completionTimeSeconds"] = session.completionTimeSeconds;
    Json::Value view = PuzzleGenerator::clientView(session.progressData);
    json["puzzle"] = view.get("puzzle", Json::Value(Json::objectValue));
    json["player"] = view.get("player", Json::Value(Json::objectValue));
    json["publication"] = view.get("publication", Json::Value(Json::objectValue));
    json["sources"] = view.get("sources", Json::Value(Json::arrayValue));
    json["summary"] = view.get("summary", "");
    json["daily"] = view.get("daily", false);
    json["challengeId"] = view.get("challengeId", "");
    json["status"] = view.get("status", session.isComplete ? "complete" : "active");
    if (includeReview && session.isComplete && session.progressData.isMember("solution")) {
        json["review"] = session.progressData["solution"];
    }
    if (session.progressData.isMember("reward")) json["reward"] = session.progressData["reward"];
    return json;
}

GameSession sessionFromRow(const drogon::orm::Row &row) {
    GameSession session;
    session.sessionId = cellText(row, "id");
    session.userId = cellText(row, "user_id");
    session.gameType = gameTypeName(cellInt(row, "game_type"));
    session.puzzleId = cellText(row, "puzzle_id");
    session.difficulty = difficultyName(cellInt(row, "difficulty"));
    session.isMultiplayer = cellBool(row, "is_multiplayer");
    session.opponentId = cellText(row, "opponent_id");
    session.isComplete = cellBool(row, "is_complete");
    session.totalPoints = cellInt(row, "total_points_earned");
    session.currentScore = session.totalPoints;
    session.mistakes = cellInt(row, "mistakes");
    session.hintsUsed = cellInt(row, "hints_used");
    session.completionTimeSeconds = cellInt(row, "completion_time_seconds");
    session.progressData = parseJson(cellText(row, "progress_data"));
    try {
        if (!row["start_time"].isNull()) session.startTime = fromTrantor(trantor::Date::fromDbStringLocal(row["start_time"].as<std::string>()));
        else session.startTime = std::chrono::system_clock::now();
    } catch (...) {
        session.startTime = std::chrono::system_clock::now();
    }
    session.lastActivity = session.startTime;
    return session;
}

drogon::Task<GameSession> loadOwnedSession(const drogon::orm::DbClientPtr &db,
                                           const std::string &sessionId,
                                           const std::string &userId) {
    auto rows = co_await db->execSqlCoro(
        "SELECT id::text AS id, user_id::text AS user_id, game_type, puzzle_id, difficulty, "
        "start_time, last_activity, is_multiplayer, opponent_id::text AS opponent_id, is_complete, "
        "total_points_earned, completion_time_seconds, mistakes, hints_used, progress_data::text AS progress_data, ended_at "
        "FROM game_sessions WHERE id = $1::uuid AND user_id = $2::uuid",
        sessionId, userId);
    if (rows.empty()) throw std::runtime_error("SESSION: Puzzle session was not found.");
    co_return sessionFromRow(rows[0]);
}

} // namespace

drogon::Task<GameReward> persistAndReward(const CompletionInput &input);
drogon::Task<int> rankForPoints(const std::string &userId);

std::string GameManager::generateSessionId() { return utils::IdGeneratorUtils::generateGuid(); }

int GameManager::getDifficultyMultiplier(const std::string &difficulty) {
    const std::string canonical = canonicalDifficulty(difficulty);
    return difficultyMultiplier(canonical.empty() ? "medium" : canonical);
}

int GameManager::getBasePoints(const std::string &gameType) {
    const std::string canonical = canonicalGameType(gameType);
    return basePointsFor(canonical.empty() ? gameType : canonical);
}

bool GameManager::isValidSession(const GameSession &session) {
    return isUuid(session.sessionId) && isUuid(session.userId) && isPlayableGameType(session.gameType);
}

double GameManager::calculateWinRate(const UserStats &stats) {
    if (stats.gamesPlayed <= 0) return 0.0;
    return static_cast<double>(stats.gamesWon) / static_cast<double>(stats.gamesPlayed);
}

void GameManager::initializeAchievements() {}

drogon::Task<void> GameManager::ensureAchievementCatalog() {
    if (gCatalogReady.load()) co_return;
    auto db = drogon::app().getDbClient();
    if (!db) co_return;

    struct Seed {
        const char *name;
        const char *description;
        const char *type;
        const char *gameType;
        int points;
        int games;
        int bonus;
    };
    const Seed seeds[] = {
        {"First Edition", "Finish your first publication puzzle.", "games", "any", 0, 1, 50},
        {"Cub Reporter", "Finish 10 publication puzzles.", "games", "any", 0, 10, 50},
        {"Newsroom Veteran", "Finish 50 publication puzzles.", "games", "any", 0, 50, 100},
        {"Sudoku Cadet", "Solve a letter Sudoku from the paper.", "games", "sudoku", 0, 1, 50},
        {"Grid Master", "Solve 25 Sudoku puzzles.", "games", "sudoku", 0, 25, 100},
        {"Word Scout", "Finish a word search from the paper.", "games", "word_search", 0, 1, 50},
        {"Search Captain", "Finish 25 word searches.", "games", "word_search", 0, 25, 100},
        {"Crossword Cub", "Finish a crossword from the paper.", "games", "crossword", 0, 1, 50},
        {"Puzzle Editor", "Finish 25 crosswords.", "games", "crossword", 0, 25, 100},
        {"Riddle Rookie", "Answer a set of news riddles.", "games", "riddle", 0, 1, 50},
        {"Oracle", "Finish 25 riddle sets.", "games", "riddle", 0, 25, 100},
        {"Point Collector", "Earn 100 game points.", "points", "any", 100, 0, 50},
        {"High Scorer", "Earn 500 game points.", "points", "any", 500, 0, 75},
        {"News Scholar", "Earn 1000 game points.", "points", "any", 1000, 0, 100},
        {"Front Page", "Earn 5000 game points.", "points", "any", 5000, 0, 200},
        {"Hat Trick", "Win on three consecutive days.", "streak", "any", 0, 3, 50},
        {"Week of Ink", "Win on seven consecutive days.", "streak", "any", 0, 7, 100},
        {"Clean Copy", "Finish a puzzle with no mistakes and no hints.", "perfect", "any", 0, 1, 50},
        {"Perfectionist", "Record 10 perfect solves.", "perfect", "any", 0, 10, 100},
        {"Daily Reader", "Complete a daily challenge.", "daily", "any", 0, 1, 50},
        {"Daily Streak", "Play the daily challenge on 7 different days.", "daily", "any", 0, 7, 150},
    };

    struct GameSeed {
        const char *title;
        const char *description;
    };
    const GameSeed games[] = {
        {"Sudoku", "Letter Sudoku whose nine symbols come from a word in a stored newspaper edition."},
        {"Word Search", "Hidden-word puzzle. Every word is taken from newspaper page text stored on the server."},
        {"Crossword", "Crossword answers and clues are built from sentences in published newspaper pages."},
        {"Riddles", "Newsroom riddles: cloze and anagram questions written from publication sentences."},
        {"Daily Challenge", "Shared daily puzzles and news questions from the latest edition, with bonus points."},
    };

    try {
        for (const auto &game : games) {
            co_await db->execSqlCoro(
                "INSERT INTO games (id, title, description, is_active) "
                "SELECT $1::uuid, $2, $3, TRUE "
                "WHERE NOT EXISTS (SELECT 1 FROM games WHERE lower(title) = lower($2))",
                utils::IdGeneratorUtils::generateGuid(), std::string(game.title), std::string(game.description));
        }
        for (const auto &seed : seeds) {
            co_await db->execSqlCoro(
                "INSERT INTO achievements_definitions "
                "(id, name, description, points_required, games_required, game_type, achievement_type, bonus_points) "
                "SELECT $1::uuid, $2, $3, $4, $5, $6, $7, $8 "
                "WHERE NOT EXISTS (SELECT 1 FROM achievements_definitions WHERE name = $2)",
                utils::IdGeneratorUtils::generateGuid(), std::string(seed.name), std::string(seed.description),
                seed.points, seed.games, std::string(seed.gameType), std::string(seed.type), seed.bonus);
        }
        gCatalogReady.store(true);
    } catch (const std::exception &e) {
        LOG_WARN << "[games] Achievement catalog seed skipped: " << e.what();
    }
    co_return;
}

drogon::Task<dto::BaseApiResponse> GameManager::getAllGameCategories() {
    co_await ensureAchievementCatalog();
    Json::Value categories(Json::arrayValue);
    const char *types[] = {"sudoku", "word_search", "crossword", "riddle", "daily"};
    std::unordered_map<std::string, bool> active = {
        {"sudoku", true}, {"word_search", true}, {"crossword", true}, {"riddle", true}, {"daily", true}};
    try {
        auto db = drogon::app().getDbClient();
        if (db) {
            auto rows = co_await db->execSqlCoro("SELECT title, description, is_active FROM games");
            for (size_t i = 0; i < rows.size(); ++i) {
                const std::string title = lowerCopy(cellText(rows[i], "title"));
                std::string key;
                if (title == "sudoku") key = "sudoku";
                else if (title.find("word") != std::string::npos) key = "word_search";
                else if (title.find("cross") != std::string::npos) key = "crossword";
                else if (title.find("riddle") != std::string::npos) key = "riddle";
                else if (title.find("daily") != std::string::npos) key = "daily";
                if (!key.empty()) active[key] = cellBool(rows[i], "is_active", true);
            }
        }
    } catch (const std::exception &e) {
        LOG_WARN << "[games] Could not read games catalog: " << e.what();
    }

    for (const char *type : types) {
        Json::Value item;
        item["gameType"] = type;
        item["title"] = catalogTitle(type);
        item["isActive"] = active[type];
        item["basePoints"] = std::string(type) == "daily" ? constants::GamePoints::DAILY_CHALLENGE_BONUS : basePointsFor(type);
        item["gameTypeCode"] = gameTypeCode(type);
        if (std::string(type) == "sudoku") {
            item["description"] = "Fill a 9x9 grid with nine letters drawn from a word in a stored newspaper.";
            item["content"] = "Letter symbols and the theme word come from publication text.";
        } else if (std::string(type) == "word_search") {
            item["description"] = "Find words hidden in a grid. The word list is taken from newspaper pages.";
            item["content"] = "Answers and filler letters are drawn from the same edition.";
        } else if (std::string(type) == "crossword") {
            item["description"] = "Solve clues written from sentences in published newspaper pages.";
            item["content"] = "Every answer is a word that appears in the sourced edition.";
        } else if (std::string(type) == "riddle") {
            item["description"] = "Cloze and anagram questions written from publication sentences.";
            item["content"] = "Each prompt cites the newspaper it was taken from.";
        } else {
            item["description"] = "A shared set of puzzles and news questions from the latest edition.";
            item["content"] = "One attempt per challenge. The fastest accurate solves lead the daily board.";
        }
        categories.append(item);
    }

    Json::Value result;
    result["categories"] = categories;
    result["difficulties"] = Json::Value(Json::arrayValue);
    result["difficulties"].append("easy");
    result["difficulties"].append("medium");
    result["difficulties"].append("hard");
    result["difficulties"].append("expert");
    result["scoring"] = scoringRules();
    co_return ok("Game categories loaded.", result);
}

drogon::Task<GameSession> GameManager::openSession(const StartOptions &options) {
    const std::string gameType = canonicalGameType(options.gameType);
    const std::string difficulty = canonicalDifficulty(options.difficulty);
    if (!isUuid(options.userId)) throw std::runtime_error("SESSION: A valid user id is required.");
    if (!isPlayableGameType(gameType)) throw std::runtime_error("SESSION: gameType must be sudoku, word_search, crossword, or riddle.");
    if (difficulty.empty()) throw std::runtime_error("SESSION: difficulty must be easy, medium, hard, or expert.");
    if (!options.publicationId.empty() && !isUuid(options.publicationId)) throw std::runtime_error("SESSION: publicationId is not a valid id.");
    if (!options.newspaperId.empty() && !isUuid(options.newspaperId)) throw std::runtime_error("SESSION: newspaperId is not a valid id.");
    if (options.isMultiplayer && (options.opponentId.empty() || !isUuid(options.opponentId))) {
        throw std::runtime_error("SESSION: A valid opponentId is required for a challenge.");
    }
    if (options.isMultiplayer && options.opponentId == options.userId) {
        throw std::runtime_error("SESSION: You cannot challenge yourself.");
    }

    auto db = drogon::app().getDbClient();
    if (!db) throw std::runtime_error("SESSION: Database client is not configured.");
    auto users = co_await db->execSqlCoro("SELECT id::text AS id FROM users WHERE id = $1::uuid", options.userId);
    if (users.empty()) throw std::runtime_error("SESSION: User was not found.");
    if (options.isMultiplayer) {
        auto opponents = co_await db->execSqlCoro("SELECT id::text AS id FROM users WHERE id = $1::uuid", options.opponentId);
        if (opponents.empty()) throw std::runtime_error("SESSION: Opponent was not found.");
    }

    try {
        auto flags = co_await db->execSqlCoro(
            "SELECT is_active FROM games WHERE lower(title) = lower($1) LIMIT 1", catalogTitle(gameType));
        if (!flags.empty() && !cellBool(flags[0], "is_active", true)) {
            throw std::runtime_error("SESSION: This game is currently inactive.");
        }
    } catch (const std::runtime_error &) {
        throw;
    } catch (const std::exception &e) {
        LOG_DEBUG << "[games] Active-flag lookup skipped: " << e.what();
    }

    Corpus corpus = co_await PublicationLexicon::load(options.publicationId, options.newspaperId, options.topics);
    BuiltPuzzle built = PuzzleGenerator::build(gameType, difficulty, corpus, options.seed);
    built.stored["daily"] = !options.dailyChallengeId.empty();
    built.stored["challengeId"] = options.dailyChallengeId;
    built.stored["status"] = options.isMultiplayer ? "pending" : "active";
    if (built.stored.isMember("puzzle")) {
        // Keep the generator summary; publication block is already set.
    }

    const std::string sessionId = generateSessionId();
    const std::string puzzleId = options.dailyChallengeId.empty() ? generateSessionId() : options.dailyChallengeId;
    co_await db->execSqlCoro(
        "INSERT INTO game_sessions ("
        "id, user_id, game_type, puzzle_id, difficulty, start_time, last_activity, is_multiplayer, opponent_id, "
        "is_complete, total_points_earned, completion_time_seconds, mistakes, hints_used, progress_data) "
        "VALUES ($1::uuid, $2::uuid, $3, $4, $5, NOW(), NOW(), $6, NULLIF($7, '')::uuid, FALSE, 0, 0, 0, 0, $8::jsonb)",
        sessionId, options.userId, gameTypeCode(gameType), puzzleId, difficultyCode(difficulty), options.isMultiplayer,
        options.opponentId, jsonToString(built.stored));

    auto rows = co_await db->execSqlCoro(
        "SELECT id::text AS id, user_id::text AS user_id, game_type, puzzle_id, difficulty, start_time, last_activity, "
        "is_multiplayer, opponent_id::text AS opponent_id, is_complete, total_points_earned, completion_time_seconds, "
        "mistakes, hints_used, progress_data::text AS progress_data, ended_at "
        "FROM game_sessions WHERE id = $1::uuid",
        sessionId);
    if (rows.empty()) throw std::runtime_error("SESSION: Puzzle was created but could not be reloaded.");
    co_return sessionFromRow(rows[0]);
}

drogon::Task<GameSession> GameManager::startGameSession(const std::string &userId,
                                                        const std::string &gameType,
                                                        const std::string &difficulty,
                                                        bool isMultiplayer,
                                                        const std::string &opponentId) {
    StartOptions options;
    options.userId = userId;
    options.gameType = gameType;
    options.difficulty = difficulty;
    options.isMultiplayer = isMultiplayer;
    options.opponentId = opponentId;
    co_return co_await openSession(options);
}

drogon::Task<dto::BaseApiResponse> GameManager::startGame(const StartOptions &options) {
    try {
        co_await ensureAchievementCatalog();
        StartOptions resolved = options;
        if (!resolved.topics.empty()) {
            bool topicsFailed = false;
            try {
                auto session = co_await openSession(resolved);
                Json::Value result = sessionClientJson(session, false);
                result["topicsRelaxed"] = false;
                co_return ok("Puzzle ready. Words were taken from stored newspaper publications.", result);
            } catch (const std::exception &e) {
                const std::string message = e.what();
                if (message.find("requested topics") == std::string::npos) throw;
                topicsFailed = true;
            }
            if (topicsFailed) {
                resolved.topics.clear();
                auto session = co_await openSession(resolved);
                Json::Value result = sessionClientJson(session, false);
                result["topicsRelaxed"] = true;
                co_return ok("Not enough topic matches. The puzzle used the wider publication lexicon.", result);
            }
        }
        auto session = co_await openSession(resolved);
        co_return ok("Puzzle ready. Words were taken from stored newspaper publications.", sessionClientJson(session, false));
    } catch (const drogon::orm::DrogonDbException &e) {
        co_return fail(constants::ERR_DB_QUERY, "Database error while starting a puzzle.", e.base().what());
    } catch (const std::exception &e) {
        const std::string message = e.what();
        const auto marker = message.find(": ");
        const std::string clean = marker == std::string::npos ? message : message.substr(marker + 2);
        const int code = message.rfind("SESSION: User", 0) == 0 ? constants::ERR_USER_NOT_FOUND : constants::ERR_VALIDATION;
        co_return fail(code, clean.empty() ? "Could not start the puzzle." : clean, message);
    }
}

drogon::Task<GameSession> GameManager::generateSudoku(const std::string &userId) {
    co_return co_await startGameSession(userId, "sudoku");
}

drogon::Task<GameSession> GameManager::generateWordSearch(const std::string &userId) {
    co_return co_await startGameSession(userId, "word_search");
}

drogon::Task<GameSession> GameManager::generateWordSearchTopics(const std::string &userId, const std::vector<std::string> &topics) {
    StartOptions options;
    options.userId = userId;
    options.gameType = "word_search";
    options.difficulty = "medium";
    options.topics = topics;
    co_return co_await openSession(options);
}

drogon::Task<GameSession> GameManager::generateCrosswordPuzzle(const std::string &userId) {
    co_return co_await startGameSession(userId, "crossword");
}

drogon::Task<GameSession> GameManager::generateBrainTeaser(const std::string &userId) {
    co_return co_await startGameSession(userId, "riddle");
}

drogon::Task<std::optional<GameSession>> GameManager::getCurrentSession(const std::string &userId) {
    if (!isUuid(userId)) co_return std::nullopt;
    auto db = drogon::app().getDbClient();
    if (!db) co_return std::nullopt;
    auto rows = co_await db->execSqlCoro(
        "SELECT id::text AS id, user_id::text AS user_id, game_type, puzzle_id, difficulty, start_time, last_activity, "
        "is_multiplayer, opponent_id::text AS opponent_id, is_complete, total_points_earned, completion_time_seconds, "
        "mistakes, hints_used, progress_data::text AS progress_data, ended_at "
        "FROM game_sessions WHERE user_id = $1::uuid AND COALESCE(is_complete, FALSE) = FALSE "
        "ORDER BY last_activity DESC NULLS LAST LIMIT 1",
        userId);
    if (rows.empty()) co_return std::nullopt;
    auto session = sessionFromRow(rows[0]);
    session.progressData = PuzzleGenerator::clientView(session.progressData);
    co_return session;
}

drogon::Task<dto::BaseApiResponse> GameManager::getCurrentSessionView(const std::string &userId) {
    try {
        auto session = co_await getCurrentSession(userId);
        if (!session) co_return ok("No active puzzle.", Json::Value(Json::nullValue));
        // getCurrentSession redacts, but review is not included. Reload raw for a consistent client view.
        auto db = drogon::app().getDbClient();
        auto raw = co_await loadOwnedSession(db, session->sessionId, userId);
        co_return ok("Active puzzle loaded.", sessionClientJson(raw, false));
    } catch (const std::exception &e) {
        co_return fail(constants::ERR_DB_QUERY, "Could not load the active puzzle.", e.what());
    }
}

drogon::Task<dto::BaseApiResponse> GameManager::getSessionView(const std::string &userId, const std::string &sessionId) {
    if (!isUuid(userId) || !isUuid(sessionId)) co_return fail(constants::ERR_VALIDATION, "A valid session id is required.");
    try {
        auto db = drogon::app().getDbClient();
        auto session = co_await loadOwnedSession(db, sessionId, userId);
        co_return ok("Puzzle session loaded.", sessionClientJson(session, session.isComplete));
    } catch (const std::exception &e) {
        co_return fail(constants::ERR_RESOURCE_NOT_FOUND, "Puzzle session was not found.", e.what());
    }
}

drogon::Task<bool> GameManager::saveProgress(const std::string &sessionId, const Json::Value &progress) {
    auto db = drogon::app().getDbClient();
    if (!db || !isUuid(sessionId)) co_return false;
    try {
        auto rows = co_await db->execSqlCoro(
            "SELECT user_id::text AS user_id, progress_data::text AS progress_data, COALESCE(is_complete, FALSE) AS is_complete "
            "FROM game_sessions WHERE id = $1::uuid",
            sessionId);
        if (rows.empty() || cellBool(rows[0], "is_complete")) co_return false;
        Json::Value stored = parseJson(cellText(rows[0], "progress_data"));
        if (progress.isMember("player")) stored["player"] = progress["player"];
        else if (progress.isMember("grid") || progress.isMember("found") || progress.isMember("answers")) {
            if (progress.isMember("grid")) stored["player"]["grid"] = progress["grid"];
            if (progress.isMember("found")) stored["player"]["found"] = progress["found"];
            if (progress.isMember("answers")) stored["player"]["answers"] = progress["answers"];
        } else {
            stored["player"] = progress;
        }
        co_await db->execSqlCoro(
            "UPDATE game_sessions SET progress_data = $1::jsonb, last_activity = NOW() WHERE id = $2::uuid",
            jsonToString(stored), sessionId);
        co_return true;
    } catch (const std::exception &e) {
        LOG_ERROR << "[games] saveProgress failed: " << e.what();
        co_return false;
    }
}

drogon::Task<dto::BaseApiResponse> GameManager::saveSessionProgress(const std::string &userId,
                                                                    const std::string &sessionId,
                                                                    const Json::Value &playerState) {
    if (!isUuid(userId) || !isUuid(sessionId)) co_return fail(constants::ERR_VALIDATION, "A valid session id is required.");
    if (jsonToString(playerState).size() > 100000) co_return fail(constants::ERR_VALIDATION, "Progress payload is too large.");
    try {
        auto db = drogon::app().getDbClient();
        auto session = co_await loadOwnedSession(db, sessionId, userId);
        if (session.isComplete) co_return fail(constants::ERR_UNSUPPORTED_OPERATION, "A completed puzzle cannot be changed.");
        Json::Value patch = playerState;
        if (!patch.isMember("player") && (patch.isMember("grid") || patch.isMember("found") || patch.isMember("answers"))) {
            Json::Value wrapped;
            wrapped["player"] = patch;
            patch = wrapped;
        }
        const bool saved = co_await saveProgress(sessionId, patch);
        if (!saved) co_return fail(constants::ERR_DB_QUERY, "Progress could not be saved.");
        auto reloaded = co_await loadOwnedSession(db, sessionId, userId);
        co_return ok("Progress saved.", sessionClientJson(reloaded, false));
    } catch (const std::exception &e) {
        co_return fail(constants::ERR_RESOURCE_NOT_FOUND, "Puzzle session was not found.", e.what());
    }
}

drogon::Task<bool> GameManager::deleteSession(const std::string &sessionId) {
    if (!isUuid(sessionId)) co_return false;
    auto db = drogon::app().getDbClient();
    if (!db) co_return false;
    try {
        auto rows = co_await db->execSqlCoro(
            "DELETE FROM game_sessions WHERE id = $1::uuid AND COALESCE(is_complete, FALSE) = FALSE RETURNING id",
            sessionId);
        co_return !rows.empty();
    } catch (const std::exception &) {
        co_return false;
    }
}

drogon::Task<dto::BaseApiResponse> GameManager::removeSession(const std::string &userId, const std::string &sessionId) {
    if (!isUuid(userId) || !isUuid(sessionId)) co_return fail(constants::ERR_VALIDATION, "A valid session id is required.");
    try {
        auto db = drogon::app().getDbClient();
        auto rows = co_await db->execSqlCoro(
            "DELETE FROM game_sessions WHERE id = $1::uuid AND user_id = $2::uuid AND COALESCE(is_complete, FALSE) = FALSE RETURNING id",
            sessionId, userId);
        if (rows.empty()) co_return fail(constants::ERR_UNSUPPORTED_OPERATION, "Only your unfinished puzzles can be deleted.");
        co_return ok("Puzzle session deleted.");
    } catch (const std::exception &e) {
        co_return fail(constants::ERR_DB_QUERY, "Could not delete the puzzle session.", e.what());
    }
}

drogon::Task<int> GameManager::calculatePuzzlePoints(const std::string &gameType,
                                                     const std::string &difficulty,
                                                     double completionTime,
                                                     int mistakes,
                                                     int hintsUsed) {
    const std::string type = canonicalGameType(gameType);
    const std::string level = canonicalDifficulty(difficulty);
    const int base = basePointsFor(type.empty() ? gameType : type);
    const int multiplier = difficultyMultiplier(level.empty() ? "medium" : level);
    int points = base * multiplier;
    const int target = targetSeconds(type, level.empty() ? "medium" : level);
    if (completionTime >= 5 && completionTime < target) {
        const double ratio = (target - completionTime) / static_cast<double>(target);
        points += static_cast<int>(std::lround(base * multiplier * 0.5 * ratio));
    }
    points -= mistakes * kMistakePenalty;
    points -= hintsUsed * kHintPenalty;
    if (points < 0) points = 0;
    co_return points;
}

drogon::Task<int> GameManager::calculatePerfectBonus(const std::string &gameType,
                                                     const std::string &difficulty,
                                                     int mistakes,
                                                     int hintsUsed) {
    (void)gameType;
    if (mistakes != 0 || hintsUsed != 0) co_return 0;
    co_return constants::GamePoints::PERFECT_BONUS * getDifficultyMultiplier(difficulty);
}

drogon::Task<int> GameManager::calculateStreakBonus(const std::string &userId) {
    if (!isUuid(userId)) co_return 0;
    auto db = drogon::app().getDbClient();
    if (!db) co_return 0;
    try {
        auto rows = co_await db->execSqlCoro(
            "SELECT COALESCE(MAX(current_streak), 0)::int AS streak FROM user_game_stats WHERE user_id = $1::uuid",
            userId);
        const int streak = rows.empty() ? 0 : cellInt(rows[0], "streak");
        co_return std::min(streak, static_cast<int>(constants::GamePoints::MAX_STREAK_BONUS)) * kStreakBonusPerDay;
    } catch (...) {
        co_return 0;
    }
}

drogon::Task<GameReward> GameManager::endGameSession(const std::string &sessionId,
                                                     bool isCompleted,
                                                     int completionTimeSeconds,
                                                     int mistakes,
                                                     int hintsUsed,
                                                     const Json::Value &finalProgress) {
    GameReward reward;
    auto db = drogon::app().getDbClient();
    if (!db || !isUuid(sessionId)) {
        reward.message = "Session not found.";
        co_return reward;
    }
    auto rows = co_await db->execSqlCoro(
        "SELECT id::text AS id, user_id::text AS user_id, game_type, difficulty, progress_data::text AS progress_data, "
        "COALESCE(is_complete, FALSE) AS is_complete, COALESCE(total_points_earned, 0) AS total_points_earned "
        "FROM game_sessions WHERE id = $1::uuid",
        sessionId);
    if (rows.empty()) {
        reward.message = "Session not found.";
        co_return reward;
    }
    if (cellBool(rows[0], "is_complete")) {
        reward.pointsEarned = cellInt(rows[0], "total_points_earned");
        reward.message = "Session already completed.";
        co_return reward;
    }
    const std::string userId = cellText(rows[0], "user_id");
    const std::string gameType = gameTypeName(cellInt(rows[0], "game_type"));
    const std::string difficulty = difficultyName(cellInt(rows[0], "difficulty"));
    const int points = isCompleted ? co_await calculatePuzzlePoints(gameType, difficulty, completionTimeSeconds, mistakes, hintsUsed) : 0;
    const int perfect = isCompleted ? co_await calculatePerfectBonus(gameType, difficulty, mistakes, hintsUsed) : 0;
    const int streak = isCompleted ? co_await calculateStreakBonus(userId) : 0;
    CompletionInput input;
    input.userId = userId;
    input.sessionId = sessionId;
    input.gameType = gameType;
    input.gameTypeCode = cellInt(rows[0], "game_type");
    input.difficulty = difficulty;
    input.won = isCompleted;
    input.perfect = isCompleted && mistakes == 0 && hintsUsed == 0;
    input.puzzlePoints = points + perfect + streak;
    input.bonusPoints = perfect + streak;
    input.mistakes = mistakes;
    input.hints = hintsUsed;
    input.elapsed = completionTimeSeconds;
    input.accuracy = isCompleted ? 1.0 : 0.0;
    input.progress = parseJson(cellText(rows[0], "progress_data"));
    if (!finalProgress.empty()) input.progress["player"] = finalProgress;
    Json::Value submission;
    submission["finalize"] = true;
    co_return co_await persistAndReward(input);
}

drogon::Task<dto::BaseApiResponse> GameManager::takeHint(const std::string &userId, const std::string &sessionId) {
    if (!isUuid(userId) || !isUuid(sessionId)) co_return fail(constants::ERR_VALIDATION, "A valid session id is required.");
    try {
        auto db = drogon::app().getDbClient();
        auto session = co_await loadOwnedSession(db, sessionId, userId);
        if (session.isComplete) co_return fail(constants::ERR_UNSUPPORTED_OPERATION, "This puzzle is already complete.");
        const int cap = hintCapFor(session.difficulty);
        if (session.hintsUsed >= cap) co_return fail(constants::ERR_QUOTA_EXCEEDED, "No hints left on this puzzle.");
        HintResult hint = PuzzleGenerator::revealHint(session.progressData);
        if (!hint.ok) co_return fail(constants::ERR_UNSUPPORTED_OPERATION, hint.error.empty() ? "No hint available." : hint.error);
        const int hints = session.hintsUsed + 1;
        co_await db->execSqlCoro(
            "UPDATE game_sessions SET hints_used = $1, progress_data = $2::jsonb, last_activity = NOW() "
            "WHERE id = $3::uuid AND user_id = $4::uuid",
            hints, jsonToString(session.progressData), sessionId, userId);
        Json::Value result = sessionClientJson(session, false);
        result["hintsUsed"] = hints;
        result["hintsRemaining"] = std::max(0, cap - hints);
        result["hint"] = hint.hint;
        co_return ok("Hint revealed. It will reduce your score.", result);
    } catch (const std::exception &e) {
        co_return fail(constants::ERR_RESOURCE_NOT_FOUND, "Puzzle session was not found.", e.what());
    }
}

drogon::Task<GameReward> persistAndReward(const CompletionInput &input) {
    GameReward reward;
    auto db = drogon::app().getDbClient();
    if (!db) {
        reward.message = "Database client is not configured.";
        co_return reward;
    }
    auto transaction = co_await db->newTransactionCoro();
    try {
        auto locked = co_await transaction->execSqlCoro(
            "SELECT COALESCE(is_complete, FALSE) AS is_complete, COALESCE(total_points_earned, 0) AS total_points_earned, "
            "progress_data::text AS progress_data FROM game_sessions WHERE id = $1::uuid AND user_id = $2::uuid FOR UPDATE",
            input.sessionId, input.userId);
        if (locked.empty()) {
            transaction->rollback();
            reward.message = "Session not found.";
            co_return reward;
        }
        if (cellBool(locked[0], "is_complete")) {
            transaction->rollback();
            reward.pointsEarned = cellInt(locked[0], "total_points_earned");
            reward.message = "Session already completed.";
            Json::Value stored = parseJson(cellText(locked[0], "progress_data"));
            if (stored.isMember("reward") && stored["reward"].isMember("newAchievements")) {
                for (const auto &name : stored["reward"]["newAchievements"]) reward.newAchievements.push_back(name.asString());
            }
            co_return reward;
        }

        auto todayRows = co_await transaction->execSqlCoro("SELECT CURRENT_DATE::text AS today");
        const std::string today = todayRows.empty() ? "" : cellText(todayRows[0], "today");

        auto statRows = co_await transaction->execSqlCoro(
            "SELECT id::text AS id, game_type, COALESCE(total_points, 0) AS total_points, COALESCE(games_played, 0) AS games_played, "
            "COALESCE(games_won, 0) AS games_won, COALESCE(games_lost, 0) AS games_lost, COALESCE(games_draw, 0) AS games_draw, "
            "COALESCE(current_streak, 0) AS current_streak, COALESCE(longest_streak, 0) AS longest_streak, "
            "COALESCE(last_played::text, '') AS last_played "
            "FROM user_game_stats WHERE user_id = $1::uuid",
            input.userId);

        int played = 0, won = 0, lost = 0, draw = 0, typePlayed = 0, typeWon = 0, typeLost = 0, typePoints = 0;
        int streak = 0, longest = 0;
        std::string lastPlayed;
        std::string statsId;
        for (size_t i = 0; i < statRows.size(); ++i) {
            const int type = cellInt(statRows[i], "game_type", -1);
            played += cellInt(statRows[i], "games_played");
            won += cellInt(statRows[i], "games_won");
            if (type == input.gameTypeCode) {
                statsId = cellText(statRows[i], "id");
                typePlayed = cellInt(statRows[i], "games_played");
                typeWon = cellInt(statRows[i], "games_won");
                typeLost = cellInt(statRows[i], "games_lost");
                typePoints = cellInt(statRows[i], "total_points");
                lastPlayed = cellText(statRows[i], "last_played");
                streak = cellInt(statRows[i], "current_streak");
                longest = cellInt(statRows[i], "longest_streak");
            }
        }

        const bool countOutcome = input.countWinLoss;
        const int newTypePlayed = typePlayed + 1;
        const int newTypeWon = typeWon + (countOutcome && input.won ? 1 : 0);
        const int newTypeLost = typeLost + (countOutcome && !input.won ? 1 : 0);
        const int newStreak = nextStreak(streak, lastPlayed, today, input.won);
        const int newLongest = std::max(longest, newStreak);
        const int globalPlayed = played + 1;
        const int globalWon = won + (countOutcome && input.won ? 1 : 0);

        int dailyBonus = 0;
        if (!input.challengeId.empty() && input.dailyBonusAvailable > 0 &&
            (input.won || (input.allowPartialDaily && input.accuracy * 100 >= kPartialDailyThreshold))) {
            auto claimed = co_await transaction->execSqlCoro(
                "SELECT id::text AS id FROM daily_challenge_completions WHERE user_id = $1::uuid AND challenge_id = $2::uuid",
                input.userId, input.challengeId);
            if (claimed.empty()) {
                dailyBonus = input.won ? input.dailyBonusAvailable
                                       : static_cast<int>(std::lround(input.dailyBonusAvailable * input.accuracy));
                co_await transaction->execSqlCoro(
                    "INSERT INTO daily_challenge_completions (id, user_id, challenge_id, session_id, completed_at, bonus_earned) "
                    "VALUES ($1::uuid, $2::uuid, $3::uuid, $4::uuid, NOW(), $5)",
                    utils::IdGeneratorUtils::generateGuid(), input.userId, input.challengeId, input.sessionId, dailyBonus);
            }
        }

        auto boardRows = co_await transaction->execSqlCoro(
            "SELECT id::text AS id, COALESCE(total_points, 0) AS total_points FROM leaderboard WHERE user_id = $1::uuid",
            input.userId);
        const int oldGlobal = boardRows.empty() ? 0 : cellInt(boardRows[0], "total_points");
        int points = input.puzzlePoints + dailyBonus;
        int projectedGlobal = oldGlobal + points;
        const int perfectCountBase = cellInt((co_await transaction->execSqlCoro(
            "SELECT COUNT(*)::int AS n FROM game_sessions WHERE user_id = $1::uuid AND COALESCE(is_complete, FALSE) = TRUE "
            "AND COALESCE(mistakes, 0) = 0 AND COALESCE(hints_used, 0) = 0 AND COALESCE(total_points_earned, 0) > 0",
            input.userId))[0], "n");
        const int perfectCount = perfectCountBase + (input.perfect ? 1 : 0);
        const int dailyDaysBase = cellInt((co_await transaction->execSqlCoro(
            "SELECT COUNT(DISTINCT completed_at::date)::int AS n FROM daily_challenge_completions WHERE user_id = $1::uuid",
            input.userId))[0], "n");

        auto unlockedRows = co_await transaction->execSqlCoro(
            "SELECT achievement_id::text AS achievement_id FROM user_achievements WHERE user_id = $1::uuid", input.userId);
        std::unordered_set<std::string> unlocked;
        for (size_t i = 0; i < unlockedRows.size(); ++i) unlocked.insert(cellText(unlockedRows[i], "achievement_id"));

        auto definitions = co_await transaction->execSqlCoro(
            "SELECT id::text AS id, name, description, COALESCE(points_required, 0) AS points_required, "
            "COALESCE(games_required, 0) AS games_required, COALESCE(game_type, '') AS game_type, "
            "COALESCE(achievement_type, '') AS achievement_type, COALESCE(bonus_points, 0) AS bonus_points "
            "FROM achievements_definitions");

        int achievementBonus = 0;
        Json::Value newAchievementJson(Json::arrayValue);
        for (int pass = 0; pass < 4; ++pass) {
            bool awarded = false;
            for (size_t i = 0; i < definitions.size(); ++i) {
                const std::string id = cellText(definitions[i], "id");
                if (id.empty() || unlocked.count(id)) continue;
                const std::string type = cellText(definitions[i], "achievement_type");
                const std::string requiredGame = cellText(definitions[i], "game_type");
                const int needPoints = cellInt(definitions[i], "points_required");
                const int needGames = cellInt(definitions[i], "games_required");
                const bool typeOk = achievementMatches(requiredGame, input.gameType);
                const int relevantPlayed = typeOk && requiredGame != "any" && !requiredGame.empty() ? newTypePlayed : globalPlayed;
                const int relevantWon = typeOk && requiredGame != "any" && !requiredGame.empty() ? newTypeWon : globalWon;
                const int relevantStreak = std::max(newStreak, newLongest);
                bool eligible = false;
                if (type == "points") eligible = projectedGlobal >= needPoints;
                else if (type == "streak") eligible = typeOk && relevantStreak >= needGames;
                else if (type == "perfect") eligible = perfectCount >= std::max(needGames, 1);
                else if (type == "daily") eligible = dailyDaysBase >= std::max(needGames, 1);
                else if (type == "wins") eligible = typeOk && relevantWon >= needGames;
                else eligible = typeOk && relevantPlayed >= std::max(needGames, 1);
                if (!eligible) continue;

                const int bonus = cellInt(definitions[i], "bonus_points", constants::GamePoints::ACHIEVEMENT_BONUS);
                co_await transaction->execSqlCoro(
                    "INSERT INTO user_achievements (id, user_id, achievement_id, achievement_name, achievement_description, unlocked_at) "
                    "VALUES ($1::uuid, $2::uuid, $3::uuid, $4, $5, NOW())",
                    utils::IdGeneratorUtils::generateGuid(), input.userId, id, cellText(definitions[i], "name"), cellText(definitions[i], "description"));
                unlocked.insert(id);
                achievementBonus += bonus;
                projectedGlobal += bonus;
                points += bonus;
                reward.newAchievements.push_back(cellText(definitions[i], "name"));
                Json::Value item;
                item["id"] = id;
                item["name"] = cellText(definitions[i], "name");
                item["description"] = cellText(definitions[i], "description");
                item["bonusPoints"] = bonus;
                newAchievementJson.append(item);
                awarded = true;
            }
            if (!awarded) break;
        }

        const double typeWinRate = newTypePlayed == 0 ? 0.0 : static_cast<double>(newTypeWon) / static_cast<double>(newTypePlayed);
        const int newTypePoints = typePoints + points;
        if (statsId.empty()) {
            co_await transaction->execSqlCoro(
                "INSERT INTO user_game_stats (id, user_id, game_type, total_points, games_played, games_won, games_lost, games_draw, "
                "current_streak, longest_streak, win_rate, last_played, updated_at) "
                "VALUES ($1::uuid, $2::uuid, $3, $4, $5, $6, $7, 0, $8, $9, $10, NOW(), NOW())",
                utils::IdGeneratorUtils::generateGuid(), input.userId, input.gameTypeCode, newTypePoints, newTypePlayed, newTypeWon,
                countOutcome && !input.won ? 1 : 0, newStreak, newLongest, typeWinRate);
        } else {
            co_await transaction->execSqlCoro(
                "UPDATE user_game_stats SET total_points = $1, games_played = $2, games_won = $3, games_lost = $4, "
                "current_streak = $5, longest_streak = $6, win_rate = $7, last_played = CASE WHEN $8 THEN NOW() ELSE last_played END, "
                "updated_at = NOW() WHERE id = $9::uuid",
                newTypePoints, newTypePlayed, newTypeWon, newTypeLost, newStreak, newLongest, typeWinRate, input.won, statsId);
        }

        const int globalPlayedFinal = globalPlayed;
        const int globalWonFinal = globalWon;
        const double globalWinRate = globalPlayedFinal == 0 ? 0.0 : static_cast<double>(globalWonFinal) / static_cast<double>(globalPlayedFinal);
        if (boardRows.empty()) {
            co_await transaction->execSqlCoro(
                "INSERT INTO leaderboard (id, user_id, total_points, games_played, win_rate, last_active, updated_at) "
                "VALUES ($1::uuid, $2::uuid, $3, $4, $5, NOW(), NOW())",
                utils::IdGeneratorUtils::generateGuid(), input.userId, points, globalPlayedFinal, globalWinRate);
        } else {
            co_await transaction->execSqlCoro(
                "UPDATE leaderboard SET total_points = $1, games_played = $2, win_rate = $3, last_active = NOW(), updated_at = NOW() "
                "WHERE user_id = $4::uuid",
                projectedGlobal, globalPlayedFinal, globalWinRate, input.userId);
        }

        auto typeBoard = co_await transaction->execSqlCoro(
            "SELECT id::text AS id FROM game_type_leaderboard WHERE user_id = $1::uuid AND game_type = $2",
            input.userId, input.gameTypeCode);
        if (typeBoard.empty()) {
            co_await transaction->execSqlCoro(
                "INSERT INTO game_type_leaderboard (id, user_id, game_type, points, games_played, updated_at) "
                "VALUES ($1::uuid, $2::uuid, $3, $4, $5, NOW())",
                utils::IdGeneratorUtils::generateGuid(), input.userId, input.gameTypeCode, points, newTypePlayed);
        } else {
            co_await transaction->execSqlCoro(
                "UPDATE game_type_leaderboard SET points = $1, games_played = $2, updated_at = NOW() WHERE id = $3::uuid",
                newTypePoints, newTypePlayed, cellText(typeBoard[0], "id"));
        }

        co_await transaction->execSqlCoro(
            "INSERT INTO user_game_history (id, user_id, session_id, game_type, points_earned, completed_at) "
            "VALUES ($1::uuid, $2::uuid, $3::uuid, $4, $5, NOW())",
            utils::IdGeneratorUtils::generateGuid(), input.userId, input.sessionId, input.gameTypeCode, points);

        Json::Value progress = input.progress;
        progress["status"] = "complete";
        Json::Value rewardJson;
        rewardJson["pointsEarned"] = points;
        rewardJson["puzzlePoints"] = input.puzzlePoints;
        rewardJson["bonusPoints"] = input.bonusPoints + dailyBonus + achievementBonus;
        rewardJson["dailyBonus"] = dailyBonus;
        rewardJson["achievementBonus"] = achievementBonus;
        rewardJson["newAchievements"] = newAchievementJson;
        rewardJson["accuracy"] = input.accuracy;
        rewardJson["won"] = input.won;
        rewardJson["perfect"] = input.perfect;
        rewardJson["streak"] = newStreak;
        progress["reward"] = rewardJson;

        co_await transaction->execSqlCoro(
            "UPDATE game_sessions SET is_complete = TRUE, total_points_earned = $1, completion_time_seconds = $2, "
            "mistakes = $3, hints_used = $4, progress_data = $5::jsonb, ended_at = NOW(), last_activity = NOW() "
            "WHERE id = $6::uuid",
            points, input.elapsed, input.mistakes, input.hints, jsonToString(progress), input.sessionId);

        reward.pointsEarned = points;
        reward.bonusPoints = input.bonusPoints + dailyBonus + achievementBonus;
        reward.isNewAchievement = !reward.newAchievements.empty();
        reward.achievementName = reward.newAchievements.empty() ? "" : reward.newAchievements.front();
        std::ostringstream message;
        message << (input.won ? "Solved" : "Submitted") << ". +" << points << " points";
        if (!reward.newAchievements.empty()) message << ". Unlocked " << reward.newAchievements.front();
        reward.message = message.str();
        co_return reward;
    } catch (const std::exception &e) {
        try { transaction->rollback(); } catch (...) {}
        LOG_ERROR << "[games] persist completion failed: " << e.what();
        throw;
    }
}

drogon::Task<dto::BaseApiResponse> GameManager::submitSession(const std::string &userId,
                                                             const std::string &sessionId,
                                                             const Json::Value &submission) {
    if (!isUuid(userId) || !isUuid(sessionId)) co_return fail(constants::ERR_VALIDATION, "A valid session id is required.");
    try {
        co_await ensureAchievementCatalog();
        auto db = drogon::app().getDbClient();
        auto session = co_await loadOwnedSession(db, sessionId, userId);
        if (session.isComplete) {
            Json::Value result = sessionClientJson(session, true);
            co_return ok("This puzzle was already submitted.", result);
        }
        const bool finalize = !submission.isMember("finalize") || submission["finalize"].asBool();
        GradeResult grade = PuzzleGenerator::grade(session.progressData, submission);
        if (!finalize) {
            session.progressData["player"]["lastCheck"] = grade.review;
            if (submission.isMember("grid")) session.progressData["player"]["grid"] = submission["grid"];
            if (submission.isMember("found")) session.progressData["player"]["found"] = submission["found"];
            if (submission.isMember("answers")) session.progressData["player"]["answers"] = submission["answers"];
            int mistakes = session.mistakes + (grade.wrong > 0 ? 1 : 0);
            co_await db->execSqlCoro(
                "UPDATE game_sessions SET mistakes = $1, progress_data = $2::jsonb, last_activity = NOW() WHERE id = $3::uuid",
                mistakes, jsonToString(session.progressData), sessionId);
            Json::Value result = sessionClientJson(session, false);
            result["mistakes"] = mistakes;
            result["check"] = grade.review;
            result["check"].removeMember("placements");
            result["check"].removeMember("grid");
            result["check"].removeMember("items");
            result["accuracy"] = grade.accuracy;
            result["correct"] = grade.correct;
            result["total"] = grade.total;
            co_return ok(grade.wrong > 0 ? "Some answers are still wrong." : "Looking good so far.", result);
        }

        const auto elapsedCount = std::chrono::duration_cast<std::chrono::seconds>(
                                       std::chrono::system_clock::now() - session.startTime)
                                       .count();
        int elapsed = static_cast<int>(std::max<int64_t>(0, elapsedCount));
        if (elapsed > 6 * 60 * 60) elapsed = 6 * 60 * 60;
        const int multiplier = difficultyMultiplier(session.difficulty);
        const int base = basePointsFor(session.gameType);
        const int accuracyPoints = static_cast<int>(std::lround(base * multiplier * grade.accuracy));
        int timeBonus = 0;
        const int target = targetSeconds(session.gameType, session.difficulty);
        if (grade.solved && elapsed >= 5 && elapsed < target) {
            const double ratio = static_cast<double>(target - elapsed) / static_cast<double>(target);
            timeBonus = static_cast<int>(std::lround(base * multiplier * 0.5 * ratio));
        }
        const int streakBonus = grade.solved ? co_await calculateStreakBonus(userId) : 0;
        const int perfectBonus = (grade.solved && grade.wrong == 0 && session.hintsUsed == 0)
                                     ? constants::GamePoints::PERFECT_BONUS * multiplier
                                     : 0;
        const int mistakeCount = session.mistakes + grade.wrong;
        int puzzlePoints = accuracyPoints + timeBonus + perfectBonus + streakBonus -
                           mistakeCount * kMistakePenalty - session.hintsUsed * kHintPenalty;
        if (puzzlePoints < 0 || grade.accuracy <= 0) puzzlePoints = 0;

        if (submission.isMember("grid")) session.progressData["player"]["grid"] = submission["grid"];
        if (submission.isMember("found")) session.progressData["player"]["found"] = submission["found"];
        if (submission.isMember("answers")) session.progressData["player"]["answers"] = submission["answers"];
        Json::Value breakdown;
        breakdown["base"] = base;
        breakdown["difficultyMultiplier"] = multiplier;
        breakdown["accuracy"] = grade.accuracy;
        breakdown["accuracyPoints"] = accuracyPoints;
        breakdown["timeBonus"] = timeBonus;
        breakdown["perfectBonus"] = perfectBonus;
        breakdown["streakBonus"] = streakBonus;
        breakdown["mistakePenalty"] = mistakeCount * kMistakePenalty;
        breakdown["hintPenalty"] = session.hintsUsed * kHintPenalty;
        breakdown["elapsedSeconds"] = elapsed;
        session.progressData["breakdown"] = breakdown;

        CompletionInput input;
        input.userId = userId;
        input.sessionId = sessionId;
        input.gameType = session.gameType;
        input.gameTypeCode = gameTypeCode(session.gameType);
        input.difficulty = session.difficulty;
        input.won = grade.solved;
        input.perfect = grade.solved && grade.wrong == 0 && session.hintsUsed == 0;
        input.countWinLoss = !session.isMultiplayer;
        input.puzzlePoints = puzzlePoints;
        input.bonusPoints = perfectBonus + streakBonus;
        input.mistakes = mistakeCount;
        input.hints = session.hintsUsed;
        input.elapsed = elapsed;
        input.accuracy = grade.accuracy;
        input.challengeId = session.progressData.get("challengeId", "").asString();
        input.dailyBonusAvailable = session.progressData.get("dailyBonus", 0).asInt();
        if (input.dailyBonusAvailable == 0 && session.progressData.get("daily", false).asBool()) {
            input.dailyBonusAvailable = constants::GamePoints::DAILY_CHALLENGE_BONUS;
        }
        input.allowPartialDaily = session.gameType == "riddle";
        input.progress = session.progressData;

        const int rankBefore = co_await rankForPoints(userId);
        GameReward reward = co_await persistAndReward(input);
        auto finished = co_await loadOwnedSession(db, sessionId, userId);
        if (session.isMultiplayer) {
            co_await finishMultiplayer(userId, sessionId);
            finished = co_await loadOwnedSession(db, sessionId, userId);
        }
        const int rankAfter = co_await rankForPoints(userId);
        Json::Value result = sessionClientJson(finished, true);
        result["reward"]["pointsEarned"] = reward.pointsEarned;
        result["reward"]["bonusPoints"] = reward.bonusPoints;
        result["reward"]["message"] = reward.message;
        result["reward"]["breakdown"] = finished.progressData.get("breakdown", breakdown);
        result["reward"]["rankBefore"] = rankBefore;
        result["reward"]["rankAfter"] = rankAfter;
        result["reward"]["rankChange"] = rankBefore > 0 && rankAfter > 0 ? rankBefore - rankAfter : 0;
        Json::Value unlocked(Json::arrayValue);
        for (const auto &name : reward.newAchievements) unlocked.append(name);
        result["reward"]["newAchievements"] = unlocked;
        result["correct"] = grade.correct;
        result["total"] = grade.total;
        result["accuracy"] = grade.accuracy;
        co_return ok(reward.message.empty() ? "Puzzle submitted." : reward.message, result);
    } catch (const drogon::orm::DrogonDbException &e) {
        co_return fail(constants::ERR_DB_QUERY, "Database error while submitting the puzzle.", e.base().what());
    } catch (const std::exception &e) {
        co_return fail(constants::ERR_INTERNAL, "Could not submit the puzzle.", e.what());
    }
}

drogon::Task<int> rankForPoints(const std::string &userId) {
    if (!isUuid(userId)) co_return 0;
    auto db = drogon::app().getDbClient();
    if (!db) co_return 0;
    try {
        auto rows = co_await db->execSqlCoro(
            "SELECT COALESCE(total_points, 0) AS total_points FROM leaderboard WHERE user_id = $1::uuid", userId);
        if (rows.empty()) co_return 0;
        auto rankRows = co_await db->execSqlCoro(
            "SELECT (COUNT(*) + 1)::int AS rank FROM leaderboard WHERE total_points > $1", cellInt(rows[0], "total_points"));
        co_return rankRows.empty() ? 0 : cellInt(rankRows[0], "rank");
    } catch (...) {
        co_return 0;
    }
}

drogon::Task<bool> GameManager::addPoints(const std::string &userId, int points, const std::string &reason) {
    if (!isUuid(userId) || points == 0) co_return false;
    auto db = drogon::app().getDbClient();
    if (!db) co_return false;
    try {
        auto rows = co_await db->execSqlCoro(
            "SELECT id::text AS id, COALESCE(total_points, 0) AS total_points, COALESCE(games_played, 0) AS games_played, "
            "COALESCE(win_rate, 0) AS win_rate FROM leaderboard WHERE user_id = $1::uuid",
            userId);
        if (rows.empty()) {
            co_await db->execSqlCoro(
                "INSERT INTO leaderboard (id, user_id, total_points, games_played, win_rate, last_active, updated_at) "
                "VALUES ($1::uuid, $2::uuid, $3, 0, 0, NOW(), NOW())",
                generateSessionId(), userId, std::max(0, points));
        } else {
            co_await db->execSqlCoro(
                "UPDATE leaderboard SET total_points = GREATEST(0, total_points + $1), updated_at = NOW(), last_active = NOW() "
                "WHERE user_id = $2::uuid",
                points, userId);
        }
        LOG_INFO << "[games] Added " << points << " points to " << userId << " (" << reason << ")";
        co_return true;
    } catch (const std::exception &e) {
        LOG_ERROR << "[games] addPoints failed: " << e.what();
        co_return false;
    }
}

drogon::Task<int> GameManager::getUserPoints(const std::string &userId) {
    if (!isUuid(userId)) co_return 0;
    auto db = drogon::app().getDbClient();
    if (!db) co_return 0;
    try {
        auto rows = co_await db->execSqlCoro(
            "SELECT COALESCE(total_points, 0) AS total_points FROM leaderboard WHERE user_id = $1::uuid", userId);
        if (!rows.empty()) co_return cellInt(rows[0], "total_points");
        auto sum = co_await db->execSqlCoro(
            "SELECT COALESCE(SUM(total_points), 0)::int AS total_points FROM user_game_stats WHERE user_id = $1::uuid", userId);
        co_return sum.empty() ? 0 : cellInt(sum[0], "total_points");
    } catch (...) {
        co_return 0;
    }
}

drogon::Task<UserStats> GameManager::getUserStats(const std::string &userId) {
    UserStats stats;
    stats.userId = userId;
    if (!isUuid(userId)) co_return stats;
    auto db = drogon::app().getDbClient();
    if (!db) co_return stats;
    auto users = co_await db->execSqlCoro(
        "SELECT username, first_name, last_name FROM users WHERE id = $1::uuid", userId);
    if (!users.empty()) {
        stats.username = displayName(cellText(users[0], "username"), cellText(users[0], "first_name"), cellText(users[0], "last_name"));
    }
    auto rows = co_await db->execSqlCoro(
        "SELECT game_type, COALESCE(total_points, 0) AS total_points, COALESCE(games_played, 0) AS games_played, "
        "COALESCE(games_won, 0) AS games_won, COALESCE(games_lost, 0) AS games_lost, COALESCE(games_draw, 0) AS games_draw, "
        "COALESCE(current_streak, 0) AS current_streak, COALESCE(longest_streak, 0) AS longest_streak, last_played "
        "FROM user_game_stats WHERE user_id = $1::uuid",
        userId);
    for (size_t i = 0; i < rows.size(); ++i) {
        const std::string type = gameTypeName(cellInt(rows[i], "game_type"));
        const int points = cellInt(rows[i], "total_points");
        const int played = cellInt(rows[i], "games_played");
        stats.totalPoints += points;
        stats.gamesPlayed += played;
        stats.gamesWon += cellInt(rows[i], "games_won");
        stats.gamesLost += cellInt(rows[i], "games_lost");
        stats.gamesDraw += cellInt(rows[i], "games_draw");
        stats.currentStreak = std::max(stats.currentStreak, cellInt(rows[i], "current_streak"));
        stats.longestStreak = std::max(stats.longestStreak, cellInt(rows[i], "longest_streak"));
        if (!type.empty()) {
            stats.gameTypePoints[type] = points;
            stats.gameTypePlayed[type] = played;
        }
        try {
            if (!rows[i]["last_played"].isNull()) {
                auto playedAt = fromTrantor(trantor::Date::fromDbStringLocal(rows[i]["last_played"].as<std::string>()));
                if (playedAt > stats.lastPlayed) stats.lastPlayed = playedAt;
            }
        } catch (...) {}
    }
    auto board = co_await db->execSqlCoro(
        "SELECT COALESCE(total_points, 0) AS total_points FROM leaderboard WHERE user_id = $1::uuid", userId);
    if (!board.empty()) stats.totalPoints = cellInt(board[0], "total_points");
    stats.winRate = calculateWinRate(stats);
    auto achievements = co_await getUserAchievements(userId);
    for (const auto &achievement : achievements) stats.achievements.push_back(achievement.name);
    co_return stats;
}

drogon::Task<dto::BaseApiResponse> GameManager::statsView(const std::string &userId) {
    if (!isUuid(userId)) co_return fail(constants::ERR_VALIDATION, "A valid user id is required.");
    try {
        auto stats = co_await getUserStats(userId);
        const int points = co_await getUserPoints(userId);
        const int rank = co_await rankForPoints(userId);
        Json::Value result;
        result["userId"] = stats.userId;
        result["username"] = stats.username;
        result["totalPoints"] = points;
        result["gamesPlayed"] = stats.gamesPlayed;
        result["gamesWon"] = stats.gamesWon;
        result["gamesLost"] = stats.gamesLost;
        result["gamesDraw"] = stats.gamesDraw;
        result["currentStreak"] = stats.currentStreak;
        result["longestStreak"] = stats.longestStreak;
        result["winRate"] = stats.winRate;
        result["rank"] = rank;
        Json::Value byType(Json::objectValue);
        for (const auto &entry : stats.gameTypePlayed) {
            Json::Value item;
            item["played"] = entry.second;
            item["points"] = stats.gameTypePoints[entry.first];
            byType[entry.first] = item;
        }
        result["byGameType"] = byType;
        Json::Value achievements(Json::arrayValue);
        for (const auto &name : stats.achievements) achievements.append(name);
        result["achievements"] = achievements;
        auto db = drogon::app().getDbClient();
        auto ahead = co_await db->execSqlCoro(
            "SELECT COALESCE(MIN(total_points), 0) AS next_points FROM leaderboard WHERE total_points > $1", points);
        const int nextPoints = ahead.empty() ? points : cellInt(ahead[0], "next_points");
        result["pointsToNextRank"] = std::max(0, nextPoints - points);
        co_return ok("Stats loaded.", result);
    } catch (const std::exception &e) {
        co_return fail(constants::ERR_DB_QUERY, "Could not load game stats.", e.what());
    }
}

drogon::Task<dto::BaseApiResponse> GameManager::pointsView(const std::string &userId) {
    Json::Value result;
    result["userId"] = userId;
    result["totalPoints"] = co_await getUserPoints(userId);
    result["rank"] = co_await rankForPoints(userId);
    co_return ok("Points loaded.", result);
}

drogon::Task<std::vector<GameSession>> GameManager::getGameHistory(const std::string &userId, int pageNo, int pageSize) {
    std::vector<GameSession> history;
    if (!isUuid(userId)) co_return history;
    auto db = drogon::app().getDbClient();
    if (!db) co_return history;
    pageNo = clampPage(pageNo);
    pageSize = clampSize(pageSize);
    auto rows = co_await db->execSqlCoro(
        "SELECT id::text AS id, user_id::text AS user_id, game_type, puzzle_id, difficulty, start_time, last_activity, "
        "is_multiplayer, opponent_id::text AS opponent_id, is_complete, total_points_earned, completion_time_seconds, "
        "mistakes, hints_used, progress_data::text AS progress_data, ended_at "
        "FROM game_sessions WHERE user_id = $1::uuid ORDER BY start_time DESC NULLS LAST LIMIT $2 OFFSET $3",
        userId, pageSize, (pageNo - 1) * pageSize);
    for (size_t i = 0; i < rows.size(); ++i) {
        auto session = sessionFromRow(rows[i]);
        session.progressData = PuzzleGenerator::publicMeta(session.progressData);
        history.push_back(std::move(session));
    }
    co_return history;
}

drogon::Task<dto::BaseApiResponse> GameManager::historyView(const std::string &userId, int pageNo, int pageSize) {
    if (!isUuid(userId)) co_return fail(constants::ERR_VALIDATION, "A valid user id is required.");
    try {
        pageNo = clampPage(pageNo);
        pageSize = clampSize(pageSize);
        auto db = drogon::app().getDbClient();
        auto countRows = co_await db->execSqlCoro(
            "SELECT COUNT(*)::int AS n FROM game_sessions WHERE user_id = $1::uuid", userId);
        const int total = countRows.empty() ? 0 : cellInt(countRows[0], "n");
        auto history = co_await getGameHistory(userId, pageNo, pageSize);
        Json::Value data(Json::arrayValue);
        for (const auto &session : history) {
            Json::Value item = sessionClientJson(session, false);
            item.removeMember("puzzle");
            item.removeMember("player");
            data.append(item);
        }
        Json::Value result;
        result["data"] = data;
        result["totalCount"] = total;
        result["pageNo"] = pageNo;
        result["pageSize"] = pageSize;
        result["totalPages"] = pageSize == 0 ? 0 : (total + pageSize - 1) / pageSize;
        co_return ok("Game history loaded.", result);
    } catch (const std::exception &e) {
        co_return fail(constants::ERR_DB_QUERY, "Could not load game history.", e.what());
    }
}

drogon::Task<std::vector<Achievement>> GameManager::getAvailableAchievements() {
    co_await ensureAchievementCatalog();
    std::vector<Achievement> items;
    auto db = drogon::app().getDbClient();
    if (!db) co_return items;
    auto rows = co_await db->execSqlCoro(
        "SELECT id::text AS id, name, description, COALESCE(points_required, 0) AS points_required, "
        "COALESCE(games_required, 0) AS games_required, COALESCE(game_type, '') AS game_type, "
        "COALESCE(achievement_type, '') AS achievement_type, COALESCE(bonus_points, 0) AS bonus_points "
        "FROM achievements_definitions ORDER BY bonus_points, name");
    for (size_t i = 0; i < rows.size(); ++i) {
        Achievement item;
        item.id = cellText(rows[i], "id");
        item.name = cellText(rows[i], "name");
        item.description = cellText(rows[i], "description");
        item.pointsRequired = cellInt(rows[i], "points_required");
        item.gamesRequired = cellInt(rows[i], "games_required");
        item.gameType = cellText(rows[i], "game_type");
        item.achievementType = cellText(rows[i], "achievement_type");
        item.bonusPoints = cellInt(rows[i], "bonus_points");
        items.push_back(std::move(item));
    }
    co_return items;
}

drogon::Task<std::vector<Achievement>> GameManager::getUserAchievements(const std::string &userId) {
    std::vector<Achievement> items;
    if (!isUuid(userId)) co_return items;
    auto db = drogon::app().getDbClient();
    if (!db) co_return items;
    auto rows = co_await db->execSqlCoro(
        "SELECT ua.achievement_id::text AS id, ua.achievement_name AS name, ua.achievement_description AS description, ua.unlocked_at, "
        "COALESCE(ad.bonus_points, 0) AS bonus_points, COALESCE(ad.achievement_type, '') AS achievement_type, "
        "COALESCE(ad.game_type, '') AS game_type, COALESCE(ad.points_required, 0) AS points_required, "
        "COALESCE(ad.games_required, 0) AS games_required "
        "FROM user_achievements ua "
        "LEFT JOIN achievements_definitions ad ON ad.id = ua.achievement_id "
        "WHERE ua.user_id = $1::uuid ORDER BY ua.unlocked_at DESC",
        userId);
    for (size_t i = 0; i < rows.size(); ++i) {
        Achievement item;
        item.id = cellText(rows[i], "id");
        item.name = cellText(rows[i], "name");
        item.description = cellText(rows[i], "description");
        item.bonusPoints = cellInt(rows[i], "bonus_points");
        item.achievementType = cellText(rows[i], "achievement_type");
        item.gameType = cellText(rows[i], "game_type");
        item.pointsRequired = cellInt(rows[i], "points_required");
        item.gamesRequired = cellInt(rows[i], "games_required");
        item.isUnlocked = true;
        try {
            if (!rows[i]["unlocked_at"].isNull()) item.unlockedAt = fromTrantor(trantor::Date::fromDbStringLocal(rows[i]["unlocked_at"].as<std::string>()));
        } catch (...) {}
        items.push_back(std::move(item));
    }
    co_return items;
}

Json::Value achievementJson(const Achievement &item) {
    Json::Value json;
    json["id"] = item.id;
    json["name"] = item.name;
    json["description"] = item.description;
    json["pointsRequired"] = item.pointsRequired;
    json["gamesRequired"] = item.gamesRequired;
    json["gameType"] = item.gameType;
    json["achievementType"] = item.achievementType;
    json["bonusPoints"] = item.bonusPoints;
    json["isUnlocked"] = item.isUnlocked;
    return json;
}

drogon::Task<dto::BaseApiResponse> GameManager::achievementsView(const std::string &userId, bool mineOnly) {
    try {
        auto catalog = co_await getAvailableAchievements();
        std::vector<Achievement> mine;
        if (isUuid(userId)) mine = co_await getUserAchievements(userId);
        std::unordered_set<std::string> unlocked;
        for (const auto &item : mine) unlocked.insert(item.id);
        Json::Value data(Json::arrayValue);
        if (mineOnly) {
            for (const auto &item : mine) data.append(achievementJson(item));
        } else {
            for (auto item : catalog) {
                item.isUnlocked = unlocked.count(item.id) > 0;
                data.append(achievementJson(item));
            }
        }
        Json::Value result;
        result["data"] = data;
        result["unlockedCount"] = static_cast<int>(mine.size());
        co_return ok("Achievements loaded.", result);
    } catch (const std::exception &e) {
        co_return fail(constants::ERR_DB_QUERY, "Could not load achievements.", e.what());
    }
}

drogon::Task<bool> GameManager::isEligibleForAchievement(const std::string &userId, const std::string &achievementId) {
    if (!isUuid(userId) || !isUuid(achievementId)) co_return false;
    auto stats = co_await getUserStats(userId);
    auto catalog = co_await getAvailableAchievements();
    for (const auto &item : catalog) {
        if (item.id != achievementId) continue;
        if (item.achievementType == "points") co_return stats.totalPoints >= item.pointsRequired;
        if (item.achievementType == "streak") co_return stats.longestStreak >= item.gamesRequired || stats.currentStreak >= item.gamesRequired;
        if (item.achievementType == "games" || item.achievementType.empty()) {
            if (item.gameType.empty() || item.gameType == "any") co_return stats.gamesPlayed >= item.gamesRequired;
            co_return stats.gameTypePlayed[item.gameType] >= item.gamesRequired;
        }
    }
    co_return false;
}

drogon::Task<bool> GameManager::awardAchievement(const std::string &userId, const std::string &achievementId) {
    if (!co_await isEligibleForAchievement(userId, achievementId)) co_return false;
    auto db = drogon::app().getDbClient();
    if (!db) co_return false;
    try {
        auto existing = co_await db->execSqlCoro(
            "SELECT id FROM user_achievements WHERE user_id = $1::uuid AND achievement_id = $2::uuid",
            userId, achievementId);
        if (!existing.empty()) co_return true;
        auto defs = co_await db->execSqlCoro(
            "SELECT name, description, COALESCE(bonus_points, 0) AS bonus_points FROM achievements_definitions WHERE id = $1::uuid",
            achievementId);
        if (defs.empty()) co_return false;
        co_await db->execSqlCoro(
            "INSERT INTO user_achievements (id, user_id, achievement_id, achievement_name, achievement_description, unlocked_at) "
            "VALUES ($1::uuid, $2::uuid, $3::uuid, $4, $5, NOW())",
            generateSessionId(), userId, achievementId, cellText(defs[0], "name"), cellText(defs[0], "description"));
        co_await addPoints(userId, cellInt(defs[0], "bonus_points", constants::GamePoints::ACHIEVEMENT_BONUS), "achievement");
        co_return true;
    } catch (const std::exception &e) {
        LOG_ERROR << "[games] awardAchievement failed: " << e.what();
        co_return false;
    }
}

drogon::Task<std::vector<Achievement>> GameManager::checkAndAwardAchievements(const std::string &userId, const UserStats &) {
    std::vector<Achievement> awarded;
    auto catalog = co_await getAvailableAchievements();
    auto mine = co_await getUserAchievements(userId);
    std::unordered_set<std::string> have;
    for (const auto &item : mine) have.insert(item.id);
    for (const auto &item : catalog) {
        if (have.count(item.id)) continue;
        if (co_await awardAchievement(userId, item.id)) {
            auto copy = item;
            copy.isUnlocked = true;
            awarded.push_back(copy);
        }
    }
    co_return awarded;
}

drogon::Task<std::vector<Achievement>> GameManager::checkAchievements(const std::string &userId) {
    auto stats = co_await getUserStats(userId);
    co_return co_await checkAndAwardAchievements(userId, stats);
}

drogon::Task<bool> GameManager::updateUserStats(const std::string &userId, const GameSession &session, const GameReward &reward) {
    (void)userId;
    (void)session;
    (void)reward;
    co_return true;
}

drogon::Task<bool> GameManager::updateLeaderboard(const std::string &userId, const GameReward &reward) {
    co_return co_await addPoints(userId, reward.pointsEarned, "leaderboard");
}

drogon::Task<std::vector<LeaderboardEntry>> GameManager::getGlobalLeaderboard(int pageNo, int pageSize) {
    std::vector<LeaderboardEntry> entries;
    auto db = drogon::app().getDbClient();
    if (!db) co_return entries;
    pageNo = clampPage(pageNo);
    pageSize = clampSize(pageSize);
    auto rows = co_await db->execSqlCoro(
        "SELECT l.user_id::text AS user_id, COALESCE(u.username, '') AS username, COALESCE(u.first_name, '') AS first_name, "
        "COALESCE(u.last_name, '') AS last_name, COALESCE(l.total_points, 0) AS total_points, COALESCE(l.games_played, 0) AS games_played, "
        "COALESCE(l.win_rate, 0) AS win_rate, l.last_active, "
        "RANK() OVER (ORDER BY l.total_points DESC, l.games_played ASC) AS rank "
        "FROM leaderboard l LEFT JOIN users u ON u.id = l.user_id "
        "ORDER BY l.total_points DESC, l.games_played ASC LIMIT $1 OFFSET $2",
        pageSize, (pageNo - 1) * pageSize);
    for (size_t i = 0; i < rows.size(); ++i) {
        LeaderboardEntry entry;
        entry.userId = cellText(rows[i], "user_id");
        entry.username = displayName(cellText(rows[i], "username"), cellText(rows[i], "first_name"), cellText(rows[i], "last_name"));
        entry.totalPoints = cellInt(rows[i], "total_points");
        entry.gamesPlayed = cellInt(rows[i], "games_played");
        entry.winRate = cellDouble(rows[i], "win_rate");
        entry.rank = cellInt(rows[i], "rank");
        try {
            if (!rows[i]["last_active"].isNull()) entry.lastActive = fromTrantor(trantor::Date::fromDbStringLocal(rows[i]["last_active"].as<std::string>()));
        } catch (...) {}
        entries.push_back(std::move(entry));
    }
    co_return entries;
}

drogon::Task<std::vector<LeaderboardEntry>> GameManager::getGameTypeLeaderboard(const std::string &gameType, int pageNo, int pageSize) {
    std::vector<LeaderboardEntry> entries;
    const int code = gameTypeCode(canonicalGameType(gameType));
    if (code == 0) co_return entries;
    auto db = drogon::app().getDbClient();
    if (!db) co_return entries;
    pageNo = clampPage(pageNo);
    pageSize = clampSize(pageSize);
    auto rows = co_await db->execSqlCoro(
        "SELECT g.user_id::text AS user_id, COALESCE(u.username, '') AS username, COALESCE(u.first_name, '') AS first_name, "
        "COALESCE(u.last_name, '') AS last_name, COALESCE(g.points, 0) AS total_points, COALESCE(g.games_played, 0) AS games_played, "
        "g.updated_at, RANK() OVER (ORDER BY g.points DESC, g.games_played ASC) AS rank "
        "FROM game_type_leaderboard g LEFT JOIN users u ON u.id = g.user_id "
        "WHERE g.game_type = $1 ORDER BY g.points DESC LIMIT $2 OFFSET $3",
        code, pageSize, (pageNo - 1) * pageSize);
    for (size_t i = 0; i < rows.size(); ++i) {
        LeaderboardEntry entry;
        entry.userId = cellText(rows[i], "user_id");
        entry.username = displayName(cellText(rows[i], "username"), cellText(rows[i], "first_name"), cellText(rows[i], "last_name"));
        entry.totalPoints = cellInt(rows[i], "total_points");
        entry.gamesPlayed = cellInt(rows[i], "games_played");
        entry.rank = cellInt(rows[i], "rank");
        entries.push_back(std::move(entry));
    }
    co_return entries;
}

drogon::Task<std::vector<LeaderboardEntry>> GameManager::getFriendsLeaderboard(const std::string &userId, int limit) {
    std::vector<LeaderboardEntry> entries;
    if (!isUuid(userId)) co_return entries;
    auto db = drogon::app().getDbClient();
    if (!db) co_return entries;
    if (limit < 1) limit = 10;
    if (limit > 50) limit = 50;
    auto rivals = co_await db->execSqlCoro(
        "SELECT DISTINCT other_id FROM ("
        "SELECT opponent_id::text AS other_id FROM game_sessions WHERE user_id = $1::uuid AND opponent_id IS NOT NULL "
        "UNION SELECT user_id::text AS other_id FROM game_sessions WHERE opponent_id = $1::uuid"
        ") rivals WHERE other_id IS NOT NULL",
        userId);
    std::string idList = "'" + userId + "'";
    for (size_t i = 0; i < rivals.size() && i < 40; ++i) {
        const std::string id = cellText(rivals[i], "other_id");
        if (isUuid(id)) idList += ",'" + id + "'";
    }
    auto rows = co_await db->execSqlCoro(
        "SELECT l.user_id::text AS user_id, COALESCE(u.username, '') AS username, COALESCE(u.first_name, '') AS first_name, "
        "COALESCE(u.last_name, '') AS last_name, COALESCE(l.total_points, 0) AS total_points, COALESCE(l.games_played, 0) AS games_played, "
        "COALESCE(l.win_rate, 0) AS win_rate, RANK() OVER (ORDER BY l.total_points DESC) AS rank "
        "FROM leaderboard l LEFT JOIN users u ON u.id = l.user_id "
        "WHERE l.user_id::text IN (" + idList + ") ORDER BY l.total_points DESC LIMIT " + std::to_string(limit));
    if (rows.empty()) {
        co_return co_await getGlobalLeaderboard(1, std::min(limit, 10));
    }
    for (size_t i = 0; i < rows.size(); ++i) {
        LeaderboardEntry entry;
        entry.userId = cellText(rows[i], "user_id");
        entry.username = displayName(cellText(rows[i], "username"), cellText(rows[i], "first_name"), cellText(rows[i], "last_name"));
        entry.totalPoints = cellInt(rows[i], "total_points");
        entry.gamesPlayed = cellInt(rows[i], "games_played");
        entry.winRate = cellDouble(rows[i], "win_rate");
        entry.rank = cellInt(rows[i], "rank");
        entries.push_back(std::move(entry));
    }
    co_return entries;
}

Json::Value leaderboardJson(const std::vector<LeaderboardEntry> &entries) {
    Json::Value data(Json::arrayValue);
    for (const auto &entry : entries) {
        Json::Value item;
        item["userId"] = entry.userId;
        item["username"] = entry.username;
        item["totalPoints"] = entry.totalPoints;
        item["gamesPlayed"] = entry.gamesPlayed;
        item["winRate"] = entry.winRate;
        item["rank"] = entry.rank;
        data.append(item);
    }
    return data;
}

drogon::Task<dto::BaseApiResponse> GameManager::leaderboardView(const std::string &gameType,
                                                                const std::string &period,
                                                                int pageNo,
                                                                int pageSize,
                                                                const std::string &focusUserId) {
    try {
        pageNo = clampPage(pageNo);
        pageSize = clampSize(pageSize);
        const std::string canonical = canonicalGameType(gameType);
        const std::string window = lowerCopy(period);
        Json::Value result;
        result["pageNo"] = pageNo;
        result["pageSize"] = pageSize;
        result["period"] = window.empty() ? "all" : window;
        result["gameType"] = canonical;
        if (window == "week" || window == "month") {
            auto db = drogon::app().getDbClient();
            const std::string interval = window == "month" ? "30 days" : "7 days";
            std::string sql =
                "SELECT s.user_id::text AS user_id, COALESCE(u.username, '') AS username, COALESCE(u.first_name, '') AS first_name, "
                "COALESCE(u.last_name, '') AS last_name, COALESCE(SUM(s.total_points_earned), 0)::int AS total_points, "
                "COUNT(*)::int AS games_played, "
                "RANK() OVER (ORDER BY COALESCE(SUM(s.total_points_earned), 0) DESC) AS rank "
                "FROM game_sessions s LEFT JOIN users u ON u.id = s.user_id "
                "WHERE COALESCE(s.is_complete, FALSE) = TRUE AND s.ended_at >= NOW() - INTERVAL '" + interval + "' ";
            if (!canonical.empty() && canonical != "daily") sql += "AND s.game_type = " + std::to_string(gameTypeCode(canonical)) + " ";
            sql += "GROUP BY s.user_id, u.username, u.first_name, u.last_name "
                   "ORDER BY total_points DESC LIMIT " + std::to_string(pageSize) +
                   " OFFSET " + std::to_string((pageNo - 1) * pageSize);
            auto rows = co_await db->execSqlCoro(sql);
            Json::Value data(Json::arrayValue);
            for (size_t i = 0; i < rows.size(); ++i) {
                Json::Value item;
                item["userId"] = cellText(rows[i], "user_id");
                item["username"] = displayName(cellText(rows[i], "username"), cellText(rows[i], "first_name"), cellText(rows[i], "last_name"));
                item["totalPoints"] = cellInt(rows[i], "total_points");
                item["gamesPlayed"] = cellInt(rows[i], "games_played");
                item["rank"] = cellInt(rows[i], "rank");
                data.append(item);
            }
            result["data"] = data;
        } else if (!canonical.empty() && canonical != "daily") {
            auto entries = co_await getGameTypeLeaderboard(canonical, pageNo, pageSize);
            result["data"] = leaderboardJson(entries);
        } else {
            auto entries = co_await getGlobalLeaderboard(pageNo, pageSize);
            result["data"] = leaderboardJson(entries);
        }
        if (isUuid(focusUserId)) {
            Json::Value me;
            me["userId"] = focusUserId;
            me["totalPoints"] = co_await getUserPoints(focusUserId);
            me["rank"] = co_await rankForPoints(focusUserId);
            result["me"] = me;
        }
        co_return ok("Leaderboard loaded.", result);
    } catch (const std::exception &e) {
        co_return fail(constants::ERR_DB_QUERY, "Could not load the leaderboard.", e.what());
    }
}

drogon::Task<dto::BaseApiResponse> GameManager::rivalsView(const std::string &userId, int limit) {
    try {
        auto entries = co_await getFriendsLeaderboard(userId, limit);
        Json::Value result;
        result["data"] = leaderboardJson(entries);
        result["note"] = "Rivals are readers you have challenged, plus your own standing. A global board is shown until you have rivals.";
        co_return ok("Rival board loaded.", result);
    } catch (const std::exception &e) {
        co_return fail(constants::ERR_DB_QUERY, "Could not load rivals.", e.what());
    }
}

drogon::Task<bool> GameManager::challengeUser(const std::string &challengerId, const std::string &opponentId, const std::string &gameType) {
    auto response = co_await createChallenge(challengerId, opponentId, gameType, "medium");
    co_return response.success;
}

drogon::Task<dto::BaseApiResponse> GameManager::createChallenge(const std::string &userId,
                                                               const std::string &opponentId,
                                                               const std::string &gameType,
                                                               const std::string &difficulty) {
    StartOptions options;
    options.userId = userId;
    options.opponentId = opponentId;
    options.gameType = gameType;
    options.difficulty = difficulty.empty() ? "medium" : difficulty;
    options.isMultiplayer = true;
    auto response = co_await startGame(options);
    if (response.success) response.message = "Challenge sent. You both play the same publication puzzle.";
    co_return response;
}

drogon::Task<bool> GameManager::acceptChallenge(const std::string &challengeId, const std::string &userId) {
    auto response = co_await acceptChallengeView(userId, challengeId);
    co_return response.success;
}

drogon::Task<dto::BaseApiResponse> GameManager::acceptChallengeView(const std::string &userId, const std::string &challengeId) {
    if (!isUuid(userId) || !isUuid(challengeId)) co_return fail(constants::ERR_VALIDATION, "A valid challenge id is required.");
    try {
        auto db = drogon::app().getDbClient();
        auto rows = co_await db->execSqlCoro(
            "SELECT id::text AS id, user_id::text AS user_id, opponent_id::text AS opponent_id, game_type, puzzle_id, difficulty, "
            "progress_data::text AS progress_data FROM game_sessions WHERE id = $1::uuid AND COALESCE(is_multiplayer, FALSE) = TRUE",
            challengeId);
        if (rows.empty()) co_return fail(constants::ERR_RESOURCE_NOT_FOUND, "Challenge was not found.");
        if (cellText(rows[0], "opponent_id") != userId) co_return fail(constants::ERR_PERMISSION_DENIED, "This challenge is for someone else.");
        Json::Value stored = parseJson(cellText(rows[0], "progress_data"));
        if (stored.get("status", "").asString() == "declined") co_return fail(constants::ERR_UNSUPPORTED_OPERATION, "This challenge was declined.");
        auto existing = co_await db->execSqlCoro(
            "SELECT id::text AS id FROM game_sessions WHERE user_id = $1::uuid AND puzzle_id = $2 AND id <> $3::uuid",
            userId, cellText(rows[0], "puzzle_id"), challengeId);
        if (!existing.empty()) {
            auto session = co_await loadOwnedSession(db, cellText(existing[0], "id"), userId);
            co_return ok("Challenge already accepted.", sessionClientJson(session, session.isComplete));
        }
        stored["status"] = "active";
        stored["player"] = Json::Value(Json::objectValue);
        const std::string sessionId = generateSessionId();
        co_await db->execSqlCoro(
            "INSERT INTO game_sessions (id, user_id, game_type, puzzle_id, difficulty, start_time, last_activity, is_multiplayer, "
            "opponent_id, is_complete, total_points_earned, completion_time_seconds, mistakes, hints_used, progress_data) "
            "VALUES ($1::uuid, $2::uuid, $3, $4, $5, NOW(), NOW(), TRUE, $6::uuid, FALSE, 0, 0, 0, 0, $7::jsonb)",
            sessionId, userId, cellInt(rows[0], "game_type"), cellText(rows[0], "puzzle_id"), cellInt(rows[0], "difficulty"),
            cellText(rows[0], "user_id"), jsonToString(stored));
        co_await db->execSqlCoro(
            "UPDATE game_sessions SET progress_data = jsonb_set(progress_data, '{status}', '\"active\"'::jsonb), last_activity = NOW() WHERE id = $1::uuid",
            challengeId);
        auto session = co_await loadOwnedSession(db, sessionId, userId);
        co_return ok("Challenge accepted. The puzzle is the same one your rival is solving.", sessionClientJson(session, false));
    } catch (const std::exception &e) {
        co_return fail(constants::ERR_DB_QUERY, "Could not accept the challenge.", e.what());
    }
}

drogon::Task<dto::BaseApiResponse> GameManager::declineChallengeView(const std::string &userId, const std::string &challengeId) {
    if (!isUuid(userId) || !isUuid(challengeId)) co_return fail(constants::ERR_VALIDATION, "A valid challenge id is required.");
    try {
        auto db = drogon::app().getDbClient();
        auto rows = co_await db->execSqlCoro(
            "UPDATE game_sessions SET is_complete = TRUE, ended_at = NOW(), total_points_earned = 0, "
            "progress_data = jsonb_set(progress_data, '{status}', '\"declined\"'::jsonb) "
            "WHERE id = $1::uuid AND opponent_id = $2::uuid AND COALESCE(is_complete, FALSE) = FALSE RETURNING id",
            challengeId, userId);
        if (rows.empty()) co_return fail(constants::ERR_RESOURCE_NOT_FOUND, "No pending challenge matched that id.");
        co_return ok("Challenge declined.");
    } catch (const std::exception &e) {
        co_return fail(constants::ERR_DB_QUERY, "Could not decline the challenge.", e.what());
    }
}

drogon::Task<dto::BaseApiResponse> GameManager::listChallengesView(const std::string &userId) {
    if (!isUuid(userId)) co_return fail(constants::ERR_VALIDATION, "A valid user id is required.");
    try {
        auto db = drogon::app().getDbClient();
        auto rows = co_await db->execSqlCoro(
            "SELECT s.id::text AS id, s.user_id::text AS user_id, s.opponent_id::text AS opponent_id, s.game_type, s.difficulty, "
            "s.is_complete, s.progress_data::text AS progress_data, s.start_time, "
            "COALESCE(cu.username, '') AS challenger_username, COALESCE(ou.username, '') AS opponent_username "
            "FROM game_sessions s "
            "LEFT JOIN users cu ON cu.id = s.user_id "
            "LEFT JOIN users ou ON ou.id = s.opponent_id "
            "WHERE COALESCE(s.is_multiplayer, FALSE) = TRUE AND (s.user_id = $1::uuid OR s.opponent_id = $1::uuid) "
            "ORDER BY s.start_time DESC LIMIT 30",
            userId);
        Json::Value data(Json::arrayValue);
        for (size_t i = 0; i < rows.size(); ++i) {
            Json::Value stored = parseJson(cellText(rows[i], "progress_data"));
            Json::Value item;
            item["challengeId"] = cellText(rows[i], "id");
            item["gameType"] = gameTypeName(cellInt(rows[i], "game_type"));
            item["difficulty"] = difficultyName(cellInt(rows[i], "difficulty"));
            item["status"] = stored.get("status", cellBool(rows[i], "is_complete") ? "complete" : "active").asString();
            item["summary"] = stored.get("summary", "");
            item["youAreChallenger"] = cellText(rows[i], "user_id") == userId;
            item["challengerId"] = cellText(rows[i], "user_id");
            item["opponentId"] = cellText(rows[i], "opponent_id");
            item["challengerName"] = cellText(rows[i], "challenger_username");
            item["opponentName"] = cellText(rows[i], "opponent_username");
            item["publication"] = PuzzleGenerator::publicMeta(stored).get("publication", Json::Value(Json::objectValue));
            item["startedAt"] = isoFromTrantor(rows[i], "start_time");
            data.append(item);
        }
        Json::Value result;
        result["data"] = data;
        co_return ok("Challenges loaded.", result);
    } catch (const std::exception &e) {
        co_return fail(constants::ERR_DB_QUERY, "Could not load challenges.", e.what());
    }
}

drogon::Task<GameReward> GameManager::submitMultiplayerResult(const std::string &sessionId,
                                                             const std::string &winnerId,
                                                             const std::map<std::string, int> &playerScores) {
    (void)playerScores;
    (void)winnerId;
    GameReward reward;
    auto db = drogon::app().getDbClient();
    if (!db || !isUuid(sessionId)) {
        reward.message = "Session not found.";
        co_return reward;
    }
    auto rows = co_await db->execSqlCoro("SELECT user_id::text AS user_id FROM game_sessions WHERE id = $1::uuid", sessionId);
    if (rows.empty()) {
        reward.message = "Session not found.";
        co_return reward;
    }
    auto response = co_await finishMultiplayer(cellText(rows[0], "user_id"), sessionId);
    reward.message = response.message;
    if (response.success && response.result.isMember("winnerPoints")) reward.pointsEarned = response.result["winnerPoints"].asInt();
    co_return reward;
}

drogon::Task<dto::BaseApiResponse> GameManager::finishMultiplayer(const std::string &userId, const std::string &sessionId) {
    if (!isUuid(userId) || !isUuid(sessionId)) co_return fail(constants::ERR_VALIDATION, "A valid session id is required.");
    try {
        auto db = drogon::app().getDbClient();
        auto mineRows = co_await db->execSqlCoro(
            "SELECT id::text AS id, user_id::text AS user_id, opponent_id::text AS opponent_id, puzzle_id, game_type, "
            "COALESCE(is_complete, FALSE) AS is_complete, COALESCE(total_points_earned, 0) AS total_points_earned, "
            "progress_data::text AS progress_data FROM game_sessions WHERE id = $1::uuid AND user_id = $2::uuid",
            sessionId, userId);
        if (mineRows.empty()) co_return fail(constants::ERR_RESOURCE_NOT_FOUND, "Session was not found.");
        if (!cellBool(mineRows[0], "is_complete")) co_return fail(constants::ERR_UNSUPPORTED_OPERATION, "Submit your puzzle before the rivalry is scored.");
        Json::Value mineProgress = parseJson(cellText(mineRows[0], "progress_data"));
        if (mineProgress.get("rivalryResolved", false).asBool()) {
            co_return ok("Rivalry already scored.", mineProgress.get("rivalry", Json::Value(Json::objectValue)));
        }
        if (!isUuid(cellText(mineRows[0], "opponent_id"))) {
            co_return ok("Waiting for your rival to accept the challenge.");
        }
        auto otherRows = co_await db->execSqlCoro(
            "SELECT id::text AS id, user_id::text AS user_id, COALESCE(is_complete, FALSE) AS is_complete, "
            "COALESCE(total_points_earned, 0) AS total_points_earned, progress_data::text AS progress_data "
            "FROM game_sessions WHERE puzzle_id = $1 AND user_id = $2::uuid",
            cellText(mineRows[0], "puzzle_id"), cellText(mineRows[0], "opponent_id"));
        if (otherRows.empty() || !cellBool(otherRows[0], "is_complete")) {
            co_return ok("Waiting for your rival to finish the same puzzle.");
        }
        const int myPoints = cellInt(mineRows[0], "total_points_earned");
        const int theirPoints = cellInt(otherRows[0], "total_points_earned");
        std::string winnerId;
        std::string resultName = "draw";
        if (myPoints > theirPoints) {
            winnerId = userId;
            resultName = "win";
        } else if (theirPoints > myPoints) {
            winnerId = cellText(otherRows[0], "user_id");
            resultName = "loss";
        }
        if (!winnerId.empty()) co_await addPoints(winnerId, kRivalryBonus, "rivalry");
        const int code = cellInt(mineRows[0], "game_type");
        const std::string rivalId = cellText(otherRows[0], "user_id");
        const std::string rivalResult = resultName == "win" ? "loss" : resultName == "loss" ? "win" : "draw";
        auto flag = [](const std::string &outcome, const char *name) { return outcome == name ? 1 : 0; };
        co_await db->execSqlCoro(
            "UPDATE user_game_stats SET games_won = COALESCE(games_won, 0) + $1, games_lost = COALESCE(games_lost, 0) + $2, "
            "games_draw = COALESCE(games_draw, 0) + $3, updated_at = NOW() WHERE user_id = $4::uuid AND game_type = $5",
            flag(resultName, "win"), flag(resultName, "loss"), flag(resultName, "draw"), userId, code);
        co_await db->execSqlCoro(
            "UPDATE user_game_stats SET games_won = COALESCE(games_won, 0) + $1, games_lost = COALESCE(games_lost, 0) + $2, "
            "games_draw = COALESCE(games_draw, 0) + $3, updated_at = NOW() WHERE user_id = $4::uuid AND game_type = $5",
            flag(rivalResult, "win"), flag(rivalResult, "loss"), flag(rivalResult, "draw"), rivalId, code);

        Json::Value rivalry;
        rivalry["result"] = resultName;
        rivalry["yourPoints"] = myPoints;
        rivalry["rivalPoints"] = theirPoints;
        rivalry["winnerId"] = winnerId;
        rivalry["rivalryBonus"] = winnerId.empty() ? 0 : kRivalryBonus;
        mineProgress["rivalryResolved"] = true;
        mineProgress["rivalry"] = rivalry;
        Json::Value theirProgress = parseJson(cellText(otherRows[0], "progress_data"));
        theirProgress["rivalryResolved"] = true;
        theirProgress["rivalry"] = rivalry;
        theirProgress["rivalry"]["result"] = resultName == "win" ? "loss" : resultName == "loss" ? "win" : "draw";
        co_await db->execSqlCoro("UPDATE game_sessions SET progress_data = $1::jsonb WHERE id = $2::uuid", jsonToString(mineProgress), sessionId);
        co_await db->execSqlCoro("UPDATE game_sessions SET progress_data = $1::jsonb WHERE id = $2::uuid", jsonToString(theirProgress), cellText(otherRows[0], "id"));
        rivalry["winnerPoints"] = winnerId == userId ? myPoints + kRivalryBonus : theirPoints;
        co_return ok(resultName == "draw" ? "It's a draw." : resultName == "win" ? "You won the rivalry." : "Your rival scored higher.", rivalry);
    } catch (const std::exception &e) {
        co_return fail(constants::ERR_DB_QUERY, "Could not score the rivalry.", e.what());
    }
}

drogon::Task<std::string> GameManager::ensureDailyChallenges() {
    auto db = drogon::app().getDbClient();
    if (!db) throw std::runtime_error("DAILY: Database client is not configured.");
    auto clock = co_await db->execSqlCoro("SELECT CURRENT_DATE::text AS today, EXTRACT(DOW FROM CURRENT_DATE)::int AS dow");
    if (clock.empty()) throw std::runtime_error("DAILY: Could not read the database date.");
    const std::string today = cellText(clock[0], "today");
    const int dow = cellInt(clock[0], "dow");
    const char *rotation[] = {"riddle", "crossword", "sudoku", "word_search", "riddle", "crossword", "sudoku"};
    const std::string featured = rotation[std::clamp(dow, 0, 6)];
    const char *types[] = {"sudoku", "word_search", "crossword", "riddle"};
    Corpus corpus = co_await PublicationLexicon::load("", "", {});
    for (const char *type : types) {
        auto existing = co_await db->execSqlCoro(
            "SELECT id::text AS id FROM daily_challenges WHERE game_type = $1 AND created_at = CURRENT_DATE ORDER BY id LIMIT 1",
            gameTypeCode(type));
        if (!existing.empty()) continue;
        BuiltPuzzle built = PuzzleGenerator::build(type, "medium", corpus, hashSeed(today + "|" + type));
        built.stored["daily"] = true;
        built.stored["featured"] = featured == type;
        built.stored["dailyBonus"] = featured == type ? constants::GamePoints::DAILY_CHALLENGE_BONUS : 50;
        const std::string id = generateSessionId();
        built.stored["challengeId"] = id;
        co_await db->execSqlCoro(
            "INSERT INTO daily_challenges (id, game_type, puzzle_data, difficulty, created_at, expires_at) "
            "SELECT $1::uuid, $2, $3::jsonb, $4, CURRENT_DATE, CURRENT_DATE "
            "WHERE NOT EXISTS (SELECT 1 FROM daily_challenges WHERE game_type = $2 AND created_at = CURRENT_DATE)",
            id, gameTypeCode(type), jsonToString(built.stored), difficultyCode("medium"));
    }
    co_return today;
}

drogon::Task<std::optional<GameSession>> GameManager::getDailyChallenge(const std::string &userId) {
    auto response = co_await startDaily(userId, "", "");
    if (!response.success || !response.result.isMember("sessionId")) co_return std::nullopt;
    auto db = drogon::app().getDbClient();
    auto session = co_await loadOwnedSession(db, response.result["sessionId"].asString(), userId);
    session.progressData = PuzzleGenerator::clientView(session.progressData);
    co_return session;
}

drogon::Task<dto::BaseApiResponse> GameManager::dailyBoard(const std::string &userId) {
    try {
        co_await ensureAchievementCatalog();
        const std::string today = co_await ensureDailyChallenges();
        auto db = drogon::app().getDbClient();
        auto rows = co_await db->execSqlCoro(
            "SELECT id::text AS id, game_type, difficulty, puzzle_data::text AS puzzle_data, created_at::text AS created_at, "
            "expires_at::text AS expires_at FROM daily_challenges WHERE created_at = CURRENT_DATE ORDER BY game_type");
        Json::Value challenges(Json::arrayValue);
        int completed = 0;
        for (size_t i = 0; i < rows.size(); ++i) {
            Json::Value stored = parseJson(cellText(rows[i], "puzzle_data"));
            Json::Value item = PuzzleGenerator::publicMeta(stored);
            item["id"] = cellText(rows[i], "id");
            item["gameType"] = gameTypeName(cellInt(rows[i], "game_type"));
            item["difficulty"] = difficultyName(cellInt(rows[i], "difficulty"));
            item["featured"] = stored.get("featured", false);
            item["bonus"] = stored.get("dailyBonus", constants::GamePoints::DAILY_CHALLENGE_BONUS).asInt();
            item["createdAt"] = cellText(rows[i], "created_at");
            item["expiresAt"] = cellText(rows[i], "expires_at");
            item["completed"] = false;
            if (isUuid(userId)) {
                auto done = co_await db->execSqlCoro(
                    "SELECT bonus_earned FROM daily_challenge_completions WHERE user_id = $1::uuid AND challenge_id = $2::uuid",
                    userId, cellText(rows[i], "id"));
                item["completed"] = !done.empty();
                if (!done.empty()) {
                    item["bonusEarned"] = cellInt(done[0], "bonus_earned");
                    ++completed;
                }
            }
            challenges.append(item);
        }
        Json::Value result;
        result["date"] = today;
        result["challenges"] = challenges;
        result["completedCount"] = completed;
        result["scoring"] = scoringRules();
        co_return ok("Today's challenges are ready.", result);
    } catch (const std::exception &e) {
        const std::string message = e.what();
        const auto marker = message.find(": ");
        co_return fail(constants::ERR_VALIDATION, marker == std::string::npos ? message : message.substr(marker + 2), message);
    }
}

drogon::Task<dto::BaseApiResponse> GameManager::startDaily(const std::string &userId,
                                                          const std::string &challengeId,
                                                          const std::string &gameType) {
    if (!isUuid(userId)) co_return fail(constants::ERR_VALIDATION, "Sign in to play the daily challenge.");
    try {
        co_await ensureDailyChallenges();
        auto db = drogon::app().getDbClient();
        std::string sql =
            "SELECT id::text AS id, game_type, difficulty, puzzle_data::text AS puzzle_data FROM daily_challenges "
            "WHERE created_at = CURRENT_DATE ";
        drogon::orm::Result rows(nullptr);
        if (isUuid(challengeId)) {
            sql += "AND id = $1::uuid LIMIT 1";
            rows = co_await db->execSqlCoro(sql, challengeId);
        } else {
            const std::string canonical = canonicalGameType(gameType);
            if (isPlayableGameType(canonical)) {
                sql += "AND game_type = $1 ORDER BY id LIMIT 1";
                rows = co_await db->execSqlCoro(sql, gameTypeCode(canonical));
            } else {
                sql += "AND puzzle_data::text LIKE '%\"featured\":true%' ORDER BY id LIMIT 1";
                rows = co_await db->execSqlCoro(sql);
                if (rows.empty()) {
                    rows = co_await db->execSqlCoro(
                        "SELECT id::text AS id, game_type, difficulty, puzzle_data::text AS puzzle_data "
                        "FROM daily_challenges WHERE created_at = CURRENT_DATE ORDER BY game_type LIMIT 1");
                }
            }
        }
        if (rows.empty()) co_return fail(constants::ERR_RESOURCE_NOT_FOUND, "No daily challenge is available.");
        const std::string id = cellText(rows[0], "id");
        auto existing = co_await db->execSqlCoro(
            "SELECT id::text AS id FROM game_sessions WHERE user_id = $1::uuid AND puzzle_id = $2 ORDER BY start_time DESC LIMIT 1",
            userId, id);
        if (!existing.empty()) {
            auto session = co_await loadOwnedSession(db, cellText(existing[0], "id"), userId);
            co_return ok(session.isComplete ? "You already played this daily challenge." : "Resuming today's challenge.",
                         sessionClientJson(session, session.isComplete));
        }
        Json::Value stored = parseJson(cellText(rows[0], "puzzle_data"));
        stored["daily"] = true;
        stored["challengeId"] = id;
        stored["status"] = "active";
        stored["dailyBonus"] = stored.get("dailyBonus", constants::GamePoints::DAILY_CHALLENGE_BONUS).asInt();
        if (stored.isMember("player")) stored["player"] = Json::Value(Json::objectValue);
        const std::string sessionId = generateSessionId();
        co_await db->execSqlCoro(
            "INSERT INTO game_sessions (id, user_id, game_type, puzzle_id, difficulty, start_time, last_activity, is_multiplayer, "
            "is_complete, total_points_earned, completion_time_seconds, mistakes, hints_used, progress_data) "
            "VALUES ($1::uuid, $2::uuid, $3, $4, $5, NOW(), NOW(), FALSE, FALSE, 0, 0, 0, 0, $6::jsonb)",
            sessionId, userId, cellInt(rows[0], "game_type"), id, cellInt(rows[0], "difficulty"), jsonToString(stored));
        auto session = co_await loadOwnedSession(db, sessionId, userId);
        co_return ok("Daily challenge started. Everyone is solving this same publication puzzle.", sessionClientJson(session, false));
    } catch (const std::exception &e) {
        co_return fail(constants::ERR_VALIDATION, "Could not start the daily challenge.", e.what());
    }
}

drogon::Task<GameReward> GameManager::completeDailyChallenge(const std::string &userId, const std::string &sessionId) {
    GameReward reward;
    auto response = co_await completeDailyView(userId, sessionId);
    reward.message = response.message;
    if (response.success) reward.pointsEarned = response.result.get("bonusEarned", 0).asInt();
    reward.bonusPoints = reward.pointsEarned;
    co_return reward;
}

drogon::Task<dto::BaseApiResponse> GameManager::completeDailyView(const std::string &userId, const std::string &sessionId) {
    if (!isUuid(userId) || !isUuid(sessionId)) co_return fail(constants::ERR_VALIDATION, "A valid session id is required.");
    try {
        auto db = drogon::app().getDbClient();
        auto session = co_await loadOwnedSession(db, sessionId, userId);
        const std::string challengeId = session.progressData.get("challengeId", session.puzzleId).asString();
        if (!isUuid(challengeId)) co_return fail(constants::ERR_VALIDATION, "This session is not a daily challenge.");
        auto existing = co_await db->execSqlCoro(
            "SELECT bonus_earned, completed_at::text AS completed_at FROM daily_challenge_completions "
            "WHERE user_id = $1::uuid AND challenge_id = $2::uuid",
            userId, challengeId);
        if (!existing.empty()) {
            Json::Value result;
            result["bonusEarned"] = cellInt(existing[0], "bonus_earned");
            result["completedAt"] = cellText(existing[0], "completed_at");
            result["alreadyClaimed"] = true;
            co_return ok("Daily bonus was already claimed.", result);
        }
        if (!session.isComplete) co_return fail(constants::ERR_UNSUPPORTED_OPERATION, "Submit the puzzle before claiming the daily bonus.");
        const int available = session.progressData.get("dailyBonus", constants::GamePoints::DAILY_CHALLENGE_BONUS).asInt();
        const bool won = session.progressData.get("reward", Json::Value(Json::objectValue)).get("won", false).asBool();
        const double accuracy = session.progressData.get("reward", Json::Value(Json::objectValue)).get("accuracy", 0).asDouble();
        if (!won && !(session.gameType == "riddle" && accuracy * 100 >= kPartialDailyThreshold)) {
            co_return fail(constants::ERR_UNSUPPORTED_OPERATION, "Finish the daily puzzle accurately to claim the bonus.");
        }
        const int bonus = won ? available : static_cast<int>(std::lround(available * accuracy));
        co_await db->execSqlCoro(
            "INSERT INTO daily_challenge_completions (id, user_id, challenge_id, session_id, completed_at, bonus_earned) "
            "VALUES ($1::uuid, $2::uuid, $3::uuid, $4::uuid, NOW(), $5)",
            generateSessionId(), userId, challengeId, sessionId, bonus);
        co_await addPoints(userId, bonus, "daily challenge");
        Json::Value result;
        result["bonusEarned"] = bonus;
        result["alreadyClaimed"] = false;
        result["totalPoints"] = co_await getUserPoints(userId);
        co_return ok("Daily bonus added to your score.", result);
    } catch (const std::exception &e) {
        co_return fail(constants::ERR_DB_QUERY, "Could not claim the daily bonus.", e.what());
    }
}

drogon::Task<dto::BaseApiResponse> GameManager::dailyLeaderboardView(int pageNo, int pageSize) {
    try {
        pageNo = clampPage(pageNo);
        pageSize = clampSize(pageSize);
        auto db = drogon::app().getDbClient();
        auto rows = co_await db->execSqlCoro(
            "SELECT c.user_id::text AS user_id, COALESCE(u.username, '') AS username, COALESCE(u.first_name, '') AS first_name, "
            "COALESCE(u.last_name, '') AS last_name, COALESCE(SUM(c.bonus_earned), 0)::int AS total_points, "
            "COUNT(*)::int AS challenges, MIN(c.completed_at) AS first_finish, "
            "RANK() OVER (ORDER BY COALESCE(SUM(c.bonus_earned), 0) DESC, MIN(c.completed_at) ASC) AS rank "
            "FROM daily_challenge_completions c LEFT JOIN users u ON u.id = c.user_id "
            "WHERE c.completed_at::date = CURRENT_DATE "
            "GROUP BY c.user_id, u.username, u.first_name, u.last_name "
            "ORDER BY total_points DESC, first_finish ASC LIMIT $1 OFFSET $2",
            pageSize, (pageNo - 1) * pageSize);
        Json::Value data(Json::arrayValue);
        for (size_t i = 0; i < rows.size(); ++i) {
            Json::Value item;
            item["userId"] = cellText(rows[i], "user_id");
            item["username"] = displayName(cellText(rows[i], "username"), cellText(rows[i], "first_name"), cellText(rows[i], "last_name"));
            item["totalPoints"] = cellInt(rows[i], "total_points");
            item["challengesCompleted"] = cellInt(rows[i], "challenges");
            item["rank"] = cellInt(rows[i], "rank");
            item["firstFinish"] = isoFromTrantor(rows[i], "first_finish");
            data.append(item);
        }
        Json::Value result;
        result["data"] = data;
        result["pageNo"] = pageNo;
        result["pageSize"] = pageSize;
        co_return ok("Daily leaderboard loaded.", result);
    } catch (const std::exception &e) {
        co_return fail(constants::ERR_DB_QUERY, "Could not load the daily leaderboard.", e.what());
    }
}

drogon::Task<dto::BaseApiResponse> GameManager::publicationSourcesView(const std::string &publicationId) {
    try {
        if (!publicationId.empty() && !isUuid(publicationId)) {
            co_return fail(constants::ERR_VALIDATION, "publicationId is not a valid id.");
        }
        Json::Value result;
        result["sources"] = co_await PublicationLexicon::listSources(publicationId, 12);
        result["note"] = "Puzzles use page text already extracted from editions, and fall back to the PDF stored in G3.";
        co_return ok("Publication sources loaded.", result);
    } catch (const std::exception &e) {
        co_return fail(constants::ERR_DB_QUERY, "Could not list publication sources.", e.what());
    }
}

drogon::Task<std::map<std::string, int>> GameManager::getGameStatistics(const std::string &gameType) {
    std::map<std::string, int> stats;
    const int code = gameTypeCode(canonicalGameType(gameType));
    if (code == 0) co_return stats;
    auto db = drogon::app().getDbClient();
    if (!db) co_return stats;
    auto rows = co_await db->execSqlCoro(
        "SELECT COUNT(*)::int AS sessions, "
        "COUNT(*) FILTER (WHERE COALESCE(is_complete, FALSE) = TRUE)::int AS completed, "
        "COALESCE(AVG(total_points_earned) FILTER (WHERE COALESCE(is_complete, FALSE) = TRUE), 0)::int AS average_points, "
        "COALESCE(AVG(completion_time_seconds) FILTER (WHERE COALESCE(completion_time_seconds, 0) > 0), 0)::int AS average_seconds, "
        "COUNT(*) FILTER (WHERE start_time >= NOW() - INTERVAL '7 days')::int AS week_sessions "
        "FROM game_sessions WHERE game_type = $1",
        code);
    if (!rows.empty()) {
        stats["sessions"] = cellInt(rows[0], "sessions");
        stats["completed"] = cellInt(rows[0], "completed");
        stats["averagePoints"] = cellInt(rows[0], "average_points");
        stats["averageSeconds"] = cellInt(rows[0], "average_seconds");
        stats["weekSessions"] = cellInt(rows[0], "week_sessions");
    }
    co_return stats;
}

drogon::Task<dto::BaseApiResponse> GameManager::statisticsView(const std::string &gameType) {
    try {
        const std::string canonical = canonicalGameType(gameType);
        if (!isPlayableGameType(canonical)) co_return fail(constants::ERR_VALIDATION, "Unknown game type.");
        auto stats = co_await getGameStatistics(canonical);
        Json::Value result;
        result["gameType"] = canonical;
        for (const auto &entry : stats) result[entry.first] = entry.second;
        co_return ok("Game statistics loaded.", result);
    } catch (const std::exception &e) {
        co_return fail(constants::ERR_DB_QUERY, "Could not load game statistics.", e.what());
    }
}

drogon::Task<bool> GameManager::resetUserProgress(const std::string &userId) {
    auto response = co_await resetProgressView(userId);
    co_return response.success;
}

drogon::Task<dto::BaseApiResponse> GameManager::resetProgressView(const std::string &userId) {
    if (!isUuid(userId)) co_return fail(constants::ERR_VALIDATION, "A valid user id is required.");
    auto db = drogon::app().getDbClient();
    if (!db) co_return fail(constants::ERR_DB_CONNECTION, "Database client is not configured.");
    try {
        co_await db->execSqlCoro("DELETE FROM daily_challenge_completions WHERE user_id = $1::uuid", userId);
        co_await db->execSqlCoro("DELETE FROM user_achievements WHERE user_id = $1::uuid", userId);
        co_await db->execSqlCoro("DELETE FROM user_game_history WHERE user_id = $1::uuid", userId);
        co_await db->execSqlCoro("DELETE FROM user_game_stats WHERE user_id = $1::uuid", userId);
        co_await db->execSqlCoro("DELETE FROM game_type_leaderboard WHERE user_id = $1::uuid", userId);
        co_await db->execSqlCoro("DELETE FROM leaderboard WHERE user_id = $1::uuid", userId);
        co_await db->execSqlCoro("DELETE FROM game_sessions WHERE user_id = $1::uuid OR opponent_id = $1::uuid", userId);
        co_return ok("Game progress reset.");
    } catch (const std::exception &e) {
        co_return fail(constants::ERR_DB_QUERY, "Could not reset game progress.", e.what());
    }
}

drogon::Task<dto::BaseApiResponse> GameManager::seedAchievementsView() {
    gCatalogReady.store(false);
    co_await ensureAchievementCatalog();
    auto catalog = co_await getAvailableAchievements();
    Json::Value result;
    result["achievements"] = static_cast<int>(catalog.size());
    co_return ok("Achievement catalog is ready.", result);
}

drogon::Task<dto::BaseApiResponse> GameManager::refreshLexiconView() {
    PublicationLexicon::clearCache();
    co_return ok("Publication lexicon cache cleared. The next puzzle will reread stored editions.");
}

} // namespace gnp::services
