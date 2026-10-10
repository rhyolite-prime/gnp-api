#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

class SubscriptionsController
    : public drogon::HttpController<SubscriptionsController> {

public:
  static constexpr const char *PREFIX = "/api/v1/subscription/";
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(SubscriptionsController::getAll,  std::string(PREFIX) + "get-all", Get);
  ADD_METHOD_TO(SubscriptionsController::getUserSubscription, std::string(PREFIX) + "get-details", Get);
  ADD_METHOD_TO(SubscriptionsController::manageGuestSubscription,  std::string(PREFIX) + "guest", Post, Options);
  ADD_METHOD_TO(SubscriptionsController::manageGuestOneTimeBuy,  std::string(PREFIX) + "guest-onetime-buy", Post, Options);
  ADD_METHOD_TO(SubscriptionsController::manageUserOneTimeBuy, std::string(PREFIX) + "user-onetime-buy", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(SubscriptionsController::fulfillGuestOneTimeBuy, std::string(PREFIX) + "fulfill-guest-onetime", Get, Options);
  ADD_METHOD_TO(SubscriptionsController::fulfillUserOneTimeBuy, std::string(PREFIX) + "fulfill-user-onetime", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(SubscriptionsController::validateNewsPaperEntitlement, std::string(PREFIX) + "validate-newspaper-entitlement", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(SubscriptionsController::grantNewsPaperAccess, std::string(PREFIX) + "grant-newspaper-access", Post, Options);
  ADD_METHOD_TO(SubscriptionsController::getNewsPaperRedactedDetailsViaUniqueId, std::string(PREFIX) + "get-newspaper-redacted-details-via-unique-id", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(SubscriptionsController::findNewsPaperByDateAndPublication, std::string(PREFIX) + "find-newspaper-by-date", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(SubscriptionsController::buyCopy, std::string(PREFIX) + "buy-copy", Post, Options, "JwtAuthFilter");
  ADD_METHOD_TO(SubscriptionsController::fulfillBuyCopy, std::string(PREFIX) + "fulfill-buy-copy", Get, Options);
  ADD_METHOD_TO(SubscriptionsController::fulfillSubscriptionPayment, std::string(PREFIX) + "fulfill-subscription-payment", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(SubscriptionsController::manageUserSubscription, std::string(PREFIX) + "user", Post);
  ADD_METHOD_TO(SubscriptionsController::renew, std::string(PREFIX) + "renew", Post);
  ADD_METHOD_TO(SubscriptionsController::renewSubscriptionViaDirectDebit, std::string(PREFIX) + "renew-subscription-via-direct-debit", Get);
  METHOD_LIST_END

  // handler methods
  void getAll(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);

  void getUserSubscription(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);

  void manageGuestSubscription(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);

  Task<HttpResponsePtr> manageGuestOneTimeBuy(HttpRequestPtr req);

  Task<HttpResponsePtr> manageUserOneTimeBuy(HttpRequestPtr req);

  Task<HttpResponsePtr> fulfillGuestOneTimeBuy(HttpRequestPtr req);

  Task<HttpResponsePtr> fulfillUserOneTimeBuy(HttpRequestPtr req);

  Task<HttpResponsePtr> buyCopy(HttpRequestPtr req);

  Task<HttpResponsePtr> fulfillBuyCopy(HttpRequestPtr req);

  Task<HttpResponsePtr> fulfillSubscriptionPayment(HttpRequestPtr req);

  void manageUserSubscription(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);

  Task<HttpResponsePtr> validateNewsPaperEntitlement(HttpRequestPtr req);

  void grantNewsPaperAccess(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);

  Task<HttpResponsePtr> getNewsPaperRedactedDetailsViaUniqueId(HttpRequestPtr req);

  Task<HttpResponsePtr> findNewsPaperByDateAndPublication(HttpRequestPtr req);

  void renew(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);

    Task<HttpResponsePtr> renewSubscriptionViaDirectDebit(HttpRequestPtr req);
};
