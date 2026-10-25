#include "ReportsController.h"

#include <drogon/orm/Exception.h>

#include <algorithm>
#include <string>

#include "dto/BaseApiResponse.h"
#include "dto/ReportDto.h"
#include "plugins/GnpServicePlugin.h"

using gnp::dto::BaseApiResponse;
using gnp::dto::ReportDto;
using gnp::dto::ReportFilterDto;

namespace {

// Extracts the trailing URL segment: "/api/v1/reports/sales-revenue" -> "sales-revenue"
std::string reportSlugFromPath(const std::string &path) {
    const auto pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

HttpResponsePtr jsonResponse(const BaseApiResponse &apiResp) {
    return HttpResponse::newHttpJsonResponse(apiResp.toJson());
}

HttpResponsePtr badRequest(const std::string &message) {
    BaseApiResponse response;
    response.success = false;
    response.message = message;
    response.error["message"] = message;
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k400BadRequest);
    return resp;
}

// Populates the legacy ReportDto from the unified filter (POST JSON + GET query).
ReportDto reportDtoFromFilter(const ReportFilterDto &filter) {
    ReportDto dto;
    dto.setPartnerId(filter.partnerId.value_or(""));
    dto.setStartDate(filter.startDate);
    dto.setEndDate(filter.endDate);
    return dto;
}

} // namespace

drogon::Task<HttpResponsePtr> ReportsController::generateReport(HttpRequestPtr req) {
    const std::string slug = reportSlugFromPath(req->getPath());

    auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
    auto &reportingService = plugin->getReportingService();

    const ReportFilterDto filter = ReportFilterDto::fromRequest(req);
    auto apiResp = co_await reportingService.generateReport(slug, filter);
    co_return jsonResponse(apiResp);
}

drogon::Task<HttpResponsePtr> ReportsController::generatePartnerInvoice(HttpRequestPtr req) {
    const ReportFilterDto filter = ReportFilterDto::fromRequest(req);
    if (!filter.partnerId.has_value() || filter.partnerId->empty()) {
        co_return badRequest("partnerId is required");
    }

    const ReportDto dto = reportDtoFromFilter(filter);

    auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
    auto &commercialPartnerService = plugin->getCommercialPartnerService();

    auto apiResp = co_await commercialPartnerService.getPartnerInvoiceGenerationReport(dto);
    co_return jsonResponse(apiResp);
}

drogon::Task<HttpResponsePtr> ReportsController::sendPartnerInvoiceEmail(HttpRequestPtr req) {
    const ReportFilterDto filter = ReportFilterDto::fromRequest(req);
    if (!filter.partnerId.has_value() || filter.partnerId->empty()) {
        co_return badRequest("partnerId is required");
    }

    const ReportDto dto = reportDtoFromFilter(filter);

    auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
    auto &commercialPartnerService = plugin->getCommercialPartnerService();

    auto apiResp = co_await commercialPartnerService.sendPartnerInvoiceByMail(dto);

    // Contract (§2.3 Endpoint 1.3b): { sent, invoiceNo, recipientEmail, dispatchedAt }
    if (apiResp.success) {
        Json::Value shaped(Json::objectValue);
        shaped["sent"] = true;
        shaped["invoiceNo"] = apiResp.result.get("invoiceNo", "");
        shaped["recipientEmail"] = apiResp.result.get("recipientEmail",
                                                      apiResp.result.get("sentTo", ""));
        shaped["dispatchedAt"] = apiResp.result.get("dispatchedAt", "");
        apiResp.result = shaped;
    }
    co_return jsonResponse(apiResp);
}

drogon::Task<HttpResponsePtr> ReportsController::getCommercialPartnersDirectory(HttpRequestPtr req) {
    int pageNo = 1;
    int pageSize = 100;

    try {
        const std::string pageNoParam = req->getParameter("pageNo");
        if (!pageNoParam.empty()) {
            pageNo = std::max(1, std::stoi(pageNoParam));
        }
    } catch (...) {
        pageNo = 1;
    }
    try {
        const std::string pageSizeParam = req->getParameter("pageSize");
        if (!pageSizeParam.empty()) {
            pageSize = std::max(1, std::min(500, std::stoi(pageSizeParam)));
        }
    } catch (...) {
        pageSize = 100;
    }

    auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
    auto &reportingService = plugin->getReportingService();

    auto apiResp = co_await reportingService.getCommercialPartnersDirectory(pageNo, pageSize);
    co_return jsonResponse(apiResp);
}
