//
// ReportingService — Group 5: Marketing & Campaigns report engines
// (campaign_performance, coupon_usage, affiliate_performance,
//  affiliate_payout_reconciliation, push_notification_analytics).
//
// See docs/REPORTS_IMPLEMENTATION_PLAN_AND_API_SPEC.md §2.7.
//

#include "ReportingService.h"

#include <drogon/orm/DbClient.h>
#include <drogon/orm/Result.h>

#include "ReportSqlHelpers.h"
#include "utils/ReportResponseBuilder.h"

using gnp::dto::ReportFilterDto;
using gnp::utils::ReportResponseBuilder;
using namespace gnp::services::reportdetail;

namespace gnp::services {

// ---------------------------------------------------------------------------
// Endpoint 5.1 — POST /api/v1/reports/campaign-performance
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildCampaignPerformanceReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/campaign-performance");

    const std::string channelFilter = filter.channel.value_or("");

    // ---- Summary -----------------------------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  COUNT(*) FILTER (WHERE e.event_type = 'delivered') AS delivered, "
            "  COUNT(*) FILTER (WHERE e.event_type IN ('opened', 'viewed')) AS opened, "
            "  COUNT(*) FILTER (WHERE e.event_type = 'clicked') AS clicked, "
            "  COALESCE(SUM(COALESCE(e.attributed_revenue, 0)::NUMERIC) "
            "           FILTER (WHERE e.event_type = 'converted'), 0) AS revenue "
            "FROM campaign_recipient_events e "
            "WHERE e.occurred_at >= $1::date AND e.occurred_at < ($2::date + INTERVAL '1 day') "
            "  AND ($3 = '' OR LOWER(e.channel) = $3)",
            filter.startDate, filter.endDate, channelFilter);

        const double delivered = (double)asLong(rows[0], "delivered");
        const double opened = (double)asLong(rows[0], "opened");
        const double clicked = (double)asLong(rows[0], "clicked");

        builder.addSummary("messagesDelivered", "Messages Delivered", delivered, "number");
        builder.addSummary("openOrViewRatePct", "Open / View Rate", pct(opened, delivered), "percent");
        builder.addSummary("clickThroughRatePct", "Click-Through Rate", pct(clicked, delivered), "percent");
        builder.addSummary("attributedRevenueGhs", "Attributed Revenue (GHS)",
                           asDouble(rows[0], "revenue"), "currency");
    }

    // ---- Chart: channel_performance_comparison (bar_line) ------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(e.channel, ''), 'unknown') AS channel, "
            "       COUNT(*) FILTER (WHERE e.event_type = 'delivered') AS delivered, "
            "       COUNT(*) FILTER (WHERE e.event_type = 'clicked') AS clicked "
            "FROM campaign_recipient_events e "
            "WHERE e.occurred_at >= $1::date AND e.occurred_at < ($2::date + INTERVAL '1 day') "
            "  AND ($3 = '' OR LOWER(e.channel) = $3) "
            "GROUP BY 1 ORDER BY delivered DESC",
            filter.startDate, filter.endDate, channelFilter);

        Json::Value labels = Json::arrayValue;
        Json::Value delivered = Json::arrayValue;
        Json::Value ctr = Json::arrayValue;
        for (const auto &row : rows) {
            labels.append(asString(row, "channel"));
            const double d = (double)asLong(row, "delivered");
            const double c = (double)asLong(row, "clicked");
            delivered.append(jsonLong((long long)d));
            ctr.append(jsonDouble(pct(c, d)));
        }
        Json::Value series = Json::arrayValue;
        series.append(ReportResponseBuilder::makeSeries("Delivered", "bar", delivered));
        series.append(ReportResponseBuilder::makeSeries("CTR %", "line", ctr, 1));
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "channel_performance_comparison", "Channel Performance Comparison", "bar_line",
            labels, series, "Messages", "CTR %"));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("campaignName", "Campaign")
        .addColumn("channel", "Channel")
        .addColumn("delivered", "Delivered", "number", "right")
        .addColumn("clicks", "Clicks", "number", "right")
        .addColumn("ctrPct", "CTR", "percent", "right")
        .addColumn("revenueGhs", "Revenue (GHS)", "currency", "right")
        .addColumn("status", "Status", "badge");

    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(c.name, ''), 'Untitled') AS campaign_name, "
            "       COALESCE(NULLIF(c.channel, ''), 'unknown') AS channel, "
            "       COALESCE((SELECT COUNT(*) FROM campaign_recipient_events e "
            "                  WHERE e.campaign_id = c.id AND e.event_type = 'delivered'), "
            "                c.reach, 0) AS delivered, "
            "       COALESCE((SELECT COUNT(*) FROM campaign_recipient_events e "
            "                  WHERE e.campaign_id = c.id AND e.event_type = 'clicked'), "
            "                c.clicks, 0) AS clicks, "
            "       COALESCE((SELECT SUM(COALESCE(e.attributed_revenue, 0)::NUMERIC) "
            "                   FROM campaign_recipient_events e "
            "                  WHERE e.campaign_id = c.id AND e.event_type = 'converted'), 0) AS revenue, "
            "       COALESCE(NULLIF(c.status, ''), 'Draft') AS status "
            "FROM campaigns c "
            "WHERE ($1 = '' OR LOWER(c.channel) = $1) "
            "ORDER BY delivered DESC "
            "LIMIT $2 OFFSET $3",
            channelFilter, filter.pageSize, (filter.pageNo - 1) * filter.pageSize);

        for (const auto &row : rows) {
            const double delivered = (double)asLong(row, "delivered");
            const double clicks = (double)asLong(row, "clicks");
            Json::Value item(Json::objectValue);
            item["campaignName"] = asString(row, "campaign_name");
            item["channel"] = asString(row, "channel");
            item["delivered"] = jsonLong((long long)delivered);
            item["clicks"] = jsonLong((long long)clicks);
            item["ctrPct"] = jsonDouble(pct(clicks, delivered));
            item["revenueGhs"] = jsonDouble(asDouble(row, "revenue"));
            item["status"] = ReportResponseBuilder::normalizeBadgeStatus(asString(row, "status"));
            builder.addRow(item);
        }
    }

    co_return builder.buildResult();
}

