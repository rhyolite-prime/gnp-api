//
// ReportingService — Group 1: Revenue & Financial report engines
// (sales_revenue, payment_transactions, subscription_renewals,
//  single_copy_paywall_funnel).
//
// See docs/REPORTS_IMPLEMENTATION_PLAN_AND_API_SPEC.md §2.3.
//

#include "ReportingService.h"

#include <drogon/orm/DbClient.h>
#include <drogon/orm/Result.h>

#include <string>
#include <vector>

#include "ReportSqlHelpers.h"
#include "utils/ReportResponseBuilder.h"

using gnp::dto::ReportFilterDto;
using gnp::utils::ReportResponseBuilder;
using namespace gnp::services::reportdetail;

namespace gnp::services {

// ---------------------------------------------------------------------------
// Endpoint 1.1 — POST /api/v1/reports/sales-revenue
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildSalesRevenueReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/sales-revenue");

    // ---- Summary (current + equal-length prior window) ---------------------
    const auto summaryRows = co_await dbClient->execSqlCoro(
        "WITH pay AS ("
        "  SELECT p.created_at, "
        "         COALESCE(p.gross_amount, p.amount_paid, 0)::NUMERIC AS gross, "
        "         COALESCE(p.net_amount, p.amount_paid, 0)::NUMERIC AS net, "
        "         p.status, p.user_id, p.subscription_plan_id, p.package_name, p.partner_id "
        "  FROM payments p "
        "  WHERE p.status = 'Success' "
        "    AND (p.created_at < ($2::date + INTERVAL '1 day')) "
        "    AND (p.created_at >= $3::date) "
        "    AND ($4 = '' OR EXISTS ("
        "          SELECT 1 FROM subscription_plans sp "
        "          WHERE sp.id = p.subscription_plan_id "
        "            AND (LOWER(sp.name) LIKE '%' || $4 || '%' OR LOWER(sp.plan_type) = $4))) "
        "), inv AS ("
        "  SELECT i.created_at, i.invoice_amount::NUMERIC AS gross, i.invoice_amount::NUMERIC AS net, "
        "         i.status, i.partner_id "
        "  FROM partner_invoices i "
        "  WHERE i.created_at < ($2::date + INTERVAL '1 day') "
        "    AND i.created_at >= $3::date "
        "    AND ($4 = '' OR $4 = 'corporate') "
        "), windowed AS ("
        "  SELECT gross, net, "
        "         (created_at >= $1::date AND created_at < ($2::date + INTERVAL '1 day')) AS in_current, "
        "         (created_at >= $3::date AND created_at < ($1::date)) AS in_prev, "
        "         user_id "
        "  FROM ("
        "    SELECT created_at, gross, net, user_id FROM pay "
        "    UNION ALL "
        "    SELECT created_at, gross, net, NULL::uuid AS user_id FROM inv "
        "  ) u "
        ") "
        "SELECT "
        "  COALESCE(SUM(gross) FILTER (WHERE in_current), 0) AS gross_cur, "
        "  COALESCE(SUM(gross) FILTER (WHERE in_prev), 0) AS gross_prev, "
        "  COALESCE(SUM(net) FILTER (WHERE in_current), 0) AS net_cur, "
        "  COALESCE(SUM(net) FILTER (WHERE in_prev), 0) AS net_prev, "
        "  COUNT(*) FILTER (WHERE in_current AND user_id IS NOT NULL) AS tx_cur, "
        "  COUNT(*) FILTER (WHERE in_prev AND user_id IS NOT NULL) AS tx_prev, "
        "  COUNT(DISTINCT user_id) FILTER (WHERE in_current) AS users_cur "
        "FROM windowed",
        filter.startDate, filter.endDate, filter.prevStartDate,
        filter.plan.value_or(""));

    const double grossCur = asDouble(summaryRows[0], "gross_cur");
    const double grossPrev = asDouble(summaryRows[0], "gross_prev");
    const double netCur = asDouble(summaryRows[0], "net_cur");
    const double netPrev = asDouble(summaryRows[0], "net_prev");
    const long long txCur = asLong(summaryRows[0], "tx_cur");
    const long long txPrev = asLong(summaryRows[0], "tx_prev");
    const long long usersCur = asLong(summaryRows[0], "users_cur");

    const double arpuCur = usersCur > 0 ? grossCur / usersCur : 0.0;
    const double arpuPrev = 0.0; // prior distinct-user count not tracked in the summary window

    builder.addSummary("grossRevenueGhs", "Gross Revenue (GHS)", grossCur, "currency",
                       changePct(grossCur, grossPrev));
    builder.addSummary("netSettledGhs", "Net Settled (GHS)", netCur, "currency",
                       changePct(netCur, netPrev));
    builder.addSummary("paidTransactions", "Paid Transactions", (double)txCur, "number",
                       changePct((double)txCur, (double)txPrev));
    builder.addSummary("arpuGhs", "ARPU (GHS)", arpuCur, "currency", changePct(arpuCur, arpuPrev));

