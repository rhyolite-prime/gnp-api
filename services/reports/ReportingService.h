// ReportingService — engines for all 27 platform reports + supporting
// push dashboard & commercial partner directory.
//
// See docs/REPORTS_IMPLEMENTATION_PLAN_AND_API_SPEC.md for the full
// request/response contracts (Part II). Every builder returns the standard
// report result payload produced by gnp::utils::ReportResponseBuilder:
//   { endpoint, summary[], charts[], columns[], rows[], partners? }

#ifndef GNPAPI_REPORTINGSERVICE_H
#define GNPAPI_REPORTINGSERVICE_H

#include <drogon/drogon.h>
#include <drogon/utils/coroutine.h>
#include <json/json.h>

#include <string>

#include "dto/BaseApiResponse.h"
#include "dto/ReportDto.h"

namespace gnp::services {

class ReportingService {
public:
    // Single entry point used by ReportsController: maps the URL slug
    // (e.g. "sales-revenue") to its engine, applies the 120s Redis cache
    // and returns the standard API envelope.
    drogon::Task<dto::BaseApiResponse> generateReport(const std::string &slug,
                                                     const dto::ReportFilterDto &filter);

    // GET /api/v1/commercial-partners (partner directory dropdown).
    drogon::Task<dto::BaseApiResponse> getCommercialPartnersDirectory(int pageNo, int pageSize);

    // GET /api/v1/push/dashboard (Push Studio).
    drogon::Task<dto::BaseApiResponse> getPushDashboard();

private:
    using BuilderFn = drogon::Task<Json::Value> (ReportingService::*)(const dto::ReportFilterDto &);

    // Cache-aware wrapper shared by every standard report.
    drogon::Task<dto::BaseApiResponse> serveReport(const std::string &slug,
                                                  const dto::ReportFilterDto &filter,
                                                  BuilderFn builder,
                                                  bool bypassCache = false);

    // ---- Group 1 — Revenue & Financial ------------------------------------
    drogon::Task<Json::Value> buildSalesRevenueReport(const dto::ReportFilterDto &filter);
    drogon::Task<Json::Value> buildPaymentTransactionsReport(const dto::ReportFilterDto &filter);
    drogon::Task<Json::Value> buildSubscriptionRenewalsReport(const dto::ReportFilterDto &filter);
    drogon::Task<Json::Value> buildSingleCopyPaywallFunnelReport(const dto::ReportFilterDto &filter);

    // ---- Group 2 — User & Subscriber --------------------------------------
    drogon::Task<Json::Value> buildSubscriberDemographicsReport(const dto::ReportFilterDto &filter);
    drogon::Task<Json::Value> buildPlanDistributionReport(const dto::ReportFilterDto &filter);
    drogon::Task<Json::Value> buildSubscriberRetentionCohortsReport(const dto::ReportFilterDto &filter);

    // ---- Group 3 — Content & Newspaper ------------------------------------
    drogon::Task<Json::Value> buildNewspaperEngagementReport(const dto::ReportFilterDto &filter);
    drogon::Task<Json::Value> buildContentInventoryReport(const dto::ReportFilterDto &filter);
    drogon::Task<Json::Value> buildIngestionJobsReport(const dto::ReportFilterDto &filter);
    drogon::Task<Json::Value> buildMultimediaOriginalsPerformanceReport(const dto::ReportFilterDto &filter);

    // ---- Group 4 — Commercial Partner B2B ---------------------------------
    drogon::Task<Json::Value> buildPartnerEngagementReport(const dto::ReportFilterDto &filter);
    drogon::Task<Json::Value> buildPartnerQuotaReport(const dto::ReportFilterDto &filter);
    drogon::Task<Json::Value> buildPartnerApiUsageReport(const dto::ReportFilterDto &filter);

    // ---- Group 5 — Marketing & Campaigns ---------------------------------
    drogon::Task<Json::Value> buildCampaignPerformanceReport(const dto::ReportFilterDto &filter);
    drogon::Task<Json::Value> buildCouponUsageReport(const dto::ReportFilterDto &filter);
    drogon::Task<Json::Value> buildAffiliatePerformanceReport(const dto::ReportFilterDto &filter);
    drogon::Task<Json::Value> buildAffiliatePayoutReconciliationReport(const dto::ReportFilterDto &filter);
    drogon::Task<Json::Value> buildPushNotificationAnalyticsReport(const dto::ReportFilterDto &filter);

    // ---- Group 6 — Historical Archives & AI RAG ---------------------------
    drogon::Task<Json::Value> buildArchiveRagIntelligenceReport(const dto::ReportFilterDto &filter);
    drogon::Task<Json::Value> buildInstitutionalArchiveCreditsReport(const dto::ReportFilterDto &filter);

    // ---- Group 7 — Interactive Games & Puzzles ----------------------------
    drogon::Task<Json::Value> buildGamesEngagementRetentionReport(const dto::ReportFilterDto &filter);

    // ---- Group 8 — Admin & Security Audit ---------------------------------
    drogon::Task<Json::Value> buildAdminRolesReport(const dto::ReportFilterDto &filter);
    drogon::Task<Json::Value> buildApiKeyAuditReport(const dto::ReportFilterDto &filter);
    drogon::Task<Json::Value> buildSecurityAuditLogsReport(const dto::ReportFilterDto &filter);
    drogon::Task<Json::Value> buildSupportTicketsSlaReport(const dto::ReportFilterDto &filter);
};

} // namespace gnp::services

#endif //GNPAPI_REPORTINGSERVICE_H