// ---------------------------------------------------------------------------
// Endpoint 5.2 — POST /api/v1/reports/coupon-usage
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildCouponUsageReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/coupon-usage");

    const std::string statusFilter = filter.status.value_or("");

    // ---- Summary -----------------------------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  COUNT(*) AS redemptions, "
            "  COALESCE(SUM(COALESCE(cu.discount_applied, 0)::NUMERIC), 0) AS discount_granted, "
            "  COALESCE(SUM(COALESCE(p.amount_paid, 0)::NUMERIC), 0) AS net_order_value "
            "FROM coupon_usage cu "
            "LEFT JOIN payments p ON p.id = cu.payment_id "
            "WHERE cu.used_at >= $1::date AND cu.used_at < ($2::date + INTERVAL '1 day')",
            filter.startDate, filter.endDate);

        const auto quotaRows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(SUM(COALESCE(c.usage_quota, 0)), 0) AS max_uses, "
            "       COALESCE(SUM(COALESCE(c.usage_count, 0)), 0) AS used "
            "FROM coupons c");

        const double redemptions = (double)asLong(rows[0], "redemptions");
        const double maxUses = (double)asLong(quotaRows[0], "max_uses");
        const double used = (double)asLong(quotaRows[0], "used");

        builder.addSummary("totalRedemptions", "Total Redemptions", redemptions, "number");
        builder.addSummary("discountGrantedGhs", "Discount Granted (GHS)",
                           asDouble(rows[0], "discount_granted"), "currency");
        builder.addSummary("netOrderValueGhs", "Net Order Value (GHS)",
                           asDouble(rows[0], "net_order_value"), "currency");
        builder.addSummary("redemptionRatePct", "Quota Utilization", pct(used, maxUses), "percent");
    }

    // ---- Chart: top_coupons_bar (stacked_bar) ------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT c.code AS code, "
            "       COALESCE(c.usage_count, 0) AS used, "
            "       GREATEST(COALESCE(c.usage_quota, 0) - COALESCE(c.usage_count, 0), 0) AS remaining "
            "FROM coupons c "
            "ORDER BY used DESC LIMIT 10");

        Json::Value labels = Json::arrayValue;
        Json::Value usedSeries = Json::arrayValue;
        Json::Value remainingSeries = Json::arrayValue;
        for (const auto &row : rows) {
            labels.append(asString(row, "code"));
            usedSeries.append(jsonLong(asLong(row, "used")));
            remainingSeries.append(jsonLong(asLong(row, "remaining")));
        }
        Json::Value series = Json::arrayValue;
        series.append(ReportResponseBuilder::makeSeries("Redeemed", "bar", usedSeries));
        series.append(ReportResponseBuilder::makeSeries("Remaining Quota", "bar", remainingSeries));
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "top_coupons_bar", "Coupon Redemptions vs Quota", "stacked_bar",
            labels, series, "Uses"));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("code", "Code")
        .addColumn("discountRule", "Discount Rule")
        .addColumn("redeemed", "Redeemed", "number", "right")
        .addColumn("maxUses", "Max Uses", "number", "right")
        .addColumn("netRevenueGhs", "Net Revenue (GHS)", "currency", "right")
        .addColumn("status", "Status", "badge");

    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT c.code AS code, "
            "       CASE WHEN COALESCE(c.discount_as_percentage, FALSE) "
            "            THEN COALESCE(c.discount::TEXT, '0') || '% Off' "
            "            ELSE 'GHS ' || COALESCE(c.discount::TEXT, '0') || ' Off' "
            "       END AS discount_rule, "
            "       COALESCE(c.usage_count, 0) AS redeemed, "
            "       COALESCE(c.usage_quota, 0) AS max_uses, "
            "       COALESCE((SELECT SUM(COALESCE(p.amount_paid, 0)::NUMERIC) "
            "                   FROM coupon_usage cu "
            "                   JOIN payments p ON p.id = cu.payment_id "
            "                  WHERE cu.coupon_id = c.id "
            "                    AND cu.used_at >= $1::date "
            "                    AND cu.used_at < ($2::date + INTERVAL '1 day')), 0) AS net_revenue, "
            "       " + statusLabelCase("c.status") + " AS status "
            "FROM coupons c "
            "WHERE ($3 = '' OR ($3 = 'active' AND c.status = 11) "
            "       OR ($3 = 'inactive' AND c.status IN (2, 5, 12, 13)) "
            "       OR ($3 = 'pending' AND c.status IN (0, 6, 10, 14, 20)) "
            "       OR " + statusLabelCase("c.status") + " = $3) "
            "ORDER BY redeemed DESC "
            "LIMIT $4 OFFSET $5",
            filter.startDate, filter.endDate, statusFilter,
            filter.pageSize, (filter.pageNo - 1) * filter.pageSize);

        for (const auto &row : rows) {
            Json::Value item(Json::objectValue);
            item["code"] = asString(row, "code");
            item["discountRule"] = asString(row, "discount_rule");
            item["redeemed"] = jsonLong(asLong(row, "redeemed"));
            item["maxUses"] = jsonLong(asLong(row, "max_uses"));
            item["netRevenueGhs"] = jsonDouble(asDouble(row, "net_revenue"));
            item["status"] = ReportResponseBuilder::normalizeBadgeStatus(asString(row, "status"));
            builder.addRow(item);
        }
    }

    co_return builder.buildResult();
}

