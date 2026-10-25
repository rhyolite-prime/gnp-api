//
// Lightweight report-telemetry instrumentation (Phases 2–3 of
// docs/REPORTS_IMPLEMENTATION_PLAN_AND_API_SPEC.md §1.5).
//
// Each helper writes exactly one row into its event/ledger table and NEVER
// throws: a telemetry failure must not break the surrounding business flow.
// All helpers are fire-and-forget when awaited by their caller with
// `co_await` inside a try block, or can simply be awaited at the end of a
// success path.
//

#ifndef GNPAPI_REPORTINSTRUMENTATION_H
#define GNPAPI_REPORTINSTRUMENTATION_H

#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>
#include <drogon/utils/coroutine.h>

#include <string>

namespace gnp::services::reporting {

// edition_sales_ledger — timestamped single-copy sales
// (unlocks single_copy_paywall_funnel / newspaper_engagement / sales_revenue).
inline drogon::Task<void> recordEditionSale(const std::string &newspaperId,
                                            const std::string &publicationId,
                                            const std::string &userId,
                                            const std::string &buyerEmail,
                                            bool isGuestPurchase,
                                            const std::string &paymentId,
                                            const std::string &affiliateId,
                                            double unitPrice,
                                            double discountApplied,
                                            double amountPaid) {
    try {
        auto dbClient = drogon::app().getDbClient();
        if (!dbClient || newspaperId.empty() || publicationId.empty()) {
            co_return;
        }
        co_await dbClient->execSqlCoro(
            "INSERT INTO edition_sales_ledger "
            "(newspaper_id, publication_id, user_id, buyer_email, is_guest_purchase, "
            " payment_id, affiliate_id, unit_price, discount_applied, amount_paid, sold_at) "
            "VALUES ($1::uuid, $2::uuid, NULLIF($3, '')::uuid, NULLIF($4, ''), $5, "
            "        NULLIF($6, '')::uuid, NULLIF($7, '')::uuid, $8, $9, $10, NOW())",
            newspaperId, publicationId, userId, buyerEmail, isGuestPurchase,
            paymentId, affiliateId, unitPrice, discountApplied, amountPaid);
    } catch (const std::exception &e) {
        LOG_WARN << "recordEditionSale failed: " << e.what();
    }
    co_return;
}

// subscription_lifecycle_events — renewals / churn / dunning
// (unlocks subscription_renewals / subscriber_retention_cohorts).
inline drogon::Task<void> recordSubscriptionLifecycleEvent(const std::string &subscriptionId,
                                                           const std::string &userId,
                                                           const std::string &partnerId,
                                                           const std::string &fromPlanId,
                                                           const std::string &toPlanId,
                                                           const std::string &eventType,
                                                           double mrrDelta,
                                                           const std::string &paymentId,
                                                           const std::string &reasonCode) {
    try {
        auto dbClient = drogon::app().getDbClient();
        if (!dbClient || eventType.empty()) {
            co_return;
        }
        co_await dbClient->execSqlCoro(
            "INSERT INTO subscription_lifecycle_events "
            "(subscription_id, user_id, partner_id, from_plan_id, to_plan_id, "
            " event_type, mrr_delta, payment_id, reason_code, metadata, occurred_at) "
            "VALUES (NULLIF($1, '')::uuid, NULLIF($2, '')::uuid, NULLIF($3, '')::uuid, "
            "        NULLIF($4, '')::uuid, NULLIF($5, '')::uuid, $6, $7, "
            "        NULLIF($8, '')::uuid, NULLIF($9, ''), '{}'::jsonb, NOW())",
            subscriptionId, userId, partnerId, fromPlanId, toPlanId,
            eventType, mrrDelta, paymentId, reasonCode);
    } catch (const std::exception &e) {
        LOG_WARN << "recordSubscriptionLifecycleEvent failed: " << e.what();
    }
    co_return;
}

// campaign_recipient_events — per-recipient delivery / click telemetry
// (unlocks campaign_performance).
inline drogon::Task<void> recordCampaignRecipientEvent(const std::string &campaignId,
                                                       const std::string &userId,
                                                       const std::string &channel,
                                                       const std::string &eventType,
                                                       double attributedRevenue,
                                                       const std::string &paymentId = "") {
    try {
        auto dbClient = drogon::app().getDbClient();
        if (!dbClient || campaignId.empty() || eventType.empty()) {
            co_return;
        }
        co_await dbClient->execSqlCoro(
            "INSERT INTO campaign_recipient_events "
            "(campaign_id, user_id, channel, event_type, payment_id, attributed_revenue, occurred_at) "
            "VALUES ($1::uuid, NULLIF($2, '')::uuid, $3, $4, "
            "        NULLIF($5, '')::uuid, $6, NOW())",
            campaignId, userId, channel, eventType, paymentId, attributedRevenue);
    } catch (const std::exception &e) {
        LOG_WARN << "recordCampaignRecipientEvent failed: " << e.what();
    }
    co_return;
}

// archive_search_and_rag_logs — archive search & RAG telemetry
// (unlocks archive_rag_intelligence).
inline drogon::Task<void> recordArchiveQuery(const std::string &userId,
                                             const std::string &institutionId,
                                             const std::string &queryMode,
                                             const std::string &queryText,
                                             int targetDecade,
                                             const std::string &publicationId,
                                             int resultsCount,
                                             int citationsReturned,
                                             int latencyMs,
                                             int creditsConsumed,
                                             double avgSimilarity = -1.0) {
    try {
        auto dbClient = drogon::app().getDbClient();
        if (!dbClient || queryMode.empty()) {
            co_return;
        }
        co_await dbClient->execSqlCoro(
            "INSERT INTO archive_search_and_rag_logs "
            "(user_id, institution_id, query_mode, query_text, target_decade, publication_id, "
            " results_count, citations_returned, latency_ms, credits_consumed, avg_similarity, created_at) "
            "VALUES (NULLIF($1, '')::uuid, NULLIF($2, '')::uuid, $3, $4, $5, "
            "        NULLIF($6, '')::uuid, $7, $8, $9, $10, "
            "        CASE WHEN $11 < 0 THEN NULL ELSE $11 END, NOW())",
            userId, institutionId, queryMode, queryText, targetDecade,
            publicationId, resultsCount, citationsReturned, latencyMs,
            creditsConsumed, avgSimilarity);
    } catch (const std::exception &e) {
        LOG_WARN << "recordArchiveQuery failed: " << e.what();
    }
    co_return;
}

// security_audit_logs — privileged / security-relevant admin actions
// (unlocks security_audit_logs).
inline drogon::Task<void> recordSecurityAudit(const std::string &actorId,
                                              const std::string &actorName,
                                              const std::string &action,
                                              const std::string &module,
                                              const std::string &resource,
                                              const std::string &status = "Active",
                                              const std::string &ipAddress = "") {
    try {
        auto dbClient = drogon::app().getDbClient();
        if (!dbClient || action.empty()) {
            co_return;
        }
        co_await dbClient->execSqlCoro(
            "INSERT INTO security_audit_logs "
            "(actor_id, actor_name, action, module, resource, severity, ip_address, status, metadata, created_at) "
            "VALUES (NULLIF($1, '')::uuid, $2, $3, $4, $5, 'info', NULLIF($6, ''), $7, '{}'::jsonb, NOW())",
            actorId, actorName, action, module, resource, ipAddress, status);
    } catch (const std::exception &e) {
        LOG_WARN << "recordSecurityAudit failed: " << e.what();
    }
    co_return;
}

} // namespace gnp::services::reporting

#endif //GNPAPI_REPORTINSTRUMENTATION_H
