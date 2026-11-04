#include "AffiliateController.h"

#include "dto/AffiliateDto.h"
#include "dto/AffiliateSignupDto.h"
#include "plugins/GnpServicePlugin.h"


drogon::Task<HttpResponsePtr> AffiliateController::getAllCommissions(HttpRequestPtr req)
{
    int pageNo = 1;
    int pageSize = 10;

    auto pageNoStr = req->getParameter("pageNo");
    if (!pageNoStr.empty()) {
        pageNo = std::stoi(pageNoStr);
    }

    auto pageSizeStr = req->getParameter("pageSize");
    if (!pageSizeStr.empty()) {
        pageSize = std::stoi(pageSizeStr);
    }

    std::string query = req->getParameter("query");
    if (query.empty()) {
        query = "";
    }

    std::string sortBy = req->getParameter("sortBy");
    if (sortBy.empty()) {
        sortBy = "";
    }

    auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
    auto &affiliateService = plugin->getAffiliateService();

    auto result = co_await affiliateService.getAll(pageNo, pageSize, query, 0, sortBy);
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    co_return resp;
}



drogon::Task<HttpResponsePtr> AffiliateController::getAllPayouts(HttpRequestPtr req)
{
    int pageNo = 1;
    int pageSize = 10;

    auto pageNoStr = req->getParameter("pageNo");
    if (!pageNoStr.empty()) {
        pageNo = std::stoi(pageNoStr);
    }

    auto pageSizeStr = req->getParameter("pageSize");
    if (!pageSizeStr.empty()) {
        pageSize = std::stoi(pageSizeStr);
    }

    std::string query = req->getParameter("query");
    if (query.empty()) {
        query = "";
    }

    std::string sortBy = req->getParameter("sortBy");
    if (sortBy.empty()) {
        sortBy = "";
    }

    auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
    auto &affiliateService = plugin->getAffiliateService();

    auto result = co_await affiliateService.getAll(pageNo, pageSize, query, 0, sortBy);
    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    co_return resp;
}



drogon::Task<HttpResponsePtr> AffiliateController::signup(HttpRequestPtr req) {

    auto jsonPtr = req->getJsonObject();
    if (!jsonPtr) {
        gnp::dto::BaseApiResponse response;
        response.success = false;
        response.error["message"] = "Invalid JSON body";
        auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
        resp->setStatusCode(k400BadRequest);
        co_return resp;
    }

    gnp::dto::AffiliateSignupDto dto;
    dto.fromJson(*jsonPtr);

    auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
    auto &affiliateService = plugin->getAffiliateService();

    auto apiResp = co_await affiliateService.submitApplication(dto);
    co_return HttpResponse::newHttpJsonResponse(apiResp.toJson());

}


drogon::Task<HttpResponsePtr> AffiliateController::getRecentNewspapers(HttpRequestPtr req, const std::string &affiliateId) {

    auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
    auto &newsPaperService = plugin->getNewsPaperService();

    auto result = co_await newsPaperService.getRecentNewspapersForAffiliate(1, 10, affiliateId);

    auto resp = HttpResponse::newHttpJsonResponse(result.toJson());
    co_return resp;
}