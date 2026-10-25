//
// ReportingService — Group 4: Commercial Partner B2B report engines
// (partner_engagement, partner_quota, partner_api_usage).
//
// See docs/REPORTS_IMPLEMENTATION_PLAN_AND_API_SPEC.md §2.6.
//

#include "ReportingService.h"

#include <drogon/orm/DbClient.h>
#include <drogon/orm/Result.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "ReportSqlHelpers.h"
#include "utils/ReportResponseBuilder.h"

using gnp::dto::ReportFilterDto;
using gnp::utils::ReportResponseBuilder;
using namespace gnp::services::reportdetail;

namespace gnp::services {

// ---------------------------------------------------------------------------
// Endpoint 4.1 — POST /api/v1/reports/partner-engagement
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildPartnerEngagementReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/partner-engagement");

    // ---- Summary -----------------------------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  (SELECT COUNT(*) FROM users u "
            "    WHERE u.partner_id IS NOT NULL AND u.is_active = TRUE) AS active_readers, "
            "  (SELECT COUNT(*) FROM publication_reads pr "
            "    WHERE pr.read_at >= $1::date AND pr.read_at < ($2::date + INTERVAL '1 day')) "
            "  + (SELECT COUNT(*) FROM newspaper_engagement ne "
            "      WHERE ne.partner_id IS NOT NULL "
            "        AND ne.viewed_at >= $1::date AND ne.viewed_at < ($2::date + INTERVAL '1 day')) AS total_reads, "
            "  (SELECT COALESCE(cp.subscriber_quota, 0) FROM commercial_partners cp LIMIT 1) AS unused_quota",
            filter.startDate, filter.endDate);

        const double activeReaders = (double)asLong(rows[0], "active_readers");
        const double totalReads = (double)asLong(rows[0], "total_reads");

        const auto seatRows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(SUM(COALESCE(cp.subscriber_quota, 0)), 0) AS quota, "
            "       COALESCE((SELECT COUNT(*) FROM users u "
            "                  WHERE u.partner_id IS NOT NULL AND u.is_active = TRUE), 0) AS enrolled "
            "FROM commercial_partners cp");
        const double quota = (double)asLong(seatRows[0], "quota");
        const double enrolled = (double)asLong(seatRows[0], "enrolled");

        builder.addSummary("activeCorporateReaders", "Active Corporate Readers", activeReaders, "number");
        builder.addSummary("totalPartnerReads", "Partner Edition Reads", totalReads, "number");
        builder.addSummary("avgReadsPerSeat", "Avg Reads / Seat",
                           enrolled > 0 ? round2(totalReads / enrolled) : 0.0, "number");
        builder.addSummary("seatActivationPct", "Seat Activation", pct(enrolled, quota), "percent");
    }

    // ---- Chart: partner_reads_trend (stacked_bar) --------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "WITH months AS ("
            "  SELECT generate_series(date_trunc('month', $1::date), "
            "                         date_trunc('month', $2::date), "
            "                         INTERVAL '1 month')::date AS bucket "
            ") "
            "SELECT to_char(m.bucket, 'YYYY-MM-DD') AS bucket, "
            "       COALESCE(NULLIF(cp.name, ''), 'Unassigned') AS partner_name, "
            "       COUNT(pr.id) AS reads "
            "FROM months m "
            "LEFT JOIN publication_reads pr "
            "  ON date_trunc('month', pr.read_at) = m.bucket "
            "LEFT JOIN commercial_partners cp ON cp.id = pr.partner_id "
            "GROUP BY 1, 2 ORDER BY 1 ASC, reads DESC",
            filter.startDate, filter.endDate);

        // Pivot partner x month into a stacked_bar series per partner (top 5).
        std::vector<std::string> monthOrder;
        std::vector<std::string> partnerOrder;
        std::map<std::string, std::map<std::string, long long>> pivot; // partner -> month -> reads
        for (const auto &row : rows) {
            const std::string month = asString(row, "bucket");
            const std::string partner = asString(row, "partner_name");
            const long long reads = asLong(row, "reads");
            bool seen = false;
            for (const auto &m : monthOrder) {
                if (m == month) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                monthOrder.push_back(month);
            }
            pivot[partner][month] += reads;
        }
        for (const auto &kv : pivot) {
            partnerOrder.push_back(kv.first);
        }
        // Keep the top 5 partners by total reads.
        std::sort(partnerOrder.begin(), partnerOrder.end(), [&pivot](const std::string &a, const std::string &b) {
            long long sumA = 0;
            long long sumB = 0;
            for (const auto &kv : pivot.at(a)) {
                sumA += kv.second;
            }
            for (const auto &kv : pivot.at(b)) {
                sumB += kv.second;
            }
            return sumA > sumB;
        });
        if (partnerOrder.size() > 5) {
            partnerOrder.resize(5);
        }

        Json::Value labels = Json::arrayValue;
        for (const auto &month : monthOrder) {
            labels.append(monthLabel(month));
        }
        Json::Value series = Json::arrayValue;
        for (const auto &partner : partnerOrder) {
            Json::Value data = Json::arrayValue;
            for (const auto &month : monthOrder) {
                data.append(jsonLong(pivot[partner][month]));
            }
            series.append(ReportResponseBuilder::makeSeries(partner, "bar", data));
        }
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "partner_reads_trend", "Partner Edition Reads by Month", "stacked_bar",
            labels, series, "Reads"));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("partnerName", "Partner")
        .addColumn("enrolledSeats", "Enrolled Seats", "number", "right")
        .addColumn("monthlyActiveReaders", "Monthly Active", "number", "right")
        .addColumn("editionReads", "Edition Reads", "number", "right")
        .addColumn("topPublication", "Top Publication")
        .addColumn("status", "Status", "badge");

    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT cp.name AS partner_name, "
            "       COALESCE((SELECT COUNT(*) FROM users u "
            "                  WHERE u.partner_id = cp.id AND u.is_active = TRUE), 0) AS enrolled, "
            "       COALESCE((SELECT COUNT(DISTINCT pr.user_id) FROM publication_reads pr "
            "                  WHERE pr.partner_id = cp.id "
            "                    AND pr.read_at >= $1::date "
            "                    AND pr.read_at < ($2::date + INTERVAL '1 day')), 0) AS active_readers, "
            "       COALESCE((SELECT COUNT(*) FROM publication_reads pr "
            "                  WHERE pr.partner_id = cp.id "
            "                    AND pr.read_at >= $1::date "
            "                    AND pr.read_at < ($2::date + INTERVAL '1 day')), 0) "
            "     + COALESCE((SELECT COUNT(*) FROM newspaper_engagement ne "
            "                   WHERE ne.partner_id = cp.id "
            "                     AND ne.viewed_at >= $1::date "
            "                     AND ne.viewed_at < ($2::date + INTERVAL '1 day')), 0) AS edition_reads, "
            "       COALESCE((SELECT pub.name FROM publication_reads pr "
            "                   JOIN publications pub ON pub.id = pr.publication_id "
            "                  WHERE pr.partner_id = cp.id "
            "                    AND pr.read_at >= $1::date "
            "                    AND pr.read_at < ($2::date + INTERVAL '1 day') "
            "                  GROUP BY pub.name ORDER BY COUNT(*) DESC LIMIT 1), '—') AS top_publication, "
            "       COALESCE(cp.status, 'Active') AS status "
            "FROM commercial_partners cp "
            "ORDER BY edition_reads DESC",
            filter.startDate, filter.endDate);

        for (const auto &row : rows) {
            Json::Value item(Json::objectValue);
            item["partnerName"] = asString(row, "partner_name");
            item["enrolledSeats"] = jsonLong(asLong(row, "enrolled"));
            item["monthlyActiveReaders"] = jsonLong(asLong(row, "active_readers"));
            item["editionReads"] = jsonLong(asLong(row, "edition_reads"));
            item["topPublication"] = asString(row, "top_publication");
            item["status"] = ReportResponseBuilder::normalizeBadgeStatus(asString(row, "status"));
            builder.addRow(item);
        }
    }

    co_return builder.buildResult();
}

