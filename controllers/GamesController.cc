#include "GamesController.h"

#include "constants/ErrorCodes.h"
#include "dto/BaseApiResponse.h"
#include "plugins/GnpServicePlugin.h"
#include "services/games/GameTypes.h"

namespace {

HttpResponsePtr preflight(const HttpRequestPtr &req) {
    if (req->getMethod() != Options) return nullptr;
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    return resp;
}

HttpResponsePtr jsonResponse(const gnp::dto::BaseApiResponse &body) {
    auto resp = HttpResponse::newHttpJsonResponse(body.toJson());
    if (!body.success && body.error.isMember("code")) {
        const int code = body.error["code"].asInt();
        if (code == gnp::constants::ERR_RESOURCE_NOT_FOUND || code == gnp::constants::ERR_DB_NOT_FOUND ||
            code == gnp::constants::ERR_USER_NOT_FOUND) {
            resp->setStatusCode(k404NotFound);
        } else if (code == gnp::constants::ERR_VALIDATION || code == gnp::constants::ERR_MISSING_PARAMETER) {
            resp->setStatusCode(k400BadRequest);
        } else if (code == gnp::constants::ERR_PERMISSION_DENIED || code == gnp::constants::ERR_UNAUTHORIZED) {
            resp->setStatusCode(k403Forbidden);
        } else if (code == gnp::constants::ERR_QUOTA_EXCEEDED || code == gnp::constants::ERR_UNSUPPORTED_OPERATION) {
            resp->setStatusCode(k409Conflict);
        } else if (code == gnp::constants::ERR_DB_QUERY || code == gnp::constants::ERR_INTERNAL ||
                   code == gnp::constants::ERR_DB_CONNECTION) {
            resp->setStatusCode(k500InternalServerError);
        }
    }
    return resp;
}

HttpResponsePtr badRequest(const std::string &message) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.message = message;
    response.error["code"] = gnp::constants::ERR_VALIDATION;
    response.error["message"] = message;
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k400BadRequest);
    return resp;
}

std::string userIdOf(const HttpRequestPtr &req) {
    try {
        if (!req->attributes()) return "";
        return req->attributes()->get<std::string>("userId");
    } catch (...) {
        return "";
    }
}

HttpResponsePtr requireUser(const HttpRequestPtr &req, std::string &userId) {
    userId = userIdOf(req);
    if (userId.empty()) return badRequest("Authorization token did not include a user id.");
    return nullptr;
}

int queryInt(const HttpRequestPtr &req, const char *name, int fallback, int minValue, int maxValue) {
    const auto value = req->getParameter(name);
    if (value.empty()) return fallback;
    try {
        int parsed = std::stoi(value);
        if (parsed < minValue) return minValue;
        if (parsed > maxValue) return maxValue;
        return parsed;
    } catch (...) {
        return fallback;
    }
}

Json::Value bodyOf(const HttpRequestPtr &req) {
    auto json = req->getJsonObject();
    if (!json) return Json::Value(Json::objectValue);
    return *json;
}

std::string textField(const Json::Value &json, const HttpRequestPtr &req, const char *name) {
    if (json.isMember(name) && json[name].isString()) return json[name].asString();
    return req->getParameter(name);
}

gnp::services::StartOptions startOptionsFrom(const HttpRequestPtr &req, const std::string &userId, const std::string &forcedType) {
    const Json::Value json = bodyOf(req);
    gnp::services::StartOptions options;
    options.userId = userId;
    options.gameType = forcedType.empty() ? textField(json, req, "gameType") : forcedType;
    options.difficulty = textField(json, req, "difficulty");
    if (options.difficulty.empty()) options.difficulty = "medium";
    options.publicationId = textField(json, req, "publicationId");
    options.newspaperId = textField(json, req, "newspaperId");
    options.opponentId = textField(json, req, "opponentId");
    options.isMultiplayer = json.get("isMultiplayer", false).asBool() || !options.opponentId.empty();
    if (json.isMember("topics") && json["topics"].isArray()) {
        for (const auto &topic : json["topics"]) {
            if (topic.isString() && !topic.asString().empty()) options.topics.push_back(topic.asString());
        }
    }
    return options;
}

gnp::services::GameManager &games() {
    auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
    return plugin->getGameManager();
}

std::string sessionIdOf(const HttpRequestPtr &req, const Json::Value &json) {
    std::string sessionId = textField(json, req, "sessionId");
    if (sessionId.empty()) sessionId = textField(json, req, "challengeId");
    return sessionId;
}

} // namespace

drogon::Task<HttpResponsePtr> GamesController::getCategories(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    co_return jsonResponse(co_await games().getAllGameCategories());
}

drogon::Task<HttpResponsePtr> GamesController::getPublicationSources(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    co_return jsonResponse(co_await games().publicationSourcesView(req->getParameter("publicationId")));
}

drogon::Task<HttpResponsePtr> GamesController::getStatistics(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    const std::string gameType = req->getParameter("gameType");
    if (gameType.empty()) co_return badRequest("gameType is required.");
    co_return jsonResponse(co_await games().statisticsView(gameType));
}

