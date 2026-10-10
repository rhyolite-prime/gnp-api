#include "SubscriptionsController.h"
#include <chrono>
#include "dto/GrantNewsPaperAccessDto.h"
#include "dto/GuestSubscriptionDto.h"
#include "plugins/GnpServicePlugin.h"
#include "services/subscriptions/SubscriptionService.h"

void SubscriptionsController::getAll(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // write your application logic here
}

void SubscriptionsController::getUserSubscription(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // write your application logic here
}

void SubscriptionsController::manageGuestSubscription(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  auto jsonBody = req->getJsonObject();

  if (!jsonBody) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Invalid JSON body";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    return;
  }

  gnp::dto::GuestSubscriptionDto guestSubscriptionDto;
  guestSubscriptionDto.fromJson(*jsonBody);

  // Get tenant service from plugin
  auto plugin = app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &subscriptionService = plugin->getSubscriptionService();

  subscriptionService.manageGuestSubscription(
      guestSubscriptionDto,
      [callback](const gnp::dto::BaseApiResponse &result) {
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());

        callback(resp);
      });
}

Task<HttpResponsePtr> SubscriptionsController::manageGuestOneTimeBuy(const HttpRequestPtr req) {
  auto jsonBody = req->getJsonObject();

  if (!jsonBody) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Invalid JSON body";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k400BadRequest);
    co_return resp;
  }

  gnp::dto::GuestOnetimeBuyDto guestOnetimeBuyDto;
  guestOnetimeBuyDto.fromJson(*jsonBody);

  // Get tenant service from plugin
  auto plugin = app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &subscriptionService = plugin->getSubscriptionService();

  auto result = co_await subscriptionService.manageGuestOneTimeBuyAsync(
      guestOnetimeBuyDto);

  auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
  co_return resp;
}

Task<HttpResponsePtr> SubscriptionsController::manageUserOneTimeBuy(const HttpRequestPtr req) {

  auto newsPaperId = req->getParameter("newsPaperId");

  if (newsPaperId.empty()) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "newsPaperId is required";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k400BadRequest);
    co_return resp;
  }

  // Get userId from request attributes (set by JwtAuthFilter)
  auto userId = req->attributes()->get<std::string>("userId");

  if (userId.empty()) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "User ID not found in token";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k401Unauthorized);
    co_return resp;
  }

  auto plugin = app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &subscriptionService = plugin->getSubscriptionService();

  auto result = co_await subscriptionService.initializeUserOneTimeBuyAsync(
      newsPaperId, userId);

  auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
  co_return resp;
}

Task<HttpResponsePtr> SubscriptionsController::fulfillGuestOneTimeBuy(const HttpRequestPtr req) {

  auto reference = req->getParameter("reference");

  if (reference.empty()) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Reference is required";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k400BadRequest);
    co_return resp;
  }

  // Get tenant service from plugin
  auto plugin = app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &subscriptionService = plugin->getSubscriptionService();

  auto result =
      co_await subscriptionService.completeGuestOneTimeBuyAsync(reference);

  auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
  co_return resp;
}

Task<HttpResponsePtr> SubscriptionsController::fulfillUserOneTimeBuy(const HttpRequestPtr req) {

  auto reference = req->getParameter("reference");

  if (reference.empty()) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Reference is required";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k400BadRequest);
    co_return resp;
  }

  auto userId = req->attributes()->get<std::string>("userId");

  auto plugin = app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &subscriptionService = plugin->getSubscriptionService();

  auto result = co_await subscriptionService.completeUserOneTimeBuyAsync(
      userId, reference);

  auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
  co_return resp;
}

Task<HttpResponsePtr> SubscriptionsController::fulfillSubscriptionPayment(const HttpRequestPtr req) {
  auto reference = req->getParameter("reference");

  if (reference.empty()) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Reference is required";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k400BadRequest);
    co_return resp;
  }

  auto userId = req->attributes()->get<std::string>("userId");

  auto plugin = app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &subscriptionService = plugin->getSubscriptionService();

  auto result = co_await subscriptionService.fulfillSubscriptionPaymentAsync(userId, reference);

  auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
  co_return resp;
}

Task<HttpResponsePtr> SubscriptionsController::validateNewsPaperEntitlement(
    const HttpRequestPtr req) {

  auto newsPaperId = req->getParameter("newsPaperId");

  if (newsPaperId.empty()) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "News Paper Reference is required";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k400BadRequest);
    co_return resp;
  }

  // Get userId from request attributes (set by JwtAuthFilter)
  auto userId = req->attributes()->get<std::string>("userId");

  auto plugin = app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &subscriptionService = plugin->getSubscriptionService();

  auto result = co_await subscriptionService.validateNewsPaperEntitlementAsync(
      newsPaperId, userId);
  auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
  co_return resp;
}