// ---------------------------------------------------------------------------
// Endpoint 4.2 — POST /api/v1/reports/partner-quota
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildPartnerQuotaReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/partner-quota");

    // ---- Summary -----------------------------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(SUM(COALESCE(cp.subscriber_quota, 0)), 0) AS allocated, "
            "       COALESCE(SUM(LEAST(COALESCE(cp.subscriber_quota, 0), "
            "           (SELECT COUNT(*) FROM users u WHERE u.partner_id = cp.id AND u.is_active = TRUE))), 0) AS enrolled, "
            "       COALESCE(SUM(GREATEST(COALESCE(cp.subscriber_quota, 0) "
            "           - (SELECT COUNT(*) FROM users u WHERE u.partner_id = cp.id AND u.is_active = TRUE), 0)), 0) AS remaining "
            "FROM commercial_partners cp");

        const double allocated = (double)asLong(rows[0], "allocated");
        const double enrolled = (double)asLong(rows[0], "enrolled");
        const double remaining = (double)asLong(rows[0], "remaining");

        builder.addSummary("totalAllocatedSeats", "Allocated Seats", allocated, "number");
        builder.addSummary("totalEnrolledSeats", "Enrolled Seats", enrolled, "number");
        builder.addSummary("remainingQuota", "Remaining Quota", remaining, "number");
        builder.addSummary("overallUtilizationPct", "Utilization", pct(enrolled, allocated), "percent");
    }

    // ---- Chart: partner_quota_bars (stacked_bar) ---------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT cp.name AS partner_name, "
            "       COALESCE(cp.subscriber_quota, 0) AS quota, "
            "       COALESCE((SELECT COUNT(*) FROM users u "
            "                  WHERE u.partner_id = cp.id AND u.is_active = TRUE), 0) AS enrolled "
            "FROM commercial_partners cp "
            "ORDER BY cp.name ASC");

        Json::Value labels = Json::arrayValue;
        Json::Value enrolledSeries = Json::arrayValue;
        Json::Value remainingSeries = Json::arrayValue;
        for (const auto &row : rows) {
            labels.append(asString(row, "partner_name"));
            const long long quota = asLong(row, "quota");
            const long long enrolled = asLong(row, "enrolled");
            enrolledSeries.append(jsonLong(enrolled));
            remainingSeries.append(jsonLong(quota > enrolled ? quota - enrolled : 0));
        }
        Json::Value series = Json::arrayValue;
        series.append(ReportResponseBuilder::makeSeries("Enrolled Seats", "bar", enrolledSeries));
        series.append(ReportResponseBuilder::makeSeries("Remaining Quota", "bar", remainingSeries));
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "partner_quota_bars", "Seat Allocation vs Utilization", "stacked_bar",
            labels, series, "Seats"));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("partnerName", "Partner")
        .addColumn("assignedQuota", "Quota", "number", "right")
        .addColumn("enrolledSeats", "Enrolled", "number", "right")
        .addColumn("remainingSeats", "Remaining", "number", "right")
        .addColumn("utilizationPct", "Utilization", "percent", "right")
        .addColumn("status", "Status", "badge");

    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT cp.name AS partner_name, "
            "       COALESCE(cp.subscriber_quota, 0) AS quota, "
            "       COALESCE((SELECT COUNT(*) FROM users u "
            "                  WHERE u.partner_id = cp.id AND u.is_active = TRUE), 0) AS enrolled, "
            "       COALESCE(cp.status, 'Active') AS status "
            "FROM commercial_partners cp "
            "ORDER BY cp.subscriber_quota DESC NULLS LAST");

        for (const auto &row : rows) {
            const double quota = (double)asLong(row, "quota");
            const double enrolled = (double)asLong(row, "enrolled");
            Json::Value item(Json::objectValue);
            item["partnerName"] = asString(row, "partner_name");
            item["assignedQuota"] = jsonLong((long long)quota);
            item["enrolledSeats"] = jsonLong((long long)enrolled);
            item["remainingSeats"] = jsonLong((long long)(quota > enrolled ? quota - enrolled : 0));
            item["utilizationPct"] = jsonDouble(pct(enrolled, quota));
            item["status"] = ReportResponseBuilder::normalizeBadgeStatus(asString(row, "status"));
            builder.addRow(item);
        }
    }

    co_return builder.buildResult();
}