drogon::Task<HttpResponsePtr> GamesController::getLeaderboard(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    const int pageNo = queryInt(req, "pageNo", 1, 1, 100000);
    const int pageSize = queryInt(req, "pageSize", 20, 1, 100);
    co_return jsonResponse(co_await games().leaderboardView(
        req->getParameter("gameType"), req->getParameter("period"), pageNo, pageSize, userIdOf(req)));
}

drogon::Task<HttpResponsePtr> GamesController::getDailyLeaderboard(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    co_return jsonResponse(co_await games().dailyLeaderboardView(
        queryInt(req, "pageNo", 1, 1, 100000), queryInt(req, "pageSize", 20, 1, 100)));
}

drogon::Task<HttpResponsePtr> GamesController::getAchievements(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    co_return jsonResponse(co_await games().achievementsView(userIdOf(req), false));
}

drogon::Task<HttpResponsePtr> GamesController::startGame(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId;
    if (auto denied = requireUser(req, userId)) co_return denied;
    co_return jsonResponse(co_await games().startGame(startOptionsFrom(req, userId, "")));
}

drogon::Task<HttpResponsePtr> GamesController::generateSudoku(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId;
    if (auto denied = requireUser(req, userId)) co_return denied;
    co_return jsonResponse(co_await games().startGame(startOptionsFrom(req, userId, "sudoku")));
}

drogon::Task<HttpResponsePtr> GamesController::generateWordSearch(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId;
    if (auto denied = requireUser(req, userId)) co_return denied;
    co_return jsonResponse(co_await games().startGame(startOptionsFrom(req, userId, "word_search")));
}

drogon::Task<HttpResponsePtr> GamesController::generateCrossword(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId;
    if (auto denied = requireUser(req, userId)) co_return denied;
    co_return jsonResponse(co_await games().startGame(startOptionsFrom(req, userId, "crossword")));
}

drogon::Task<HttpResponsePtr> GamesController::generateRiddle(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId;
    if (auto denied = requireUser(req, userId)) co_return denied;
    co_return jsonResponse(co_await games().startGame(startOptionsFrom(req, userId, "riddle")));
}

drogon::Task<HttpResponsePtr> GamesController::getCurrentSession(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId;
    if (auto denied = requireUser(req, userId)) co_return denied;
    co_return jsonResponse(co_await games().getCurrentSessionView(userId));
}

drogon::Task<HttpResponsePtr> GamesController::getSession(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId;
    if (auto denied = requireUser(req, userId)) co_return denied;
    const std::string sessionId = req->getParameter("sessionId");
    if (sessionId.empty()) co_return badRequest("sessionId is required.");
    co_return jsonResponse(co_await games().getSessionView(userId, sessionId));
}

drogon::Task<HttpResponsePtr> GamesController::saveProgress(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId;
    if (auto denied = requireUser(req, userId)) co_return denied;
    const Json::Value json = bodyOf(req);
    const std::string sessionId = sessionIdOf(req, json);
    if (sessionId.empty()) co_return badRequest("sessionId is required.");
    co_return jsonResponse(co_await games().saveSessionProgress(userId, sessionId, json));
}

drogon::Task<HttpResponsePtr> GamesController::submitSession(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId;
    if (auto denied = requireUser(req, userId)) co_return denied;
    const Json::Value json = bodyOf(req);
    const std::string sessionId = sessionIdOf(req, json);
    if (sessionId.empty()) co_return badRequest("sessionId is required.");
    co_return jsonResponse(co_await games().submitSession(userId, sessionId, json));
}

drogon::Task<HttpResponsePtr> GamesController::useHint(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId;
    if (auto denied = requireUser(req, userId)) co_return denied;
    const Json::Value json = bodyOf(req);
    const std::string sessionId = sessionIdOf(req, json);
    if (sessionId.empty()) co_return badRequest("sessionId is required.");
    co_return jsonResponse(co_await games().takeHint(userId, sessionId));
}

drogon::Task<HttpResponsePtr> GamesController::deleteSession(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId;
    if (auto denied = requireUser(req, userId)) co_return denied;
    const std::string sessionId = req->getParameter("sessionId");
    if (sessionId.empty()) co_return badRequest("sessionId is required.");
    co_return jsonResponse(co_await games().removeSession(userId, sessionId));
}

drogon::Task<HttpResponsePtr> GamesController::getHistory(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId;
    if (auto denied = requireUser(req, userId)) co_return denied;
    co_return jsonResponse(co_await games().historyView(
        userId, queryInt(req, "pageNo", 1, 1, 100000), queryInt(req, "pageSize", 10, 1, 100)));
}

drogon::Task<HttpResponsePtr> GamesController::getStats(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId = req->getParameter("userId");
    if (userId.empty()) userId = userIdOf(req);
    if (userId.empty()) co_return badRequest("userId is required.");
    co_return jsonResponse(co_await games().statsView(userId));
}