// ---------------------------------------------------------------------------
// Endpoint 5.3 — POST /api/v1/reports/affiliate-performance
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildAffiliatePerformanceReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/affiliate-performance");

    // ---- Summary -----------------------------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  (SELECT COUNT(*) FROM affiliates a WHERE a.status = 11) AS active_affiliates, "
            "  (SELECT COUNT(*) FROM affiliate_referral_clicks r "
            "    WHERE r.converted = TRUE "
            "      AND r.clicked_at >= $1::date AND r.clicked_at < ($2::date + INTERVAL '1 day')) "
            "  + (SELECT COUNT(*) FROM affiliate_commissions ac "
            "      WHERE ac.created_at >= $1::date AND ac.created_at < ($2::date + INTERVAL '1 day')) AS conversions, "
            "  (SELECT COALESCE(SUM(COALESCE(ac.amount, 0)::NUMERIC), 0) FROM affiliate_commissions ac "
            "    WHERE ac.created_at >= $1::date AND ac.created_at < ($2::date + INTERVAL '1 day')) AS commissions",
            filter.startDate, filter.endDate);

        const auto revenueRows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(SUM(COALESCE(p.amount_paid, 0)::NUMERIC), 0) AS referred_revenue "
            "FROM payments p "
            "WHERE p.status = 'Success' "
            "  AND p.affiliate_id IS NOT NULL "
            "  AND p.created_at >= $1::date AND p.created_at < ($2::date + INTERVAL '1 day')",
            filter.startDate, filter.endDate);

        builder.addSummary("activeAffiliates", "Active Affiliates",
                           (double)asLong(rows[0], "active_affiliates"), "number");
        builder.addSummary("referralConversions", "Referral Conversions",
                           (double)asLong(rows[0], "conversions"), "number");
        builder.addSummary("referredRevenueGhs", "Referred Revenue (GHS)",
                           asDouble(revenueRows[0], "referred_revenue"), "currency");
        builder.addSummary("commissionsEarnedGhs", "Commissions Earned (GHS)",
                           asDouble(rows[0], "commissions"), "currency");
    }

    // ---- Chart 1: top_affiliates_bar (horizontal_bar) ----------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT TRIM(CONCAT(COALESCE(a.first_name, ''), ' ', COALESCE(a.last_name, ''))) AS affiliate_name, "
            "       COALESCE(SUM(COALESCE(ac.amount, 0)::NUMERIC), 0) AS commissions "
            "FROM affiliates a "
            "LEFT JOIN affiliate_commissions ac ON ac.affiliate_id = a.id "
            "  AND ac.created_at >= $1::date AND ac.created_at < ($2::date + INTERVAL '1 day') "
            "GROUP BY 1 ORDER BY commissions DESC LIMIT 8",
            filter.startDate, filter.endDate);

        Json::Value labels = Json::arrayValue;
        Json::Value data = Json::arrayValue;
        for (const auto &row : rows) {
            labels.append(asString(row, "affiliate_name"));
            data.append(jsonDouble(asDouble(row, "commissions")));
        }
        Json::Value series = Json::arrayValue;
        series.append(ReportResponseBuilder::makeSeries("Commissions (GHS)", "bar", data));
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "top_affiliates_bar", "Top Affiliates by Commission (GHS)", "horizontal_bar",
            labels, series, "GHS", "", false));
    }

    // ---- Chart 2: affiliate_traffic_sources (donut) ------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(r.traffic_source, ''), 'direct') AS source, COUNT(*) AS cnt "
            "FROM affiliate_referral_clicks r "
            "WHERE r.clicked_at >= $1::date AND r.clicked_at < ($2::date + INTERVAL '1 day') "
            "GROUP BY 1 ORDER BY cnt DESC",
            filter.startDate, filter.endDate);

        Json::Value items = Json::arrayValue;
        for (const auto &row : rows) {
            Json::Value item(Json::objectValue);
            item["name"] = asString(row, "source");
            item["value"] = jsonLong(asLong(row, "cnt"));
            items.append(item);
        }
        builder.addChart(ReportResponseBuilder::makeDonutChart(
            "affiliate_traffic_sources", "Referral Traffic Sources", items));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("affiliateName", "Affiliate")
        .addColumn("referralCode", "Referral Code")
        .addColumn("clicks", "Clicks", "number", "right")
        .addColumn("conversions", "Conversions", "number", "right")
        .addColumn("commissionGhs", "Commission (GHS)", "currency", "right")
        .addColumn("status", "Status", "badge");

    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT TRIM(CONCAT(COALESCE(a.first_name, ''), ' ', COALESCE(a.last_name, ''))) AS affiliate_name, "
            "       COALESCE(NULLIF(a.affiliate_id, ''), LEFT(a.id::text, 8)) AS referral_code, "
            "       COALESCE((SELECT COUNT(*) FROM affiliate_referral_clicks r "
            "                  WHERE r.affiliate_id = a.id "
            "                    AND r.clicked_at >= $1::date "
            "                    AND r.clicked_at < ($2::date + INTERVAL '1 day')), 0) AS clicks, "
            "       COALESCE((SELECT COUNT(*) FROM affiliate_referral_clicks r "
            "                  WHERE r.affiliate_id = a.id AND r.converted = TRUE "
            "                    AND r.clicked_at >= $1::date "
            "                    AND r.clicked_at < ($2::date + INTERVAL '1 day')), 0) AS conversions, "
            "       COALESCE((SELECT SUM(COALESCE(ac.amount, 0)::NUMERIC) "
            "                   FROM affiliate_commissions ac "
            "                  WHERE ac.affiliate_id = a.id "
            "                    AND ac.created_at >= $1::date "
            "                    AND ac.created_at < ($2::date + INTERVAL '1 day')), 0) AS commissions, "
            "       " + statusLabelCase("a.status") + " AS status "
            "FROM affiliates a "
            "ORDER BY commissions DESC, clicks DESC "
            "LIMIT $3 OFFSET $4",
            filter.startDate, filter.endDate,
            filter.pageSize, (filter.pageNo - 1) * filter.pageSize);

        for (const auto &row : rows) {
            Json::Value item(Json::objectValue);
            item["affiliateName"] = asString(row, "affiliate_name");
            item["referralCode"] = asString(row, "referral_code");
            item["clicks"] = jsonLong(asLong(row, "clicks"));
            item["conversions"] = jsonLong(asLong(row, "conversions"));
            item["commissionGhs"] = jsonDouble(asDouble(row, "commissions"));
            item["status"] = ReportResponseBuilder::normalizeBadgeStatus(asString(row, "status"));
            builder.addRow(item);
        }
    }

    co_return builder.buildResult();
}