// ---------------------------------------------------------------------------
// Endpoint 4.3 — POST /api/v1/reports/partner-api-usage
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildPartnerApiUsageReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/partner-api-usage");

    // ---- Summary (including PERCENTILE_CONT(0.95) p95 latency) ------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  COUNT(*) AS total_requests, "
            "  COUNT(*) FILTER (WHERE l.is_successful = TRUE) AS successful, "
            "  COALESCE(PERCENTILE_CONT(0.95) WITHIN GROUP (ORDER BY l.response_time_ms), 0) AS p95_latency, "
            "  COUNT(*) FILTER (WHERE l.response_status_code = 429) AS rate_limited "
            "FROM partner_api_request_logs l "
            "WHERE l.created_at >= $1::date AND l.created_at < ($2::date + INTERVAL '1 day')",
            filter.startDate, filter.endDate);

        const double total = (double)asLong(rows[0], "total_requests");
        const double successful = (double)asLong(rows[0], "successful");

        builder.addSummary("totalApiRequests", "API Requests", total, "number");
        builder.addSummary("apiSuccessRatePct", "Success Rate", pct(successful, total), "percent");
        builder.addSummary("p95LatencyMs", "p95 Latency (ms)", asDouble(rows[0], "p95_latency"), "number");
        builder.addSummary("rateLimitedCount", "Rate Limited", (double)asLong(rows[0], "rate_limited"), "number");
    }

    // ---- Chart: api_calls_and_latency (bar_line) ---------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT to_char(l.created_at, 'YYYY-MM-DD') AS day, "
            "       COUNT(*) AS requests, "
            "       COALESCE(AVG(l.response_time_ms), 0) AS avg_latency "
            "FROM partner_api_request_logs l "
            "WHERE l.created_at >= $1::date AND l.created_at < ($2::date + INTERVAL '1 day') "
            "GROUP BY 1 ORDER BY 1 ASC",
            filter.startDate, filter.endDate);

        Json::Value labels = Json::arrayValue;
        Json::Value requests = Json::arrayValue;
        Json::Value latency = Json::arrayValue;
        for (const auto &row : rows) {
            labels.append(dayLabel(asString(row, "day")));
            requests.append(jsonLong(asLong(row, "requests")));
            latency.append(jsonDouble(asDouble(row, "avg_latency")));
        }
        Json::Value series = Json::arrayValue;
        series.append(ReportResponseBuilder::makeSeries("API Requests", "bar", requests));
        series.append(ReportResponseBuilder::makeSeries("Avg Latency (ms)", "line", latency, 1));
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "api_calls_and_latency", "API Calls & Latency", "bar_line",
            labels, series, "Requests", "ms"));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("partnerName", "Partner")
        .addColumn("endpointRoute", "Endpoint")
        .addColumn("requests30d", "Requests", "number", "right")
        .addColumn("avgLatencyMs", "Avg Latency (ms)", "number", "right")
        .addColumn("errorRatePct", "Error Rate", "percent", "right")
        .addColumn("status", "Status", "badge");

    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(cp.name, ''), 'Unassigned') AS partner_name, "
            "       TRIM(CONCAT(COALESCE(l.method, ''), ' ', COALESCE(l.endpoint, ''))) AS endpoint_route, "
            "       COUNT(*) AS requests, "
            "       COALESCE(AVG(l.response_time_ms), 0) AS avg_latency, "
            "       COUNT(*) FILTER (WHERE l.is_successful = FALSE) AS errors "
            "FROM partner_api_request_logs l "
            "LEFT JOIN commercial_partners cp ON cp.id = l.partner_id "
            "WHERE l.created_at >= $1::date AND l.created_at < ($2::date + INTERVAL '1 day') "
            "GROUP BY 1, 2 "
            "ORDER BY requests DESC "
            "LIMIT $3 OFFSET $4",
            filter.startDate, filter.endDate,
            filter.pageSize, (filter.pageNo - 1) * filter.pageSize);

        for (const auto &row : rows) {
            const double requests = (double)asLong(row, "requests");
            const double errors = (double)asLong(row, "errors");
            Json::Value item(Json::objectValue);
            item["partnerName"] = asString(row, "partner_name");
            item["endpointRoute"] = asString(row, "endpoint_route");
            item["requests30d"] = jsonLong((long long)requests);
            item["avgLatencyMs"] = jsonDouble(asDouble(row, "avg_latency"));
            item["errorRatePct"] = jsonDouble(pct(errors, requests));
            item["status"] = "Active";
            builder.addRow(item);
        }
    }

    co_return builder.buildResult();
}

} // namespace gnp::services
