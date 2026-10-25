#include "PushController.h"

#include "dto/BaseApiResponse.h"
#include "plugins/GnpServicePlugin.h"

drogon::Task<HttpResponsePtr> PushController::getDashboard(HttpRequestPtr req) {
    auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
    auto &reportingService = plugin->getReportingService();

    auto apiResp = co_await reportingService.getPushDashboard();
    co_return HttpResponse::newHttpJsonResponse(apiResp.toJson());
}
