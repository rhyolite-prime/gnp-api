#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

class ReportsController : public drogon::HttpController<ReportsController>
{
  public:
    static constexpr const char *PREFIX = "/api/v1/reports/";

  METHOD_LIST_BEGIN
    // ---- Group 1: Revenue & Financial --------------------------------------
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "sales-revenue", Post, Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "payment-transactions", Post, Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(ReportsController::generatePartnerInvoice, std::string(PREFIX) + "partner-invoices", Post, Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(ReportsController::generatePartnerInvoice, std::string(PREFIX) + "partner-invoice", Post, Get, Options, "JwtAuthFilter"); // legacy alias
    ADD_METHOD_TO(ReportsController::sendPartnerInvoiceEmail, std::string(PREFIX) + "partner-invoice-email", Post, Options, "JwtAuthFilter");
    ADD_METHOD_TO(ReportsController::sendPartnerInvoiceEmail, std::string(PREFIX) + "send-partner-invoice-email", Post, Options, "JwtAuthFilter"); // legacy alias
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "subscription-renewals", Post, Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "single-copy-paywall-funnel", Post, Get, Options, "JwtAuthFilter");

    // ---- Group 2: User & Subscriber ---------------------------------------
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "subscriber-demographics", Post, Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "plan-distribution", Post, Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "subscriber-retention-cohorts", Post, Get, Options, "JwtAuthFilter");

    // ---- Group 3: Content & Newspaper -------------------------------------
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "newspaper-engagement", Post, Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "content-inventory", Post, Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "ingestion-jobs", Post, Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "multimedia-originals-performance", Post, Get, Options, "JwtAuthFilter");

    // ---- Group 4: Commercial Partner B2B ----------------------------------
    ADD_METHOD_TO(ReportsController::getCommercialPartnersDirectory, "/api/v1/commercial-partners", Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "partner-engagement", Post, Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "partner-quota", Post, Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "partner-api-usage", Post, Get, Options, "JwtAuthFilter");

    // ---- Group 5: Marketing & Campaigns -----------------------------------
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "campaign-performance", Post, Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "coupon-usage", Post, Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "affiliate-performance", Post, Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "affiliate-payout-reconciliation", Post, Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "push-notification-analytics", Post, Get, Options, "JwtAuthFilter");

    // ---- Group 6: Historical Archives & AI RAG ----------------------------
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "archive-rag-intelligence", Post, Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "institutional-archive-credits", Post, Get, Options, "JwtAuthFilter");

    // ---- Group 7: Interactive Games & Puzzles -----------------------------
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "games-engagement-retention", Post, Get, Options, "JwtAuthFilter");

    // ---- Group 8: Admin & Security Audit ----------------------------------
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "admin-roles", Post, Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "api-key-audit", Post, Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "security-audit-logs", Post, Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(ReportsController::generateReport, std::string(PREFIX) + "support-tickets-sla", Post, Get, Options, "JwtAuthFilter");

  METHOD_LIST_END

  // Standard report entry point — the report slug is derived from the request
  // path (e.g. POST /api/v1/reports/sales-revenue -> "sales-revenue").
  drogon::Task<HttpResponsePtr> generateReport(HttpRequestPtr req);

  // Printable A4 partner invoice (special invoice schema, §2.3 Endpoint 1.3).
  drogon::Task<HttpResponsePtr> generatePartnerInvoice(HttpRequestPtr req);

  // Async SMTP dispatch of the partner invoice (§2.3 Endpoint 1.3b).
  drogon::Task<HttpResponsePtr> sendPartnerInvoiceEmail(HttpRequestPtr req);

  // Commercial partners dropdown directory (§2.6 Supporting Endpoint 4.0).
  drogon::Task<HttpResponsePtr> getCommercialPartnersDirectory(HttpRequestPtr req);
};
