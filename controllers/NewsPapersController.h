#pragma once

#include <drogon/HttpController.h>



using namespace drogon;

class NewsPapersController
    : public drogon::HttpController<NewsPapersController> {
public:
    static constexpr const char *PREFIX = "/api/v1/news-papers/";
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(NewsPapersController::getAll, std::string(PREFIX) + "get-all", Get, Options);
  ADD_METHOD_TO(NewsPapersController::getLatest, std::string(PREFIX) + "get-latest", Get, Options);
  ADD_METHOD_TO(NewsPapersController::getTopStories, std::string(PREFIX) + "get-top-stories", Get, Options);
  ADD_METHOD_TO(NewsPapersController::getRedactedDetails, std::string(PREFIX) + "get-redacted-details", Get, Options);
  ADD_METHOD_TO(NewsPapersController::getFullDetails, std::string(PREFIX) + "get-full-details", Get);

  ADD_METHOD_TO(NewsPapersController::GetFreeNewsPaperDetailsByPublication, std::string(PREFIX) + "get-free-newspaper-details-by-publication", Get, Options);

  ADD_METHOD_TO(NewsPapersController::GetPaidNewsPaperDetailsByPublication, std::string(PREFIX) + "get-paid-newspaper-details-by-publication", Get, Options);

  ADD_METHOD_TO(NewsPapersController::publish, std::string(PREFIX) + "publish", Get);

  ADD_METHOD_TO(NewsPapersController::incrementViewCount, std::string(PREFIX) + "increment-view-count", Get, Options, "JwtAuthFilter");

  ADD_METHOD_TO(NewsPapersController::trackUserEngagement, std::string(PREFIX) + "track-user-engagement", Post, Options, "JwtAuthFilter");

  ADD_METHOD_TO(NewsPapersController::updateUserEngagement, std::string(PREFIX) + "update-user-engagement", Post, Options, "JwtAuthFilter");

  ADD_METHOD_TO(NewsPapersController::searchArchives, std::string(PREFIX) + "search-archives", Post, Options);
  ADD_METHOD_TO(NewsPapersController::askArchives, std::string(PREFIX) + "ask-archives", Post, Options, "JwtAuthFilter");

  ADD_METHOD_TO(NewsPapersController::unPublish, std::string(PREFIX) + "unpublish", Get);
  ADD_METHOD_TO(NewsPapersController::ingestPublication, std::string(PREFIX) + "ingest", Post);
  ADD_METHOD_TO(NewsPapersController::update, std::string(PREFIX) + "update", Post);
  ADD_METHOD_TO(NewsPapersController::deleteNewsPaper, std::string(PREFIX) + "delete", Delete);
  METHOD_LIST_END

  // handler methods
  drogon::Task<HttpResponsePtr> getAll(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> getLatest(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> getTopStories(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> getRedactedDetails(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> getFullDetails(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> GetFreeNewsPaperDetailsByPublication(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> GetPaidNewsPaperDetailsByPublication(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> publish(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> unPublish(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> ingestPublication(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> trackUserEngagement(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> updateUserEngagement(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> searchArchives(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> askArchives(HttpRequestPtr req);
  void  incrementViewCount(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);
  void update(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);
  drogon::Task<HttpResponsePtr> deleteNewsPaper(HttpRequestPtr req);
};