    // ---- Chart 1: revenue_by_stream (stacked_bar, last 10 months) ----------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "WITH months AS ("
            "  SELECT generate_series("
            "           date_trunc('month', ($1::date - INTERVAL '9 months')), "
            "           date_trunc('month', $1::date), "
            "           INTERVAL '1 month')::date AS bucket "
            "), streams AS ("
            "  SELECT date_trunc('month', p.created_at)::date AS bucket, "
            "         CASE "
            "           WHEN p.partner_id IS NOT NULL THEN 'Corporate B2B Invoices' "
            "           WHEN p.newspaper_id IS NOT NULL OR LOWER(COALESCE(p.payment_type, '')) "
            "                LIKE '%single%' OR LOWER(COALESCE(p.payment_type, '')) LIKE '%copy%' "
            "                OR LOWER(COALESCE(p.payment_type, '')) LIKE '%archive%' "
            "             THEN 'Archive Credits & Single Copy' "
            "           ELSE 'Individual Subscriptions' "
            "         END AS stream, "
            "         COALESCE(p.gross_amount, p.amount_paid, 0)::NUMERIC AS gross "
            "  FROM payments p "
            "  WHERE p.status = 'Success' "
            "    AND p.created_at >= ($1::date - INTERVAL '9 months') "
            "    AND p.created_at < ($1::date + INTERVAL '1 day') "
            "  UNION ALL "
            "  SELECT date_trunc('month', i.created_at)::date AS bucket, "
            "         'Corporate B2B Invoices' AS stream, "
            "         i.invoice_amount::NUMERIC AS gross "
            "  FROM partner_invoices i "
            "  WHERE i.created_at >= ($1::date - INTERVAL '9 months') "
            "    AND i.created_at < ($1::date + INTERVAL '1 day') "
            ") "
            "SELECT to_char(m.bucket, 'YYYY-MM-DD') AS bucket, "
            "       COALESCE(s.stream, '') AS stream, "
            "       COALESCE(SUM(s.gross), 0) AS gross "
            "FROM months m "
            "LEFT JOIN streams s ON s.bucket = m.bucket "
            "GROUP BY m.bucket, s.stream "
            "ORDER BY m.bucket ASC",
            filter.endDate);

        Json::Value labels = Json::arrayValue;
        Json::Value individual = Json::arrayValue;
        Json::Value corporate = Json::arrayValue;
        Json::Value archive = Json::arrayValue;

        // Bucket rows: rebuild per month with a small pivot loop.
        struct Bucket {
            std::string label;
            double individual = 0.0;
            double corporate = 0.0;
            double archive = 0.0;
        };
        std::vector<Bucket> buckets;
        for (const auto &row : rows) {
            const std::string bucket = asString(row, "bucket");
            const std::string stream = asString(row, "stream");
            const double gross = asDouble(row, "gross");
            Bucket *target = nullptr;
            for (auto &b : buckets) {
                if (b.label == bucket) {
                    target = &b;
                    break;
                }
            }
            if (target == nullptr) {
                Bucket b;
                b.label = bucket;
                buckets.push_back(b);
                target = &buckets.back();
            }
            if (stream == "Individual Subscriptions") {
                target->individual += gross;
            } else if (stream == "Corporate B2B Invoices") {
                target->corporate += gross;
            } else if (stream == "Archive Credits & Single Copy") {
                target->archive += gross;
            }
        }
        for (const auto &b : buckets) {
            labels.append(monthLabel(b.label));
            individual.append(jsonDouble(b.individual));
            corporate.append(jsonDouble(b.corporate));
            archive.append(jsonDouble(b.archive));
        }

        Json::Value series = Json::arrayValue;
        series.append(ReportResponseBuilder::makeSeries("Individual Subscriptions", "bar", individual));
        series.append(ReportResponseBuilder::makeSeries("Corporate B2B Invoices", "bar", corporate));
        series.append(ReportResponseBuilder::makeSeries("Archive Credits & Single Copy", "bar", archive));
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "revenue_by_stream", "Revenue Stream Trend (Last 10 Months)", "stacked_bar",
            labels, series, "GHS"));
    }

    // ---- Chart 2: revenue_by_plan_donut ------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(sp.name, ''), COALESCE(NULLIF(p.package_name, ''), 'Unassigned')) AS plan_name, "
            "       COALESCE(SUM(COALESCE(p.gross_amount, p.amount_paid, 0)::NUMERIC), 0) AS gross "
            "FROM payments p "
            "LEFT JOIN subscription_plans sp ON sp.id = p.subscription_plan_id "
            "WHERE p.status = 'Success' "
            "  AND p.created_at >= $1::date AND p.created_at < ($2::date + INTERVAL '1 day') "
            "  AND ($3 = '' OR LOWER(sp.name) LIKE '%' || $3 || '%' OR LOWER(sp.plan_type) = $3) "
            "GROUP BY 1 ORDER BY gross DESC",
            filter.startDate, filter.endDate, filter.plan.value_or(""));

        Json::Value items = Json::arrayValue;
        for (const auto &row : rows) {
            Json::Value item(Json::objectValue);
            item["name"] = asString(row, "plan_name");
            item["value"] = jsonDouble(asDouble(row, "gross"));
            items.append(item);
        }
        builder.addChart(ReportResponseBuilder::makeDonutChart(
            "revenue_by_plan_donut", "Revenue Share by Subscription Plan", items));
    }

    // ---- Chart 3: payment_channel_bar (horizontal_bar) ---------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(p.payment_channel, ''), 'Unspecified') AS channel, "
            "       COALESCE(SUM(COALESCE(p.net_amount, p.amount_paid, 0)::NUMERIC), 0) AS settled "
            "FROM payments p "
            "WHERE p.status = 'Success' "
            "  AND p.created_at >= $1::date AND p.created_at < ($2::date + INTERVAL '1 day') "
            "GROUP BY 1 ORDER BY settled DESC",
            filter.startDate, filter.endDate);

        Json::Value labels = Json::arrayValue;
        Json::Value data = Json::arrayValue;
        for (const auto &row : rows) {
            labels.append(asString(row, "channel"));
            data.append(jsonDouble(asDouble(row, "settled")));
        }
        Json::Value series = Json::arrayValue;
        series.append(ReportResponseBuilder::makeSeries("Settled (GHS)", "bar", data));
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "payment_channel_bar", "Collections by Payment Channel (GHS)", "horizontal_bar",
            labels, series, "GHS", "", false));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("date", "Date")
        .addColumn("revenueStream", "Revenue Stream")
        .addColumn("plan", "Plan")
        .addColumn("orders", "Orders", "number", "right")
        .addColumn("grossAmount", "Gross (GHS)", "currency", "right")
        .addColumn("gatewayFees", "Gateway Fees (GHS)", "currency", "right")
        .addColumn("netAmount", "Net (GHS)", "currency", "right")
        .addColumn("status", "Status", "badge");

    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT to_char(p.created_at, 'YYYY-MM-DD') AS date, "
            "       CASE "
            "         WHEN p.partner_id IS NOT NULL THEN 'Corporate B2B Invoices' "
            "         WHEN p.newspaper_id IS NOT NULL OR LOWER(COALESCE(p.payment_type, '')) "
            "              LIKE '%single%' OR LOWER(COALESCE(p.payment_type, '')) LIKE '%copy%' "
            "              OR LOWER(COALESCE(p.payment_type, '')) LIKE '%archive%' "
            "           THEN 'Archive Credits & Single Copy' "
            "         ELSE 'Individual Subscriptions' "
            "       END AS revenue_stream, "
            "       COALESCE(NULLIF(sp.name, ''), COALESCE(NULLIF(p.package_name, ''), 'Unassigned')) AS plan_name, "
            "       COUNT(*) AS orders, "
            "       COALESCE(SUM(COALESCE(p.gross_amount, p.amount_paid, 0)::NUMERIC), 0) AS gross, "
            "       COALESCE(SUM(COALESCE(p.gateway_fee, 0)::NUMERIC), 0) AS fees, "
            "       COALESCE(SUM(COALESCE(p.net_amount, p.amount_paid, 0)::NUMERIC), 0) AS net "
            "FROM payments p "
            "LEFT JOIN subscription_plans sp ON sp.id = p.subscription_plan_id "
            "WHERE p.status = 'Success' "
            "  AND p.created_at >= $1::date AND p.created_at < ($2::date + INTERVAL '1 day') "
            "  AND ($3 = '' OR LOWER(sp.name) LIKE '%' || $3 || '%' OR LOWER(sp.plan_type) = $3) "
            "GROUP BY 1, 2, 3 "
            "ORDER BY 1 DESC, gross DESC",
            filter.startDate, filter.endDate, filter.plan.value_or(""));

        for (const auto &row : rows) {
            Json::Value item(Json::objectValue);
            item["date"] = asString(row, "date");
            item["revenueStream"] = asString(row, "revenue_stream");
            item["plan"] = asString(row, "plan_name");
            item["orders"] = jsonLong(asLong(row, "orders"));
            item["grossAmount"] = jsonDouble(asDouble(row, "gross"));
            item["gatewayFees"] = jsonDouble(asDouble(row, "fees"));
            item["netAmount"] = jsonDouble(asDouble(row, "net"));
            item["status"] = "Settled";
            builder.addRow(item);
        }
    }

    co_return builder.buildResult();
}

