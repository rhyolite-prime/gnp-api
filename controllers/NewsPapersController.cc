#include "NewsPapersController.h"
#include <drogon/HttpAppFramework.h>
#include <functional>
#include <memory>

#include "dto/PublicationSearchRequestDto.h"
#include "dto/UserEngagementDto.h"
#include "plugins/GnpServicePlugin.h"
#include "services/newspapers/NewspaperService.h"

drogon::Task<HttpResponsePtr> NewsPapersController::getAll(const HttpRequestPtr req) {
  int pageSize = 10; // Default page size
  int pageNo = 1;    //  Default page number

  if (!req->getParameter("pageSize").empty()) {
    try {
      pageSize = std::stoi(req->getParameter("pageSize"));
      pageSize = std::max(1, std::min(100, pageSize)); // Limit between 1-100
    } catch (...) {
      // Keep default if conversion fails
    }
  }

  if (!req->getParameter("pageNo").empty()) {
    try {
      pageNo = std::stoi(req->getParameter("pageNo"));
      pageNo = std::max(1, pageNo); // Ensure page number is at least 1
    } catch (...) {
      // Keep default if conversion fails
    }
  }

  std::string query = req->getParameter("query");
  if (query.empty()) {
    query = ""; // Default to empty string if not specified
  }

  std::string publicationId = req->getParameter("publicationId");
  if (publicationId.empty()) {
    publicationId = ""; //
  }

  std::string startDate = req->getParameter("startDate");
  if (startDate.empty()) {
    startDate = ""; //
  }

  std::string endDate = req->getParameter("endDate");
  if (endDate.empty()) {
    endDate = ""; //
  }

  auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &newsPaperService = plugin->getNewsPaperService();

  auto result = co_await newsPaperService.getAllAsync(
      pageNo, pageSize, publicationId, startDate, endDate, query);

  auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
  co_return resp;
}



drogon::Task<HttpResponsePtr> NewsPapersController::getLatest(const HttpRequestPtr req) {
  int pageSize = 10; // Default page size
  int pageNo = 1;    //  Default page number

  if (!req->getParameter("pageSize").empty()) {
    try {
      pageSize = std::stoi(req->getParameter("pageSize"));
      pageSize = std::max(1, std::min(100, pageSize)); // Limit between 1-100
    } catch (...) {
      // Keep default if conversion fails
    }
  }

  if (!req->getParameter("pageNo").empty()) {
    try {
      pageNo = std::stoi(req->getParameter("pageNo"));
      pageNo = std::max(1, pageNo); // Ensure page number is at least 1
    } catch (...) {
      // Keep default if conversion fails
    }
  }

  auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &newsPaperService = plugin->getNewsPaperService();

  auto result = co_await newsPaperService.getLatestNewsPapers(pageNo, pageSize);

  auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
  co_return resp;
}



drogon::Task<HttpResponsePtr> NewsPapersController::getTopStories(const HttpRequestPtr req) {

  auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &joomlaApi = plugin->getJoomlaApi();

  auto result = co_await joomlaApi.getArticles(4, 0);

  auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
  co_return resp;
}



drogon::Task<HttpResponsePtr> NewsPapersController::getRedactedDetails(const HttpRequestPtr req) {
  if (req->getParameter("id").empty()) {

    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Missing required parameter: id";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k400BadRequest);
    co_return resp;
  }

  std::string id = req->getParameter("id");

  // Get tenant service from plugin
  auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &newsPaperService = plugin->getNewsPaperService();

  // Call service method
  auto result = co_await newsPaperService.getRedactedDetailsAsync(id);
  co_return HttpResponse::newHttpJsonResponse(result.toJson());
}

drogon::Task<HttpResponsePtr> NewsPapersController::getFullDetails(const HttpRequestPtr req) {
  if (req->getParameter("id").empty()) {
    // Missing tenant ID - return early
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Missing required parameter: id";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k400BadRequest);
    co_return resp;
  }

  std::string id = req->getParameter("id");
  std::string privateKey = req->getHeader("Vitamin");

  if (privateKey.empty()) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Missing required Header";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k500InternalServerError);
    co_return resp;
  }

  // compare header with value in custom config
  auto &app = drogon::app();
  auto customConfig = app.getCustomConfig();
  std::string privateKeyInConfig = customConfig["PrivateKey"].asString();

  if (privateKey != privateKeyInConfig) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Invalid Header";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k500InternalServerError);
    co_return resp;
  }

  // Get tenant service from plugin
  auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &newsPaperService = plugin->getNewsPaperService();

  // Call service method
  auto result = co_await newsPaperService.getDetails(id);
  co_return HttpResponse::newHttpJsonResponse(result.toJson());
}

drogon::Task<HttpResponsePtr> NewsPapersController::GetFreeNewsPaperDetailsByPublication(const HttpRequestPtr req) {
  std::string publicationId = req->getParameter("publicationId");
  std::string date = req->getParameter("date");
  std::string privateKey = req->getHeader("Vitamin");

  if (privateKey.empty()) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Missing required Header";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k500InternalServerError);
    co_return resp;
  }

  auto &app = drogon::app();
  auto customConfig = app.getCustomConfig();
  std::string privateKeyInConfig = customConfig["PrivateKey"].asString();

  if (privateKey != privateKeyInConfig) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Invalid Header";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k500InternalServerError);
    co_return resp;
  }

  auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &newsPaperService = plugin->getNewsPaperService();

  auto result =
      co_await newsPaperService.getFreeNewsPaperDetailsByPublicationAsync(
          publicationId, date);
  co_return HttpResponse::newHttpJsonResponse(result.toJson());
}

