#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

/**
 * Publication-backed games.
 *
 * Crossword, letter Sudoku, word search, and riddles are generated from
 * newspaper page text (and the stored PDFs when page text is missing).
 * Points, achievements, and leaderboards are recorded on submit.
 *
 * Prefix: /api/v1/games/
 */
class GamesController : public drogon::HttpController<GamesController> {
public:
    static constexpr const char *PREFIX = "/api/v1/games/";

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(GamesController::getCategories, std::string(PREFIX) + "categories", Get, Options);
    ADD_METHOD_TO(GamesController::getPublicationSources, std::string(PREFIX) + "publication-sources", Get, Options);
    ADD_METHOD_TO(GamesController::getStatistics, std::string(PREFIX) + "statistics", Get, Options);
    ADD_METHOD_TO(GamesController::getLeaderboard, std::string(PREFIX) + "leaderboard", Get, Options);
    ADD_METHOD_TO(GamesController::getDailyLeaderboard, std::string(PREFIX) + "daily-leaderboard", Get, Options);
    ADD_METHOD_TO(GamesController::getAchievements, std::string(PREFIX) + "achievements", Get, Options);

    ADD_METHOD_TO(GamesController::startGame, std::string(PREFIX) + "start", Post, Options, "JwtAuthFilter");
    ADD_METHOD_TO(GamesController::generateSudoku, std::string(PREFIX) + "generate-sudoku", Post, Options, "JwtAuthFilter");
    ADD_METHOD_TO(GamesController::generateWordSearch, std::string(PREFIX) + "generate-word-search", Post, Options, "JwtAuthFilter");
    ADD_METHOD_TO(GamesController::generateCrossword, std::string(PREFIX) + "generate-crossword", Post, Options, "JwtAuthFilter");
    ADD_METHOD_TO(GamesController::generateRiddle, std::string(PREFIX) + "generate-riddle", Post, Options, "JwtAuthFilter");
    ADD_METHOD_TO(GamesController::getCurrentSession, std::string(PREFIX) + "current-session", Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(GamesController::getSession, std::string(PREFIX) + "session", Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(GamesController::saveProgress, std::string(PREFIX) + "save-progress", Post, Options, "JwtAuthFilter");
    ADD_METHOD_TO(GamesController::submitSession, std::string(PREFIX) + "submit", Post, Options, "JwtAuthFilter");
    ADD_METHOD_TO(GamesController::useHint, std::string(PREFIX) + "hint", Post, Options, "JwtAuthFilter");
    ADD_METHOD_TO(GamesController::deleteSession, std::string(PREFIX) + "session", Delete, Options, "JwtAuthFilter");
    ADD_METHOD_TO(GamesController::getHistory, std::string(PREFIX) + "history", Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(GamesController::getStats, std::string(PREFIX) + "stats", Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(GamesController::getPoints, std::string(PREFIX) + "points", Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(GamesController::getMyAchievements, std::string(PREFIX) + "my-achievements", Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(GamesController::checkAchievements, std::string(PREFIX) + "check-achievements", Post, Options, "JwtAuthFilter");
    ADD_METHOD_TO(GamesController::getRivals, std::string(PREFIX) + "rivals", Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(GamesController::createChallenge, std::string(PREFIX) + "challenge", Post, Options, "JwtAuthFilter");
    ADD_METHOD_TO(GamesController::acceptChallenge, std::string(PREFIX) + "accept-challenge", Post, Options, "JwtAuthFilter");
    ADD_METHOD_TO(GamesController::declineChallenge, std::string(PREFIX) + "decline-challenge", Post, Options, "JwtAuthFilter");
    ADD_METHOD_TO(GamesController::listChallenges, std::string(PREFIX) + "challenges", Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(GamesController::submitMultiplayer, std::string(PREFIX) + "submit-multiplayer", Post, Options, "JwtAuthFilter");
    ADD_METHOD_TO(GamesController::getDaily, std::string(PREFIX) + "daily", Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(GamesController::startDaily, std::string(PREFIX) + "start-daily", Post, Options, "JwtAuthFilter");
    ADD_METHOD_TO(GamesController::completeDaily, std::string(PREFIX) + "complete-daily", Post, Options, "JwtAuthFilter");
    ADD_METHOD_TO(GamesController::resetProgress, std::string(PREFIX) + "reset-progress", Post, Options, "JwtAuthFilter");

    ADD_METHOD_TO(GamesController::seedAchievements, std::string(PREFIX) + "admin/seed-achievements", Post, Options, "AdminJwtAuthFilter");
    ADD_METHOD_TO(GamesController::refreshLexicon, std::string(PREFIX) + "admin/refresh-lexicon", Post, Options, "AdminJwtAuthFilter");
    METHOD_LIST_END

    drogon::Task<HttpResponsePtr> getCategories(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> getPublicationSources(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> getStatistics(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> getLeaderboard(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> getDailyLeaderboard(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> getAchievements(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> startGame(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> generateSudoku(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> generateWordSearch(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> generateCrossword(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> generateRiddle(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> getCurrentSession(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> getSession(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> saveProgress(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> submitSession(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> useHint(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> deleteSession(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> getHistory(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> getStats(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> getPoints(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> getMyAchievements(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> checkAchievements(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> getRivals(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> createChallenge(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> acceptChallenge(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> declineChallenge(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> listChallenges(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> submitMultiplayer(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> getDaily(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> startDaily(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> completeDaily(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> resetProgress(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> seedAchievements(HttpRequestPtr req);
    drogon::Task<HttpResponsePtr> refreshLexicon(HttpRequestPtr req);
};