Task<HttpResponsePtr> SubscriptionsController::getNewsPaperRedactedDetailsViaUniqueId(
    const HttpRequestPtr req) {

  auto uniqueId = req->getParameter("id");

  if (uniqueId.empty()) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "News Paper Reference is required";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k400BadRequest);
    co_return resp;
  }

  // Get userId and email from request attributes (set by JwtAuthFilter)
  auto userId = req->attributes()->get<std::string>("userId");
  auto email = req->attributes()->get<std::string>("email");

  auto plugin = app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &subscriptionService = plugin->getSubscriptionService();

  auto result =
      co_await subscriptionService.getNewsPaperRedactedDetailsWithUniqueIdAsync(
          uniqueId, userId, email);
  auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
  co_return resp;
}

void SubscriptionsController::grantNewsPaperAccess(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {

  auto jsonBody = req->getJsonObject();

  if (!jsonBody) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Invalid JSON body";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k400BadRequest);
    callback(resp);
  }

  gnp::dto::GrantNewsPaperAccessDto grantNewsPaperAccessDto;
  grantNewsPaperAccessDto.fromJson(*jsonBody);

  auto plugin = app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &subscriptionService = plugin->getSubscriptionService();

  subscriptionService.grantNewsPaperAccessToRequester(
      grantNewsPaperAccessDto,
      [callback](const gnp::dto::BaseApiResponse &result) {
        auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
        callback(resp);
      });
}

Task<HttpResponsePtr> SubscriptionsController::findNewsPaperByDateAndPublication(
    const HttpRequestPtr req) {
  auto publicationId = req->getParameter("publicationId");
  auto publicationDate = req->getParameter("publicationDate");

  if (publicationId.empty()) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Publication ID is required";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k400BadRequest);
    co_return resp;
  }

  // Get userId and email from request attributes (set by JwtAuthFilter)
  auto userId = req->attributes()->get<std::string>("userId");
  auto email = req->attributes()->get<std::string>("email");

  auto plugin = app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &subscriptionService = plugin->getSubscriptionService();

  auto result =
      co_await subscriptionService.readNewsPaperByDateAndPublicationAsync(
          publicationId, publicationDate, userId, email);

  auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
  co_return resp;
}

void SubscriptionsController::manageUserSubscription(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // write your application logic here
}

void SubscriptionsController::renew(
    const HttpRequestPtr &req,
    std::function<void(const HttpResponsePtr &)> &&callback) {
  // write your application logic here
}


Task<HttpResponsePtr> SubscriptionsController::renewSubscriptionViaDirectDebit(const HttpRequestPtr req) {

  //scans the database for subscription
  // initialize direct direct debit for each of the subscription records: either using actors or rabbit mq consumer or other efficient means in drogon c++
  // send receipt as sms notification to each user per subscription

  auto now = std::chrono::system_clock::now();
  auto expiryTime = now + std::chrono::hours(24 * 7);

  // Convert to time_t for formatting
  auto expiryTimeT = std::chrono::system_clock::to_time_t(expiryTime);
  std::tm* expiryTm = std::localtime(&expiryTimeT);

  // Format the date as a string (e.g., "04-Mar-2026" or "2026-03-11")
  char buffer[20];
  std::strftime(buffer, sizeof(buffer), "%d-%b-%Y", expiryTm);
  std::string expiryDate = buffer;

  std::string messageContent =
    "Your subscription has been renewed successfully. "
    "Package: Daily Graphic (Weekly), Amount: GHS 9.90. "
    "Valid till:" + expiryDate + ". Thank you! - GNP";

  auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &hubtelSmsApi = plugin->getHubtelSmsApi();
  co_await hubtelSmsApi.sendSms("0242573763", messageContent);
  co_await hubtelSmsApi.sendSms("0242602262", messageContent);

  //schedule on quartz one week ahead

  //sample response
  gnp::dto::BaseApiResponse response;
  response.success = false;
  response.error["message"] = "Subscription Renewal Processed successfully.";
  auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
  resp->setStatusCode(k200OK);
  co_return resp;



}

Task<HttpResponsePtr> SubscriptionsController::buyCopy(const HttpRequestPtr req) {
  auto jsonBody = req->getJsonObject();

  if (!jsonBody) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Invalid JSON body";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k400BadRequest);
    co_return resp;
  }

  gnp::dto::BuyNewspaperCopyDto buyNewspaperCopyDto;
  buyNewspaperCopyDto.fromJson(*jsonBody);

  // Get userId from request attributes (set by JwtAuthFilter)
  auto userId = req->attributes()->get<std::string>("userId");

  if (userId.empty()) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "User ID not found in token";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k401Unauthorized);
    co_return resp;
  }

  auto plugin = app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &subscriptionService = plugin->getSubscriptionService();

  auto result = co_await subscriptionService.buyCopy(userId, buyNewspaperCopyDto);

  auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
  co_return resp;
}

Task<HttpResponsePtr> SubscriptionsController::fulfillBuyCopy(const HttpRequestPtr req) {
  auto reference = req->getParameter("reference");

  if (reference.empty()) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Reference is required";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k400BadRequest);
    co_return resp;
  }

  auto plugin = app().getPlugin<gnp::plugins::GnpServicePlugin>();
  auto &subscriptionService = plugin->getSubscriptionService();

  auto result = co_await subscriptionService.fulFillBuyCopy(reference);

  auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
  co_return resp;
}