// ---------------------------------------------------------------------------
// Endpoint 1.2 — POST /api/v1/reports/payment-transactions
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildPaymentTransactionsReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/payment-transactions");

    // status filter mapping: "active" → Success, "inactive" → Failed,
    // "pending" → Pending/Initiated; raw values match LOWER(status) directly.
    const std::string statusFilter = filter.status.value_or("");

    // ---- Summary -----------------------------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  COALESCE(SUM(COALESCE(p.gross_amount, p.amount_paid, 0)::NUMERIC), 0) AS volume_cur, "
            "  COUNT(*) FILTER (WHERE p.status = 'Success') AS success_cur, "
            "  COUNT(*) FILTER (WHERE p.status = 'Failed') AS failed_cur, "
            "  COUNT(*) FILTER (WHERE p.status IN ('Pending', 'Initiated')) AS pending_cur, "
            "  COUNT(*) AS total_cur "
            "FROM payments p "
            "WHERE p.created_at >= $1::date AND p.created_at < ($2::date + INTERVAL '1 day')",
            filter.startDate, filter.endDate);

        const auto prevRows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  COALESCE(SUM(COALESCE(p.gross_amount, p.amount_paid, 0)::NUMERIC), 0) AS volume_prev, "
            "  COUNT(*) FILTER (WHERE p.status = 'Success') AS success_prev, "
            "  COUNT(*) FILTER (WHERE p.status = 'Failed') AS failed_prev "
            "FROM payments p "
            "WHERE p.created_at >= $1::date AND p.created_at < ($2::date + INTERVAL '1 day')",
            filter.prevStartDate, filter.prevEndDate);

        const double volume = asDouble(rows[0], "volume_cur");
        const long long success = asLong(rows[0], "success_cur");
        const long long failed = asLong(rows[0], "failed_cur");
        const long long total = asLong(rows[0], "total_cur");
        const double successRate = pct((double)success, (double)total);

        const double volumePrev = asDouble(prevRows[0], "volume_prev");
        const long long successPrev = asLong(prevRows[0], "success_prev");
        const long long failedPrev = asLong(prevRows[0], "failed_prev");

        builder.addSummary("totalVolumeGhs", "Total Volume (GHS)", volume, "currency",
                           changePct(volume, volumePrev));
        builder.addSummary("successRatePct", "Success Rate", successRate, "percent",
                           changePct(successRate, pct((double)successPrev, (double)(successPrev + failedPrev))));
        builder.addSummary("successfulCount", "Successful", (double)success, "number",
                           changePct((double)success, (double)successPrev));
        builder.addSummary("failedOrAbandoned", "Failed / Abandoned", (double)(failed + asLong(rows[0], "pending_cur")), "number",
                           changePct((double)(failed + asLong(rows[0], "pending_cur")), (double)failedPrev));
    }

    // ---- Chart 1: daily_gateway_outcomes (bar_line) ------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT to_char(p.created_at, 'YYYY-MM-DD') AS day, "
            "       COUNT(*) FILTER (WHERE p.status = 'Success') AS successful, "
            "       COUNT(*) AS total "
            "FROM payments p "
            "WHERE p.created_at >= $1::date AND p.created_at < ($2::date + INTERVAL '1 day') "
            "GROUP BY 1 ORDER BY 1 ASC",
            filter.startDate, filter.endDate);

        Json::Value labels = Json::arrayValue;
        Json::Value successful = Json::arrayValue;
        Json::Value rates = Json::arrayValue;
        for (const auto &row : rows) {
            labels.append(dayLabel(asString(row, "day")));
            const long long ok = asLong(row, "successful");
            const long long tot = asLong(row, "total");
            successful.append(jsonLong(ok));
            rates.append(jsonDouble(pct((double)ok, (double)tot)));
        }
        Json::Value series = Json::arrayValue;
        series.append(ReportResponseBuilder::makeSeries("Successful", "bar", successful));
        series.append(ReportResponseBuilder::makeSeries("Success Rate %", "line", rates, 1));
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "daily_gateway_outcomes", "Daily Payment Outcomes (Paystack & Hubtel)", "bar_line",
            labels, series, "Transactions", "Success %"));
    }

    // ---- Chart 2: payment_status_donut -------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT p.status AS status, COUNT(*) AS cnt "
            "FROM payments p "
            "WHERE p.created_at >= $1::date AND p.created_at < ($2::date + INTERVAL '1 day') "
            "GROUP BY 1",
            filter.startDate, filter.endDate);

        long long settled = 0;
        long long pending = 0;
        long long failed = 0;
        for (const auto &row : rows) {
            const std::string status = asString(row, "status");
            const long long cnt = asLong(row, "cnt");
            if (status == "Success") {
                settled += cnt;
            } else if (status == "Pending" || status == "Initiated") {
                pending += cnt;
            } else {
                failed += cnt;
            }
        }
        Json::Value items = Json::arrayValue;
        {
            Json::Value item(Json::objectValue);
            item["name"] = "Active / Settled";
            item["value"] = jsonLong(settled);
            items.append(item);
        }
        {
            Json::Value item(Json::objectValue);
            item["name"] = "Pending Webhook";
            item["value"] = jsonLong(pending);
            items.append(item);
        }
        {
            Json::Value item(Json::objectValue);
            item["name"] = "Failed / Declined";
            item["value"] = jsonLong(failed);
            items.append(item);
        }
        builder.addChart(ReportResponseBuilder::makeDonutChart(
            "payment_status_donut", "Payment Outcomes Split", items));
    }

    // ---- Chart 3: failure_reasons_bar (horizontal_bar) ---------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(p.failure_reason, ''), 'Unspecified') AS reason, COUNT(*) AS cnt "
            "FROM payments p "
            "WHERE p.status = 'Failed' "
            "  AND p.created_at >= $1::date AND p.created_at < ($2::date + INTERVAL '1 day') "
            "GROUP BY 1 ORDER BY cnt DESC LIMIT 8",
            filter.startDate, filter.endDate);

        Json::Value labels = Json::arrayValue;
        Json::Value data = Json::arrayValue;
        for (const auto &row : rows) {
            labels.append(asString(row, "reason"));
            data.append(jsonLong(asLong(row, "cnt")));
        }
        Json::Value series = Json::arrayValue;
        series.append(ReportResponseBuilder::makeSeries("Failed Transactions", "bar", data));
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "failure_reasons_bar", "Failure Reasons", "horizontal_bar",
            labels, series, "Transactions", "", false));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("reference", "Reference")
        .addColumn("subscriber", "Subscriber")
        .addColumn("channel", "Channel")
        .addColumn("plan", "Plan")
        .addColumn("amount", "Amount (GHS)", "currency", "right")
        .addColumn("paidAt", "Paid At")
        .addColumn("status", "Status", "badge");

    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(p.transaction_reference, ''), COALESCE(NULLIF(p.receipt_no, ''), p.id::text)) AS reference, "
            "       COALESCE(NULLIF(p.user_name, ''), COALESCE(NULLIF(p.user_email, ''), 'Guest')) AS subscriber, "
            "       COALESCE(NULLIF(p.payment_channel, ''), 'Unspecified') AS channel, "
            "       COALESCE(NULLIF(sp.name, ''), COALESCE(NULLIF(p.package_name, ''), 'Unassigned')) AS plan_name, "
            "       COALESCE(p.amount_paid, 0)::NUMERIC AS amount, "
            "       to_char(p.created_at, 'YYYY-MM-DD HH24:MI') AS paid_at, "
            "       p.status AS status "
            "FROM payments p "
            "LEFT JOIN subscription_plans sp ON sp.id = p.subscription_plan_id "
            "WHERE p.created_at >= $1::date AND p.created_at < ($2::date + INTERVAL '1 day') "
            "  AND ($3 = '' OR LOWER(p.status) = $3 "
            "       OR ($3 = 'active' AND p.status = 'Success') "
            "       OR ($3 = 'inactive' AND p.status = 'Failed') "
            "       OR ($3 = 'pending' AND p.status IN ('Pending', 'Initiated'))) "
            "ORDER BY p.created_at DESC "
            "LIMIT $4 OFFSET $5",
            filter.startDate, filter.endDate, statusFilter,
            filter.pageSize, (filter.pageNo - 1) * filter.pageSize);

        for (const auto &row : rows) {
            Json::Value item(Json::objectValue);
            item["reference"] = asString(row, "reference");
            item["subscriber"] = asString(row, "subscriber");
            item["channel"] = asString(row, "channel");
            item["plan"] = asString(row, "plan_name");
            item["amount"] = jsonDouble(asDouble(row, "amount"));
            item["paidAt"] = asString(row, "paid_at");
            item["status"] = ReportResponseBuilder::normalizeBadgeStatus(asString(row, "status"));
            builder.addRow(item);
        }
    }

    co_return builder.buildResult();
}

