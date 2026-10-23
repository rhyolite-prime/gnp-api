//
// ReportingService core — dispatcher, cache wrapper and supporting
// partner directory endpoint.
//
// See docs/REPORTS_IMPLEMENTATION_PLAN_AND_API_SPEC.md §1.5 / §2.12.
//

#include "ReportingService.h"

#include <drogon/orm/DbClient.h>
#include <drogon/orm/Result.h>

#include <map>
#include <utility>

#include "constants/ErrorCodes.h"
#include "utils/ReportCache.h"
#include "utils/ReportResponseBuilder.h"
#include "ReportSqlHelpers.h"

using drogon::orm::DrogonDbException;
using gnp::dto::BaseApiResponse;
using gnp::dto::ReportFilterDto;
using gnp::utils::ReportCache;
using gnp::utils::ReportResponseBuilder;

namespace gnp::services {

drogon::Task<BaseApiResponse> ReportingService::serveReport(const std::string &slug,
                                                           const ReportFilterDto &filter,
                                                           BuilderFn builder,
                                                           bool bypassCache) {
    const std::string cacheKey = ReportCache::makeKey(slug, filter.toJson());

    if (!bypassCache && !filter.bypassCache) {
        auto cached = co_await ReportCache::get(cacheKey);
        if (cached.has_value()) {
            BaseApiResponse response;
            response.success = true;
            response.message = "Report generated successfully.";
            response.result = cached.value();
            response.result["endpoint"] = "/api/v1/reports/" + slug;
            co_return response;
        }
    }

    try {
        Json::Value result = co_await (this->*builder)(filter);

        // Populate the canonical endpoint path defensively.
        if (result.isObject() && (!result.isMember("endpoint") || result["endpoint"].asString().empty())) {
            result["endpoint"] = "/api/v1/reports/" + slug;
        }

        if (!bypassCache && !filter.bypassCache) {
            // Fill the 120 second TTL cache. drogon::Task is lazy, so the
            // write must actually be awaited (or detached) to run.
            co_await ReportCache::set(cacheKey, result);
        }

        BaseApiResponse response;
        response.success = true;
        response.message = "Report generated successfully.";
        response.result = result;
        co_return response;
    } catch (const DrogonDbException &e) {
        BaseApiResponse errorResponse;
        errorResponse.success = false;
        errorResponse.message = "Failed to generate report: " + slug;
        errorResponse.error["code"] = constants::ERR_DB_QUERY;
        errorResponse.error["message"] = "Database error while generating report.";
        errorResponse.error["detail"] = e.base().what();
        co_return errorResponse;
    } catch (const std::exception &e) {
        BaseApiResponse errorResponse;
        errorResponse.success = false;
        errorResponse.message = "Failed to generate report: " + slug;
        errorResponse.error["code"] = constants::ERR_UNSUPPORTED_OPERATION;
        errorResponse.error["message"] = e.what();
        co_return errorResponse;
    }
}

drogon::Task<BaseApiResponse> ReportingService::generateReport(const std::string &slug,
                                                              const ReportFilterDto &filter) {
    // Slug → engine map (see the Master Endpoint Reference Matrix, §2.12).
    static const std::map<std::string, BuilderFn> kBuilders = {
        {"sales-revenue", &ReportingService::buildSalesRevenueReport},
        {"payment-transactions", &ReportingService::buildPaymentTransactionsReport},
        {"subscription-renewals", &ReportingService::buildSubscriptionRenewalsReport},
        {"single-copy-paywall-funnel", &ReportingService::buildSingleCopyPaywallFunnelReport},
        {"subscriber-demographics", &ReportingService::buildSubscriberDemographicsReport},
        {"plan-distribution", &ReportingService::buildPlanDistributionReport},
        {"subscriber-retention-cohorts", &ReportingService::buildSubscriberRetentionCohortsReport},
        {"newspaper-engagement", &ReportingService::buildNewspaperEngagementReport},
        {"content-inventory", &ReportingService::buildContentInventoryReport},
        {"ingestion-jobs", &ReportingService::buildIngestionJobsReport},
        {"multimedia-originals-performance", &ReportingService::buildMultimediaOriginalsPerformanceReport},
        {"partner-engagement", &ReportingService::buildPartnerEngagementReport},
        {"partner-quota", &ReportingService::buildPartnerQuotaReport},
        {"partner-api-usage", &ReportingService::buildPartnerApiUsageReport},
        {"campaign-performance", &ReportingService::buildCampaignPerformanceReport},
        {"coupon-usage", &ReportingService::buildCouponUsageReport},
        {"affiliate-performance", &ReportingService::buildAffiliatePerformanceReport},
        {"affiliate-payout-reconciliation", &ReportingService::buildAffiliatePayoutReconciliationReport},
        {"push-notification-analytics", &ReportingService::buildPushNotificationAnalyticsReport},
        {"archive-rag-intelligence", &ReportingService::buildArchiveRagIntelligenceReport},
        {"institutional-archive-credits", &ReportingService::buildInstitutionalArchiveCreditsReport},
        {"games-engagement-retention", &ReportingService::buildGamesEngagementRetentionReport},
        {"admin-roles", &ReportingService::buildAdminRolesReport},
        {"api-key-audit", &ReportingService::buildApiKeyAuditReport},
        {"security-audit-logs", &ReportingService::buildSecurityAuditLogsReport},
        {"support-tickets-sla", &ReportingService::buildSupportTicketsSlaReport},
    };

    const auto it = kBuilders.find(slug);
    if (it == kBuilders.end()) {
        BaseApiResponse errorResponse;
        errorResponse.success = false;
        errorResponse.message = "Unknown report slug: " + slug;
        errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
        errorResponse.error["message"] = "Unknown report slug: " + slug;
        co_return errorResponse;
    }

    // Real-time audit logs are never cached; everything else uses the 120s TTL.
    const bool bypassCache = (slug == "security-audit-logs");
    co_return co_await serveReport(slug, filter, it->second, bypassCache);
}

// ---------------------------------------------------------------------------
// GET /api/v1/commercial-partners — partner directory dropdown used by the
// reports viewer (§2.6 Supporting Endpoint 4.0).
// ---------------------------------------------------------------------------
drogon::Task<BaseApiResponse> ReportingService::getCommercialPartnersDirectory(int pageNo, int pageSize) {
    auto dbClient = drogon::app().getDbClient();
    BaseApiResponse response;

    if (pageNo < 1) {
        pageNo = 1;
    }
    if (pageSize < 1 || pageSize > 500) {
        pageSize = 100;
    }

    try {
        const auto countResult = co_await dbClient->execSqlCoro(
            "SELECT COUNT(*)::BIGINT AS total FROM commercial_partners");
        const long long totalCount = reportdetail::asLong(countResult[0], "total");

        auto result = co_await dbClient->execSqlCoro(
            "SELECT "
            "  cp.id::text AS id, "
            "  cp.name AS name, "
            "  COALESCE(cp.contact_name, '') AS contact_name, "
            "  COALESCE(cp.billing_email, cp.contact_email, '') AS email, "
            "  COALESCE(cp.subscriber_quota, 0) AS quota, "
            "  COALESCE(( "
            "    SELECT COUNT(*) FROM users u "
            "    WHERE u.partner_id = cp.id AND u.is_active = TRUE "
            "  ), 0) AS enrolled, "
            "  COALESCE(cp.status, 'Active') AS status "
            "FROM commercial_partners cp "
            "ORDER BY cp.name ASC "
            "LIMIT $1 OFFSET $2",
            pageSize, (pageNo - 1) * pageSize);

        Json::Value data = Json::arrayValue;
        for (const auto &row : result) {
            Json::Value item(Json::objectValue);
            item["id"] = reportdetail::asString(row, "id");
            item["name"] = reportdetail::asString(row, "name");
            item["address"] = reportdetail::asString(row, "contact_name");
            item["email"] = reportdetail::asString(row, "email");
            item["quota"] = reportdetail::jsonLong(reportdetail::asLong(row, "quota"));
            item["enrolled"] = reportdetail::jsonLong(reportdetail::asLong(row, "enrolled"));
            item["status"] = reportdetail::asString(row, "status");
            data.append(item);
        }

        response.success = true;
        response.message = "Commercial partners retrieved successfully.";
        response.result["totalCount"] = reportdetail::jsonLong(totalCount);
        response.result["pageNo"] = pageNo;
        response.result["pageSize"] = pageSize;
        response.result["data"] = data;
        co_return response;
    } catch (const DrogonDbException &e) {
        response.success = false;
        response.message = "Failed to retrieve commercial partners.";
        response.error["code"] = constants::ERR_DB_QUERY;
        response.error["detail"] = e.base().what();
        co_return response;
    }
}

} // namespace gnp::services
