#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

// Push Studio supporting endpoints (see
// docs/REPORTS_IMPLEMENTATION_PLAN_AND_API_SPEC.md §2.11).
class PushController : public drogon::HttpController<PushController>
{
  public:
    static constexpr const char *PREFIX = "/api/v1/push/";

  METHOD_LIST_BEGIN
    ADD_METHOD_TO(PushController::getDashboard, std::string(PREFIX) + "dashboard", Get, Options, "JwtAuthFilter");

  METHOD_LIST_END

  // GET /api/v1/push/dashboard — VAPID info, delivery telemetry, recent broadcasts.
  drogon::Task<HttpResponsePtr> getDashboard(HttpRequestPtr req);
};