// ---------------------------------------------------------------------------
// Endpoint 1.4 — POST /api/v1/reports/subscription-renewals
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildSubscriptionRenewalsReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/subscription-renewals");

    // ---- Summary -----------------------------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  COUNT(*) FILTER (WHERE us.end_date >= $1::date "
            "                   AND us.end_date < ($2::date + INTERVAL '1 day')) AS due_cur, "
            "  COUNT(*) FILTER (WHERE us.end_date >= $1::date "
            "                   AND us.end_date < ($2::date + INTERVAL '1 day') "
            "                   AND us.is_active = TRUE) AS renewed_cur, "
            "  COUNT(*) FILTER (WHERE us.cancelled_at >= $1::date "
            "                   AND us.cancelled_at < ($2::date + INTERVAL '1 day')) AS churned_cur, "
            "  COUNT(*) FILTER (WHERE us.is_active = TRUE) AS active_total "
            "FROM user_subscriptions us",
            filter.startDate, filter.endDate);

        const auto eventRows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  COALESCE(SUM(COALESCE(e.mrr_delta, 0)::NUMERIC) "
            "           FILTER (WHERE e.event_type IN ('renewal', 'upgrade', 'new', 'reactivation', 'signup')), 0) AS mrr_added, "
            "  COALESCE(SUM(COALESCE(e.mrr_delta, 0)::NUMERIC) "
            "           FILTER (WHERE e.event_type IN ('churn', 'downgrade')), 0) AS mrr_lost, "
            "  COUNT(*) FILTER (WHERE e.event_type = 'dunning_recovered') AS dunning_recovered "
            "FROM subscription_lifecycle_events e "
            "WHERE e.occurred_at >= $1::date AND e.occurred_at < ($2::date + INTERVAL '1 day')",
            filter.startDate, filter.endDate);

        const double due = (double)asLong(rows[0], "due_cur");
        const double renewed = (double)asLong(rows[0], "renewed_cur");
        const double churned = (double)asLong(rows[0], "churned_cur");
        const double activeTotal = (double)asLong(rows[0], "active_total");
        const double mrrAdded = asDouble(eventRows[0], "mrr_added");
        const double mrrLost = asDouble(eventRows[0], "mrr_lost");
        const double dunningRecovered = (double)asLong(eventRows[0], "dunning_recovered");

        builder.addSummary("renewalRatePct", "Renewal Rate", pct(renewed, due), "percent");
        builder.addSummary("monthlyChurnPct", "Churn", pct(churned, activeTotal), "percent");
        builder.addSummary("netMrrAddedGhs", "Net MRR Added (GHS)", mrrAdded + mrrLost, "currency");
        builder.addSummary("recoveredDunning", "Dunning Recovered", dunningRecovered, "number");
    }

    // ---- Chart 1: mrr_bridge_trend (bar_line) ------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT to_char(e.occurred_at, 'YYYY-MM-DD') AS day, "
            "       COALESCE(SUM(COALESCE(e.mrr_delta, 0)::NUMERIC) "
            "         FILTER (WHERE e.event_type IN ('renewal', 'upgrade', 'new', 'reactivation', 'signup')), 0) AS added, "
            "       COALESCE(SUM(COALESCE(e.mrr_delta, 0)::NUMERIC) "
            "         FILTER (WHERE e.event_type IN ('churn', 'downgrade')), 0) AS lost "
            "FROM subscription_lifecycle_events e "
            "WHERE e.occurred_at >= $1::date AND e.occurred_at < ($2::date + INTERVAL '1 day') "
            "GROUP BY 1 ORDER BY 1 ASC",
            filter.startDate, filter.endDate);

        Json::Value labels = Json::arrayValue;
        Json::Value added = Json::arrayValue;
        Json::Value lost = Json::arrayValue;
        for (const auto &row : rows) {
            labels.append(dayLabel(asString(row, "day")));
            added.append(jsonDouble(asDouble(row, "added")));
            lost.append(jsonDouble(asDouble(row, "lost")));
        }
        Json::Value series = Json::arrayValue;
        series.append(ReportResponseBuilder::makeSeries("MRR Added (GHS)", "bar", added));
        series.append(ReportResponseBuilder::makeSeries("MRR Churned (GHS)", "line", lost, 1));
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "mrr_bridge_trend", "MRR Bridge — Adds vs Churn", "bar_line",
            labels, series, "GHS", "Churned (GHS)"));
    }

    // ---- Chart 2: churn_reasons_donut --------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(us.churn_reason, ''), COALESCE(NULLIF(us.cancellation_type, ''), 'Unspecified')) AS reason, "
            "       COUNT(*) AS cnt "
            "FROM user_subscriptions us "
            "WHERE us.cancelled_at >= $1::date AND us.cancelled_at < ($2::date + INTERVAL '1 day') "
            "GROUP BY 1 ORDER BY cnt DESC",
            filter.startDate, filter.endDate);

        Json::Value items = Json::arrayValue;
        for (const auto &row : rows) {
            Json::Value item(Json::objectValue);
            item["name"] = asString(row, "reason");
            item["value"] = jsonLong(asLong(row, "cnt"));
            items.append(item);
        }
        builder.addChart(ReportResponseBuilder::makeDonutChart(
            "churn_reasons_donut", "Churn Reasons", items));
    }

    // ---- Chart 3: renewal_by_plan_bar (horizontal_bar) ---------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(sp.name, ''), 'Unassigned') AS plan_name, "
            "       COUNT(*) FILTER (WHERE us.end_date >= $1::date "
            "                        AND us.end_date < ($2::date + INTERVAL '1 day')) AS due, "
            "       COUNT(*) FILTER (WHERE us.end_date >= $1::date "
            "                        AND us.end_date < ($2::date + INTERVAL '1 day') "
            "                        AND us.is_active = TRUE) AS renewed "
            "FROM user_subscriptions us "
            "LEFT JOIN subscription_plans sp ON sp.id = us.subscription_plan_id "
            "GROUP BY 1 ORDER BY renewed DESC",
            filter.startDate, filter.endDate);

        Json::Value labels = Json::arrayValue;
        Json::Value data = Json::arrayValue;
        for (const auto &row : rows) {
            labels.append(asString(row, "plan_name"));
            data.append(jsonLong(asLong(row, "renewed")));
        }
        Json::Value series = Json::arrayValue;
        series.append(ReportResponseBuilder::makeSeries("Renewed", "bar", data));
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "renewal_by_plan_bar", "Renewals by Plan", "horizontal_bar",
            labels, series, "Subscriptions", "", false));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("planName", "Plan")
        .addColumn("dueForRenewal", "Due", "number", "right")
        .addColumn("renewedCount", "Renewed", "number", "right")
        .addColumn("dunningRecovered", "Dunning Recovered", "number", "right")
        .addColumn("churnedCount", "Churned", "number", "right")
        .addColumn("renewalRate", "Renewal Rate", "percent", "right")
        .addColumn("retainedMrr", "Retained MRR (GHS)", "currency", "right");

    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(sp.name, ''), 'Unassigned') AS plan_name, "
            "       COUNT(*) FILTER (WHERE us.end_date >= $1::date "
            "                        AND us.end_date < ($2::date + INTERVAL '1 day')) AS due, "
            "       COUNT(*) FILTER (WHERE us.end_date >= $1::date "
            "                        AND us.end_date < ($2::date + INTERVAL '1 day') "
            "                        AND us.is_active = TRUE) AS renewed, "
            "       COUNT(*) FILTER (WHERE us.cancelled_at >= $1::date "
            "                        AND us.cancelled_at < ($2::date + INTERVAL '1 day')) AS churned, "
            "       COALESCE(SUM(COALESCE(us.plan_fee, 0)::NUMERIC) "
            "         FILTER (WHERE us.is_active = TRUE), 0) AS retained_mrr "
            "FROM user_subscriptions us "
            "LEFT JOIN subscription_plans sp ON sp.id = us.subscription_plan_id "
            "GROUP BY 1 ORDER BY retained_mrr DESC",
            filter.startDate, filter.endDate);

        for (const auto &row : rows) {
            const double due = (double)asLong(row, "due");
            const double renewed = (double)asLong(row, "renewed");
            Json::Value item(Json::objectValue);
            item["planName"] = asString(row, "plan_name");
            item["dueForRenewal"] = jsonLong(asLong(row, "due"));
            item["renewedCount"] = jsonLong(asLong(row, "renewed"));
            item["dunningRecovered"] = jsonLong(0);
            item["churnedCount"] = jsonLong(asLong(row, "churned"));
            item["renewalRate"] = jsonDouble(pct(renewed, due));
            item["retainedMrr"] = jsonDouble(asDouble(row, "retained_mrr"));
            builder.addRow(item);
        }
    }

    co_return builder.buildResult();
}