drogon::Task<HttpResponsePtr> GamesController::getPoints(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId;
    if (auto denied = requireUser(req, userId)) co_return denied;
    co_return jsonResponse(co_await games().pointsView(userId));
}

drogon::Task<HttpResponsePtr> GamesController::getMyAchievements(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId;
    if (auto denied = requireUser(req, userId)) co_return denied;
    co_return jsonResponse(co_await games().achievementsView(userId, true));
}

drogon::Task<HttpResponsePtr> GamesController::checkAchievements(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId;
    if (auto denied = requireUser(req, userId)) co_return denied;
    auto awarded = co_await games().checkAchievements(userId);
    gnp::dto::BaseApiResponse response;
    response.success = true;
    response.message = awarded.empty() ? "No new achievements." : "New achievements unlocked.";
    response.result["count"] = static_cast<int>(awarded.size());
    Json::Value names(Json::arrayValue);
    for (const auto &item : awarded) names.append(item.name);
    response.result["achievements"] = names;
    co_return jsonResponse(response);
}

drogon::Task<HttpResponsePtr> GamesController::getRivals(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId;
    if (auto denied = requireUser(req, userId)) co_return denied;
    co_return jsonResponse(co_await games().rivalsView(userId, queryInt(req, "limit", 20, 1, 50)));
}

drogon::Task<HttpResponsePtr> GamesController::createChallenge(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId;
    if (auto denied = requireUser(req, userId)) co_return denied;
    const Json::Value json = bodyOf(req);
    const std::string opponentId = textField(json, req, "opponentId");
    const std::string gameType = textField(json, req, "gameType");
    if (opponentId.empty() || gameType.empty()) co_return badRequest("opponentId and gameType are required.");
    co_return jsonResponse(co_await games().createChallenge(userId, opponentId, gameType, textField(json, req, "difficulty")));
}

drogon::Task<HttpResponsePtr> GamesController::acceptChallenge(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId;
    if (auto denied = requireUser(req, userId)) co_return denied;
    const Json::Value json = bodyOf(req);
    const std::string challengeId = sessionIdOf(req, json);
    if (challengeId.empty()) co_return badRequest("challengeId is required.");
    co_return jsonResponse(co_await games().acceptChallengeView(userId, challengeId));
}

drogon::Task<HttpResponsePtr> GamesController::declineChallenge(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId;
    if (auto denied = requireUser(req, userId)) co_return denied;
    const Json::Value json = bodyOf(req);
    const std::string challengeId = sessionIdOf(req, json);
    if (challengeId.empty()) co_return badRequest("challengeId is required.");
    co_return jsonResponse(co_await games().declineChallengeView(userId, challengeId));
}

drogon::Task<HttpResponsePtr> GamesController::listChallenges(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId;
    if (auto denied = requireUser(req, userId)) co_return denied;
    co_return jsonResponse(co_await games().listChallengesView(userId));
}

drogon::Task<HttpResponsePtr> GamesController::submitMultiplayer(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId;
    if (auto denied = requireUser(req, userId)) co_return denied;
    const Json::Value json = bodyOf(req);
    const std::string sessionId = sessionIdOf(req, json);
    if (sessionId.empty()) co_return badRequest("sessionId is required.");
    co_return jsonResponse(co_await games().finishMultiplayer(userId, sessionId));
}

drogon::Task<HttpResponsePtr> GamesController::getDaily(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId;
    if (auto denied = requireUser(req, userId)) co_return denied;
    co_return jsonResponse(co_await games().dailyBoard(userId));
}

drogon::Task<HttpResponsePtr> GamesController::startDaily(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId;
    if (auto denied = requireUser(req, userId)) co_return denied;
    const Json::Value json = bodyOf(req);
    co_return jsonResponse(co_await games().startDaily(userId, textField(json, req, "challengeId"), textField(json, req, "gameType")));
}

drogon::Task<HttpResponsePtr> GamesController::completeDaily(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId;
    if (auto denied = requireUser(req, userId)) co_return denied;
    const Json::Value json = bodyOf(req);
    const std::string sessionId = sessionIdOf(req, json);
    if (sessionId.empty()) co_return badRequest("sessionId is required.");
    co_return jsonResponse(co_await games().completeDailyView(userId, sessionId));
}

drogon::Task<HttpResponsePtr> GamesController::resetProgress(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    std::string userId;
    if (auto denied = requireUser(req, userId)) co_return denied;
    co_return jsonResponse(co_await games().resetProgressView(userId));
}

drogon::Task<HttpResponsePtr> GamesController::seedAchievements(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    co_return jsonResponse(co_await games().seedAchievementsView());
}

drogon::Task<HttpResponsePtr> GamesController::refreshLexicon(HttpRequestPtr req) {
    if (auto pre = preflight(req)) co_return pre;
    co_return jsonResponse(co_await games().refreshLexiconView());
}