drogon::Task<HttpResponsePtr> NewsPapersController::GetPaidNewsPaperDetailsByPublication(const HttpRequestPtr req) {
  std::string publicationId = req->getParameter("publicationId");
  std::string date = req->getParameter("date");
  std::string privateKey = req->getHeader("Vitamin");

  if (privateKey.empty()) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Missing required Header";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k500InternalServerError);
    co_return resp;
  }

  auto &app = drogon::app();
  auto customConfig = app.getCustomConfig();
  std::string privateKeyInConfig = customConfig["PrivateKey"].asString();

  if (privateKey != privateKeyInConfig) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Invalid Header";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k500InternalServerError);
    co_return resp;
  }

  auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &newsPaperService = plugin->getNewsPaperService();

  auto result =
      co_await newsPaperService.getPaidNewsPaperDetailsByPublicationAsync(
          publicationId, date);
  co_return HttpResponse::newHttpJsonResponse(result.toJson());
}

drogon::Task<HttpResponsePtr> NewsPapersController::publish(const HttpRequestPtr req) {
  if (req->getParameter("id").empty()) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Missing required parameter: id";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k400BadRequest);
    co_return resp;
  }

  std::string id = req->getParameter("id");

  auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &newsPaperService = plugin->getNewsPaperService();

  auto result = co_await newsPaperService.publishAsync(id);
  auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
  co_return resp;
}

drogon::Task<HttpResponsePtr> NewsPapersController::unPublish(const HttpRequestPtr req) {
  if (req->getParameter("id").empty()) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Missing required parameter: id";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k400BadRequest);
    co_return resp;
  }

  std::string id = req->getParameter("id");

  auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &newsPaperService = plugin->getNewsPaperService();

  auto result = co_await newsPaperService.unPublishAsync(id);
  auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
  co_return resp;
}

drogon::Task<HttpResponsePtr> NewsPapersController::ingestPublication(const HttpRequestPtr req) {
  auto jsonBody = req->getJsonObject();

  if (!jsonBody) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Invalid JSON body";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k400BadRequest);
    co_return resp;
  }

  gnp::dto::IngestNewsPaperDto dto;
  dto.fromJson(*jsonBody);

  // Get tenant service from plugin
  auto plugin = app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &newsPaperService = plugin->getNewsPaperService();

  auto result = co_await newsPaperService.ingestAsync(dto);
  co_return HttpResponse::newHttpJsonResponse(result.toJson());
}


drogon::Task<HttpResponsePtr> NewsPapersController::trackUserEngagement(HttpRequestPtr req) {

  auto userId = req->attributes()->get<std::string>("userId");

  auto jsonBody = req->getJsonObject();

  if (!jsonBody) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Invalid JSON body";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k400BadRequest);
    co_return resp;
  }

  gnp::dto::UserEngagementDto dto;
  dto.fromJson(*jsonBody);

  auto plugin = app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &newsPaperService = plugin->getNewsPaperService();

  auto result = co_await newsPaperService.trackUserEngagement(dto, userId);
  co_return HttpResponse::newHttpJsonResponse(result.toJson());

}


drogon::Task<HttpResponsePtr> NewsPapersController::updateUserEngagement(HttpRequestPtr req) {

  auto userId = req->attributes()->get<std::string>("userId");

  auto jsonBody = req->getJsonObject();

  if (!jsonBody) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Invalid JSON body";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k400BadRequest);
    co_return resp;
  }

  gnp::dto::UserEngagementDto dto;
  dto.fromJson(*jsonBody);

  auto plugin = app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &newsPaperService = plugin->getNewsPaperService();

  auto result = co_await newsPaperService.updateUserEngagement(dto, userId);
  co_return HttpResponse::newHttpJsonResponse(result.toJson());

}


drogon::Task<HttpResponsePtr> NewsPapersController::searchArchives(HttpRequestPtr req) {

  auto userId = req->attributes()->get<std::string>("userId");

  auto jsonBody = req->getJsonObject();

  if (!jsonBody) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Invalid JSON body";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k400BadRequest);
    co_return resp;
  }

  gnp::dto::PublicationSearchRequestDto dto;
  dto.fromJson(*jsonBody);

  auto plugin = app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &newsPaperService = plugin->getNewsPaperService();

  auto result = co_await newsPaperService.searchArchives(dto, userId);
  co_return HttpResponse::newHttpJsonResponse(result.toJson());
}


void NewsPapersController::update(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {}

drogon::Task<HttpResponsePtr> NewsPapersController::deleteNewsPaper(const HttpRequestPtr req) {

  if (req->getParameter("id").empty()) {
    // Missing tenant ID - return early
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Missing required parameter: id";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k400BadRequest);
    co_return resp;
  }

  std::string id = req->getParameter("id");

  auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &newsPaperService = plugin->getNewsPaperService();

  // Call service method to delete the tenant
  auto result = co_await newsPaperService.deleteNewspaperAsync(id);
  co_return HttpResponse::newHttpJsonResponse(result.toJson());
}

void NewsPapersController::incrementViewCount(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  if (req->getParameter("id").empty()) {

    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Missing required parameter: id";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    return;
  }

  std::string id = req->getParameter("id");

  auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &newsPaperService = plugin->getNewsPaperService();

  // Call service method to delete the tenant
  newsPaperService.incrementViewCount(
      id, [callback](const gnp::dto::BaseApiResponse &result) {
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        callback(resp);
      });
}