// ---------------------------------------------------------------------------
// Endpoint 5.4 — POST /api/v1/reports/affiliate-payout-reconciliation
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildAffiliatePayoutReconciliationReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/affiliate-payout-reconciliation");

    const std::string statusFilter = filter.status.value_or("");

    // Statutory Ghana withholding tax rate on commission payouts.
    constexpr double kWithholdingRate = 0.075;

    // ---- Summary -----------------------------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  COALESCE(SUM(COALESCE(p.total_amount, 0)::NUMERIC) "
            "           FILTER (WHERE p.status = 19), 0) AS settled, "
            "  COALESCE(SUM(COALESCE(p.total_amount, 0)::NUMERIC) "
            "           FILTER (WHERE p.status <> 19), 0) AS pending, "
            "  COUNT(*) FILTER (WHERE p.status = 19 "
            "                   AND (LOWER(COALESCE(p.account_provider, '')) LIKE '%momo%' "
            "                        OR LOWER(COALESCE(p.account_provider, '')) LIKE '%mtn%' "
            "                        OR LOWER(COALESCE(p.account_type, '')) LIKE '%momo%')) AS momo_settled, "
            "  COUNT(*) FILTER (WHERE LOWER(COALESCE(p.account_provider, '')) LIKE '%momo%' "
            "                   OR LOWER(COALESCE(p.account_provider, '')) LIKE '%mtn%' "
            "                   OR LOWER(COALESCE(p.account_type, '')) LIKE '%momo%') AS momo_total "
            "FROM affiliate_payouts p "
            "WHERE p.created_at >= $1::date AND p.created_at < ($2::date + INTERVAL '1 day')",
            filter.startDate, filter.endDate);

        const double settled = asDouble(rows[0], "settled");
        const double pending = asDouble(rows[0], "pending");

        builder.addSummary("settledPayoutsGhs", "Settled Payouts (GHS)", settled, "currency");
        builder.addSummary("pendingPayoutsGhs", "Pending Payouts (GHS)", pending, "currency");
        builder.addSummary("withholdingTaxGhs", "Withholding Tax (GHS)",
                           round2(settled * kWithholdingRate), "currency");
        builder.addSummary("momoSuccessPct", "MoMo Success Rate",
                           pct((double)asLong(rows[0], "momo_settled"),
                               (double)asLong(rows[0], "momo_total")), "percent");
    }

    // ---- Chart: payout_methods_donut ---------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(p.account_provider, ''), 'Unspecified') AS provider, "
            "       COALESCE(SUM(COALESCE(p.total_amount, 0)::NUMERIC), 0) AS total "
            "FROM affiliate_payouts p "
            "WHERE p.created_at >= $1::date AND p.created_at < ($2::date + INTERVAL '1 day') "
            "GROUP BY 1 ORDER BY total DESC",
            filter.startDate, filter.endDate);

        Json::Value items = Json::arrayValue;
        for (const auto &row : rows) {
            Json::Value item(Json::objectValue);
            item["name"] = asString(row, "provider");
            item["value"] = jsonDouble(asDouble(row, "total"));
            items.append(item);
        }
        builder.addChart(ReportResponseBuilder::makeDonutChart(
            "payout_methods_donut", "Payouts by Rail", items));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("payoutRef", "Payout Ref")
        .addColumn("affiliateName", "Affiliate")
        .addColumn("rail", "Rail")
        .addColumn("grossAmount", "Gross (GHS)", "currency", "right")
        .addColumn("netPaid", "Net Paid (GHS)", "currency", "right")
        .addColumn("status", "Status", "badge");

    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT 'PO-AFF-' || UPPER(LEFT(REPLACE(p.id::text, '-', ''), 6)) AS payout_ref, "
            "       COALESCE(NULLIF(p.affiliate_name, ''), 'Unassigned') AS affiliate_name, "
            "       COALESCE(NULLIF(p.account_provider, ''), COALESCE(NULLIF(p.account_type, ''), 'Unspecified')) AS rail, "
            "       COALESCE(p.total_amount, 0)::NUMERIC AS gross, "
            "       " + statusLabelCase("p.status") + " AS status "
            "FROM affiliate_payouts p "
            "WHERE p.created_at >= $1::date AND p.created_at < ($2::date + INTERVAL '1 day') "
            "  AND ($3 = '' OR ($3 = 'active' AND p.status = 19) "
            "       OR ($3 = 'inactive' AND p.status IN (2, 5, 12, 13, 17)) "
            "       OR ($3 = 'pending' AND p.status NOT IN (2, 5, 12, 13, 17, 19)) "
            "       OR " + statusLabelCase("p.status") + " = $3) "
            "ORDER BY p.created_at DESC "
            "LIMIT $4 OFFSET $5",
            filter.startDate, filter.endDate, statusFilter,
            filter.pageSize, (filter.pageNo - 1) * filter.pageSize);

        for (const auto &row : rows) {
            const double gross = asDouble(row, "gross");
            Json::Value item(Json::objectValue);
            item["payoutRef"] = asString(row, "payout_ref");
            item["affiliateName"] = asString(row, "affiliate_name");
            item["rail"] = asString(row, "rail");
            item["grossAmount"] = jsonDouble(gross);
            item["netPaid"] = jsonDouble(round2(gross * (1.0 - kWithholdingRate)));
            item["status"] = ReportResponseBuilder::normalizeBadgeStatus(asString(row, "status"));
            builder.addRow(item);
        }
    }

    co_return builder.buildResult();
}