// ---------------------------------------------------------------------------
// Endpoint 1.5 — POST /api/v1/reports/single-copy-paywall-funnel
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildSingleCopyPaywallFunnelReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/single-copy-paywall-funnel");

    // ---- Summary -----------------------------------------------------------
    long long paywallViews = 0;
    long long copiesSold = 0;
    long long upsoldToPlan = 0;
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  (SELECT COUNT(*) FROM newspaper_engagement "
            "    WHERE viewed_at >= $1::date AND viewed_at < ($2::date + INTERVAL '1 day')) AS views_cur, "
            "  (SELECT COUNT(*) FROM edition_sales_ledger "
            "    WHERE sold_at >= $1::date AND sold_at < ($2::date + INTERVAL '1 day')) AS sold_cur, "
            "  (SELECT COUNT(DISTINCT l.user_id) "
            "     FROM edition_sales_ledger l "
            "     JOIN payments p ON p.user_id = l.user_id "
            "    WHERE l.sold_at >= $1::date AND l.sold_at < ($2::date + INTERVAL '1 day') "
            "      AND p.status = 'Success' "
            "      AND (LOWER(COALESCE(p.payment_type, '')) LIKE '%subscription%' "
            "           OR p.subscription_plan_id IS NOT NULL) "
            "      AND p.created_at >= $1::date AND p.created_at < ($2::date + INTERVAL '1 day')) AS upsold_cur",
            filter.startDate, filter.endDate);

        paywallViews = asLong(rows[0], "views_cur");
        copiesSold = asLong(rows[0], "sold_cur");
        upsoldToPlan = asLong(rows[0], "upsold_cur");
    }

    builder.addSummary("paywallViews", "Paywall Views", (double)paywallViews, "number");
    builder.addSummary("singleCopiesSold", "Single Copies Sold", (double)copiesSold, "number");
    builder.addSummary("conversionRatePct", "Paywall Conversion", pct((double)copiesSold, (double)paywallViews), "percent");
    builder.addSummary("upsoldToPlan", "Upsold to Plan", (double)upsoldToPlan, "number");

    // ---- Chart 1: paywall_conversion_funnel (5 stages) ---------------------
    {
        long long attempts = 0;
        long long prompted = 0;
        {
            const auto rows = co_await dbClient->execSqlCoro(
                "SELECT "
                "  COUNT(*) AS attempts, "
                "  COUNT(*) FILTER (WHERE pa.status IN ('Pending', 'Success', 'Failed')) AS prompted "
                "FROM purchase_attempts pa "
                "WHERE pa.created_at >= $1::date AND pa.created_at < ($2::date + INTERVAL '1 day')",
                filter.startDate, filter.endDate);
            attempts = asLong(rows[0], "attempts");
            prompted = asLong(rows[0], "prompted");
        }

        Json::Value items = Json::arrayValue;
        auto addStage = [&items](const std::string &name, long long value) {
            Json::Value item(Json::objectValue);
            item["name"] = name;
            item["value"] = jsonLong(value);
            items.append(item);
        };
        addStage("1. Preview Page Opened", paywallViews);
        addStage("2. Paywall Modal Triggered", attempts);
        addStage("3. Payment Prompt Initiated", prompted);
        addStage("4. Single Copy Purchased", copiesSold);
        addStage("5. Upgraded to Subscription Plan", upsoldToPlan);
        builder.addChart(ReportResponseBuilder::makeFunnelChart(
            "paywall_conversion_funnel", "Edition Paywall Conversion Funnel", items));
    }

    // ---- Chart 2: single_copy_by_publication (horizontal_bar) --------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(pub.name, ''), 'Unassigned') AS publication, "
            "       COUNT(*) AS sold "
            "FROM edition_sales_ledger l "
            "JOIN publications pub ON pub.id = l.publication_id "
            "WHERE l.sold_at >= $1::date AND l.sold_at < ($2::date + INTERVAL '1 day') "
            "GROUP BY 1 ORDER BY sold DESC LIMIT 10",
            filter.startDate, filter.endDate);

        Json::Value labels = Json::arrayValue;
        Json::Value data = Json::arrayValue;
        for (const auto &row : rows) {
            labels.append(asString(row, "publication"));
            data.append(jsonLong(asLong(row, "sold")));
        }
        Json::Value series = Json::arrayValue;
        series.append(ReportResponseBuilder::makeSeries("Copies Sold", "bar", data));
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "single_copy_by_publication", "Single Copy Sales by Publication", "horizontal_bar",
            labels, series, "Copies", "", false));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("publication", "Publication")
        .addColumn("issueDate", "Issue Date")
        .addColumn("previewReaders", "Preview Readers", "number", "right")
        .addColumn("copiesSold", "Copies Sold", "number", "right")
        .addColumn("convRate", "Conv. Rate", "percent", "right")
        .addColumn("revenueGhs", "Revenue (GHS)", "currency", "right");

    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(pub.name, ''), 'Unassigned') AS publication, "
            "       to_char(n.publication_date, 'YYYY-MM-DD') AS issue_date, "
            "       COALESCE((SELECT COUNT(*) FROM newspaper_engagement ne "
            "                  WHERE ne.newspaper_id = n.id "
            "                    AND ne.viewed_at >= $1::date "
            "                    AND ne.viewed_at < ($2::date + INTERVAL '1 day')), 0) AS preview_readers, "
            "       COALESCE((SELECT COUNT(*) FROM edition_sales_ledger l "
            "                  WHERE l.newspaper_id = n.id "
            "                    AND l.sold_at >= $1::date "
            "                    AND l.sold_at < ($2::date + INTERVAL '1 day')), 0) AS copies_sold, "
            "       COALESCE((SELECT SUM(l.amount_paid::NUMERIC) FROM edition_sales_ledger l "
            "                  WHERE l.newspaper_id = n.id "
            "                    AND l.sold_at >= $1::date "
            "                    AND l.sold_at < ($2::date + INTERVAL '1 day')), 0) AS revenue "
            "FROM newspapers n "
            "JOIN publications pub ON pub.id = n.publication_id "
            "WHERE EXISTS (SELECT 1 FROM edition_sales_ledger l "
            "               WHERE l.newspaper_id = n.id "
            "                 AND l.sold_at >= $1::date "
            "                 AND l.sold_at < ($2::date + INTERVAL '1 day')) "
            "ORDER BY revenue DESC "
            "LIMIT $3 OFFSET $4",
            filter.startDate, filter.endDate,
            filter.pageSize, (filter.pageNo - 1) * filter.pageSize);

        for (const auto &row : rows) {
            const double readers = (double)asLong(row, "preview_readers");
            const double sold = (double)asLong(row, "copies_sold");
            Json::Value item(Json::objectValue);
            item["publication"] = asString(row, "publication");
            item["issueDate"] = asString(row, "issue_date");
            item["previewReaders"] = jsonLong(asLong(row, "preview_readers"));
            item["copiesSold"] = jsonLong(asLong(row, "copies_sold"));
            item["convRate"] = jsonDouble(pct(sold, readers));
            item["revenueGhs"] = jsonDouble(asDouble(row, "revenue"));
            builder.addRow(item);
        }
    }

    co_return builder.buildResult();
}

} // namespace gnp::services