// ---------------------------------------------------------------------------
// Endpoint 5.5 — POST /api/v1/reports/push-notification-analytics
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildPushNotificationAnalyticsReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/push-notification-analytics");

    // ---- Summary -----------------------------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  (SELECT COUNT(*) FROM web_push_subscriptions s WHERE s.is_active = TRUE) AS subscribed_browsers, "
            "  (SELECT COALESCE(SUM(COALESCE(b.delivered_count, 0)), 0) FROM push_broadcasts b "
            "    WHERE b.sent_at >= $1::date AND b.sent_at < ($2::date + INTERVAL '1 day')) AS delivered, "
            "  (SELECT COALESCE(AVG(CASE WHEN b.delivered_count > 0 "
            "                             THEN (b.clicked_count::NUMERIC / b.delivered_count) * 100 "
            "                             ELSE 0 END), 0) "
            "     FROM push_broadcasts b "
            "    WHERE b.sent_at >= $1::date AND b.sent_at < ($2::date + INTERVAL '1 day')) AS avg_ctr, "
            "  (SELECT COUNT(*) FROM web_push_subscriptions s "
            "    WHERE s.pruned_at >= $1::date AND s.pruned_at < ($2::date + INTERVAL '1 day')) AS pruned",
            filter.startDate, filter.endDate);

        builder.addSummary("subscribedBrowsers", "Subscribed Browsers",
                           (double)asLong(rows[0], "subscribed_browsers"), "number");
        builder.addSummary("pushDelivered30d", "Push Delivered",
                           (double)asLong(rows[0], "delivered"), "number");
        builder.addSummary("avgPushCtrPct", "Avg Push CTR", asDouble(rows[0], "avg_ctr"), "percent");
        builder.addSummary("prunedStaleEndpoints", "Pruned Stale Endpoints",
                           (double)asLong(rows[0], "pruned"), "number");
    }

    // ---- Chart 1: push_ctr_by_topic (horizontal_bar) -----------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(b.topic, ''), 'general') AS topic, "
            "       COALESCE(AVG(CASE WHEN b.delivered_count > 0 "
            "                         THEN (b.clicked_count::NUMERIC / b.delivered_count) * 100 "
            "                         ELSE 0 END), 0) AS ctr "
            "FROM push_broadcasts b "
            "WHERE b.sent_at >= $1::date AND b.sent_at < ($2::date + INTERVAL '1 day') "
            "GROUP BY 1 ORDER BY ctr DESC",
            filter.startDate, filter.endDate);

        Json::Value labels = Json::arrayValue;
        Json::Value data = Json::arrayValue;
        for (const auto &row : rows) {
            labels.append(asString(row, "topic"));
            data.append(jsonDouble(asDouble(row, "ctr")));
        }
        Json::Value series = Json::arrayValue;
        series.append(ReportResponseBuilder::makeSeries("CTR %", "bar", data));
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "push_ctr_by_topic", "Push CTR by Topic", "horizontal_bar",
            labels, series, "CTR %", "", false));
    }

    // ---- Chart 2: push_browser_split (donut) -------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(s.browser, ''), 'Unknown') AS browser, COUNT(*) AS cnt "
            "FROM web_push_subscriptions s "
            "WHERE s.is_active = TRUE "
            "GROUP BY 1 ORDER BY cnt DESC");

        Json::Value items = Json::arrayValue;
        for (const auto &row : rows) {
            Json::Value item(Json::objectValue);
            item["name"] = asString(row, "browser");
            item["value"] = jsonLong(asLong(row, "cnt"));
            items.append(item);
        }
        builder.addChart(ReportResponseBuilder::makeDonutChart(
            "push_browser_split", "Subscriptions by Browser", items));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("broadcastTitle", "Broadcast")
        .addColumn("topic", "Topic")
        .addColumn("delivered", "Delivered", "number", "right")
        .addColumn("clicked", "Clicked", "number", "right")
        .addColumn("ctrPct", "CTR", "percent", "right")
        .addColumn("status", "Status", "badge");

    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT b.title AS broadcast_title, "
            "       COALESCE(NULLIF(b.topic, ''), 'general') AS topic, "
            "       COALESCE(b.delivered_count, 0) AS delivered, "
            "       COALESCE(b.clicked_count, 0) AS clicked, "
            "       COALESCE(NULLIF(b.status, ''), 'SCHEDULED') AS status "
            "FROM push_broadcasts b "
            "WHERE b.sent_at >= $1::date AND b.sent_at < ($2::date + INTERVAL '1 day') "
            "   OR (b.sent_at IS NULL AND b.created_at >= $1::date "
            "       AND b.created_at < ($2::date + INTERVAL '1 day')) "
            "ORDER BY b.sent_at DESC NULLS LAST, b.created_at DESC "
            "LIMIT $3 OFFSET $4",
            filter.startDate, filter.endDate,
            filter.pageSize, (filter.pageNo - 1) * filter.pageSize);

        for (const auto &row : rows) {
            const double delivered = (double)asLong(row, "delivered");
            const double clicked = (double)asLong(row, "clicked");
            Json::Value item(Json::objectValue);
            item["broadcastTitle"] = asString(row, "broadcast_title");
            item["topic"] = asString(row, "topic");
            item["delivered"] = jsonLong((long long)delivered);
            item["clicked"] = jsonLong((long long)clicked);
            item["ctrPct"] = jsonDouble(pct(clicked, delivered));
            item["status"] = ReportResponseBuilder::normalizeBadgeStatus(asString(row, "status"));
            builder.addRow(item);
        }
    }

    co_return builder.buildResult();
}

} // namespace gnp::services
