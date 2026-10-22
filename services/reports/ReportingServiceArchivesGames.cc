//
// ReportingService — Groups 6 & 7: Historical Archives, AI RAG and Games
// (archive_rag_intelligence, institutional_archive_credits,
//  games_engagement_retention).
//
// See docs/REPORTS_IMPLEMENTATION_PLAN_AND_API_SPEC.md §2.8 / §2.9.
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
// Endpoint 6.1 — POST /api/v1/reports/archive-rag-intelligence
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildArchiveRagIntelligenceReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/archive-rag-intelligence");

    // ---- Summary -----------------------------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  COUNT(*) FILTER (WHERE a.query_mode IN ('keyword', 'search')) AS searches, "
            "  COALESCE(SUM(COALESCE(a.citations_returned, 0)) "
            "           FILTER (WHERE a.query_mode = 'rag'), 0) AS citations_served, "
            "  COALESCE(AVG(a.avg_similarity) FILTER (WHERE a.avg_similarity IS NOT NULL), 0) "
            "       * 100 AS avg_similarity_pct, "
            "  COUNT(*) FILTER (WHERE COALESCE(a.results_count, 0) = 0) AS zero_results, "
            "  COUNT(*) AS total_queries "
            "FROM archive_search_and_rag_logs a "
            "WHERE a.created_at >= $1::date AND a.created_at < ($2::date + INTERVAL '1 day')",
            filter.startDate, filter.endDate);

        const double zeroResults = (double)asLong(rows[0], "zero_results");
        const double total = (double)asLong(rows[0], "total_queries");

        builder.addSummary("archiveSearches", "Archive Searches",
                           (double)asLong(rows[0], "searches"), "number");
        builder.addSummary("ragCitationsServed", "RAG Citations Served",
                           (double)asLong(rows[0], "citations_served"), "number");
        builder.addSummary("avgVectorSimilarity", "Avg Vector Similarity",
                           asDouble(rows[0], "avg_similarity_pct"), "percent");
        builder.addSummary("zeroResultRatePct", "Zero-Result Rate",
                           pct(zeroResults, total), "percent");
    }

    // ---- Chart: archive_decade_demand (bar_line, 1950s → 2020s) ------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "WITH decades AS ("
            "  SELECT generate_series(1950, 2020, 10) AS dec_start "
            ") "
            "SELECT d.dec_start, "
            "       COALESCE((SELECT COUNT(*) FROM archive_search_and_rag_logs a "
            "                  WHERE a.target_decade >= d.dec_start "
            "                    AND a.target_decade < d.dec_start + 10 "
            "                    AND a.query_mode IN ('keyword', 'search') "
            "                    AND a.created_at >= $1::date "
            "                    AND a.created_at < ($2::date + INTERVAL '1 day')), 0) AS keyword_searches, "
            "       COALESCE((SELECT COUNT(*) FROM archive_search_and_rag_logs a "
            "                  WHERE a.target_decade >= d.dec_start "
            "                    AND a.target_decade < d.dec_start + 10 "
            "                    AND a.query_mode = 'rag' "
            "                    AND a.created_at >= $1::date "
            "                    AND a.created_at < ($2::date + INTERVAL '1 day')), 0) AS rag_queries "
            "FROM decades d ORDER BY 1 ASC",
            filter.startDate, filter.endDate);

        Json::Value labels = Json::arrayValue;
        Json::Value keyword = Json::arrayValue;
        Json::Value rag = Json::arrayValue;
        for (const auto &row : rows) {
            const long long decStart = asLong(row, "dec_start");
            labels.append(std::to_string(decStart) + "s");
            keyword.append(jsonLong(asLong(row, "keyword_searches")));
            rag.append(jsonLong(asLong(row, "rag_queries")));
        }
        Json::Value series = Json::arrayValue;
        series.append(ReportResponseBuilder::makeSeries("Keyword Searches", "bar", keyword));
        series.append(ReportResponseBuilder::makeSeries("RAG Queries", "line", rag, 1));
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "archive_decade_demand", "Archive Demand by Decade", "bar_line",
            labels, series, "Queries", "RAG Queries"));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("decade", "Decade")
        .addColumn("indexedPages", "Indexed Pages", "number", "right")
        .addColumn("keywordSearches", "Keyword Searches", "number", "right")
        .addColumn("ragQueries", "RAG Queries", "number", "right")
        .addColumn("topQuery", "Top Query");

    {
        const auto rows = co_await dbClient->execSqlCoro(
            "WITH decades AS ("
            "  SELECT generate_series(1950, 2020, 10) AS dec_start "
            ") "
            "SELECT d.dec_start, "
            "       COALESCE((SELECT COUNT(*) FROM newspaper_details nd "
            "                  WHERE EXTRACT(YEAR FROM nd.publication_date) >= d.dec_start "
            "                    AND EXTRACT(YEAR FROM nd.publication_date) < d.dec_start + 10), 0) AS indexed_pages, "
            "       COALESCE((SELECT COUNT(*) FROM archive_search_and_rag_logs a "
            "                  WHERE a.target_decade >= d.dec_start "
            "                    AND a.target_decade < d.dec_start + 10 "
            "                    AND a.query_mode IN ('keyword', 'search') "
            "                    AND a.created_at >= $1::date "
            "                    AND a.created_at < ($2::date + INTERVAL '1 day')), 0) AS keyword_searches, "
            "       COALESCE((SELECT COUNT(*) FROM archive_search_and_rag_logs a "
            "                  WHERE a.target_decade >= d.dec_start "
            "                    AND a.target_decade < d.dec_start + 10 "
            "                    AND a.query_mode = 'rag' "
            "                    AND a.created_at >= $1::date "
            "                    AND a.created_at < ($2::date + INTERVAL '1 day')), 0) AS rag_queries, "
            "       COALESCE((SELECT q.query_text FROM ("
            "                  SELECT a.query_text, COUNT(*) AS cnt "
            "                    FROM archive_search_and_rag_logs a "
            "                   WHERE a.target_decade >= d.dec_start "
            "                     AND a.target_decade < d.dec_start + 10 "
            "                     AND a.created_at >= $1::date "
            "                     AND a.created_at < ($2::date + INTERVAL '1 day') "
            "                   GROUP BY 1 ORDER BY cnt DESC LIMIT 1) q), '—') AS top_query "
            "FROM decades d ORDER BY 1 ASC",
            filter.startDate, filter.endDate);

        for (const auto &row : rows) {
            const long long decStart = asLong(row, "dec_start");
            Json::Value item(Json::objectValue);
            item["decade"] = std::to_string(decStart) + " – " + std::to_string(decStart + 9);
            item["indexedPages"] = jsonLong(asLong(row, "indexed_pages"));
            item["keywordSearches"] = jsonLong(asLong(row, "keyword_searches"));
            item["ragQueries"] = jsonLong(asLong(row, "rag_queries"));
            item["topQuery"] = asString(row, "top_query");
            builder.addRow(item);
        }
    }

    co_return builder.buildResult();
}

// ---------------------------------------------------------------------------
// Endpoint 6.2 — POST /api/v1/reports/institutional-archive-credits
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildInstitutionalArchiveCreditsReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/institutional-archive-credits");

    const std::string statusFilter = filter.status.value_or("");

    // ---- Summary -----------------------------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  (SELECT COUNT(*) FROM institutional_archive_accounts "
            "    WHERE status = 'Active') AS active_institutions, "
            "  (SELECT COALESCE(SUM(ABS(t.credits_delta)), 0) "
            "     FROM archive_credit_transactions t "
            "    WHERE t.tx_type = 'consume' "
            "      AND t.created_at >= $1::date AND t.created_at < ($2::date + INTERVAL '1 day')) AS consumed, "
            "  (SELECT COALESCE(SUM(a.contract_value_ghs::NUMERIC), 0) "
            "     FROM institutional_archive_accounts a "
            "    WHERE EXISTS (SELECT 1 FROM archive_credit_transactions t "
            "                   WHERE t.institution_id = a.id AND t.tx_type = 'topup' "
            "                     AND t.created_at >= $1::date "
            "                     AND t.created_at < ($2::date + INTERVAL '1 day'))) AS topup_revenue, "
            "  (SELECT COUNT(*) FROM archive_credit_transactions t "
            "    WHERE t.tx_type = 'export' "
            "      AND t.created_at >= $1::date AND t.created_at < ($2::date + INTERVAL '1 day')) AS exports",
            filter.startDate, filter.endDate);

        builder.addSummary("activeInstitutions", "Active Institutions",
                           (double)asLong(rows[0], "active_institutions"), "number");
        builder.addSummary("creditsConsumed", "Credits Consumed",
                           (double)asLong(rows[0], "consumed"), "number");
        builder.addSummary("topupRevenueGhs", "Top-up Revenue (GHS)",
                           asDouble(rows[0], "topup_revenue"), "currency");
        builder.addSummary("clippingsExported", "Clippings Exported",
                           (double)asLong(rows[0], "exports"), "number");
    }

    // ---- Chart: institution_credit_consumption (horizontal_bar) ------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT a.institution_name AS institution, "
            "       COALESCE((SELECT SUM(ABS(t.credits_delta)) FROM archive_credit_transactions t "
            "                  WHERE t.institution_id = a.id AND t.tx_type = 'consume' "
            "                    AND t.created_at >= $1::date "
            "                    AND t.created_at < ($2::date + INTERVAL '1 day')), 0) AS consumed "
            "FROM institutional_archive_accounts a "
            "ORDER BY consumed DESC LIMIT 8",
            filter.startDate, filter.endDate);

        Json::Value labels = Json::arrayValue;
        Json::Value data = Json::arrayValue;
        for (const auto &row : rows) {
            labels.append(asString(row, "institution"));
            data.append(jsonLong(asLong(row, "consumed")));
        }
        Json::Value series = Json::arrayValue;
        series.append(ReportResponseBuilder::makeSeries("Credits Consumed", "bar", data));
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "institution_credit_consumption", "Credit Consumption by Institution",
            "horizontal_bar", labels, series, "Credits", "", false));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("institution", "Institution")
        .addColumn("purchasedCredits", "Purchased", "number", "right")
        .addColumn("consumedCredits", "Consumed", "number", "right")
        .addColumn("remainingBalance", "Remaining", "number", "right")
        .addColumn("billedGhs", "Billed (GHS)", "currency", "right")
        .addColumn("status", "Status", "badge");

    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT a.institution_name AS institution, "
            "       COALESCE(a.credits_allocated, 0) AS purchased, "
            "       COALESCE(a.credits_allocated, 0) - COALESCE(a.credits_remaining, 0) AS consumed, "
            "       COALESCE(a.credits_remaining, 0) AS remaining, "
            "       COALESCE(a.contract_value_ghs, 0)::NUMERIC AS billed, "
            "       COALESCE(a.status, 'Active') AS status "
            "FROM institutional_archive_accounts a "
            "WHERE ($1 = '' OR LOWER(a.status) = $1 "
            "       OR ($1 = 'active' AND a.status = 'Active') "
            "       OR ($1 = 'inactive' AND a.status IN ('Suspended', 'Expired')) "
            "       OR ($1 = 'pending' AND a.status = 'Pending')) "
            "ORDER BY billed DESC "
            "LIMIT $2 OFFSET $3",
            statusFilter, filter.pageSize, (filter.pageNo - 1) * filter.pageSize);

        for (const auto &row : rows) {
            Json::Value item(Json::objectValue);
            item["institution"] = asString(row, "institution");
            item["purchasedCredits"] = jsonLong(asLong(row, "purchased"));
            item["consumedCredits"] = jsonLong(asLong(row, "consumed"));
            item["remainingBalance"] = jsonLong(asLong(row, "remaining"));
            item["billedGhs"] = jsonDouble(asDouble(row, "billed"));
            item["status"] = ReportResponseBuilder::normalizeBadgeStatus(asString(row, "status"));
            builder.addRow(item);
        }
    }

    co_return builder.buildResult();
}

// ---------------------------------------------------------------------------
// Endpoint 7.1 — POST /api/v1/reports/games-engagement-retention
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildGamesEngagementRetentionReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/games-engagement-retention");

    // ---- Summary -----------------------------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  COUNT(*) AS sessions_started, "
            "  COUNT(*) FILTER (WHERE g.is_complete = TRUE) AS completed "
            "FROM game_sessions g "
            "WHERE g.start_time >= $1::date AND g.start_time < ($2::date + INTERVAL '1 day')",
            filter.startDate, filter.endDate);

        const double started = (double)asLong(rows[0], "sessions_started");
        const double completed = (double)asLong(rows[0], "completed");

        const auto streakRows = co_await dbClient->execSqlCoro(
            "SELECT COUNT(*) AS streak_holders FROM user_game_stats s "
            "WHERE COALESCE(s.current_streak, 0) >= 7");

        // Churn-reduction lift: churn rate of non-players vs players.
        const auto churnRows = co_await dbClient->execSqlCoro(
            "WITH sub_stats AS ("
            "  SELECT us.user_id, "
            "         (us.cancelled_at IS NOT NULL) AS churned, "
            "         EXISTS (SELECT 1 FROM game_sessions gs "
            "                  WHERE gs.user_id = us.user_id) AS is_player "
            "  FROM user_subscriptions us "
            ") "
            "SELECT "
            "  COALESCE(AVG(CASE WHEN is_player THEN (churned::INT)::NUMERIC END), 0) AS player_churn_rate, "
            "  COALESCE(AVG(CASE WHEN NOT is_player THEN (churned::INT)::NUMERIC END), 0) AS non_player_churn_rate "
            "FROM sub_stats");

        const double playerChurn = asDouble(churnRows[0], "player_churn_rate");
        const double nonPlayerChurn = asDouble(churnRows[0], "non_player_churn_rate");
        const double lift = nonPlayerChurn > 0.0
            ? round2(((nonPlayerChurn - playerChurn) / nonPlayerChurn) * 100.0)
            : 0.0;

        builder.addSummary("dailyPuzzleStarts", "Puzzle Sessions", started, "number");
        builder.addSummary("completionRatePct", "Completion Rate", pct(completed, started), "percent");
        builder.addSummary("streakHolders7d", "7-Day Streak Holders",
                           (double)asLong(streakRows[0], "streak_holders"), "number");
        builder.addSummary("churnReductionLift", "Churn Reduction Lift", lift, "percent");
    }

    // ---- Chart: games_sessions_by_type (bar_line) --------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT to_char(g.start_time, 'YYYY-MM-DD') AS day, "
            "       COUNT(*) AS started, "
            "       COUNT(*) FILTER (WHERE g.is_complete = TRUE) AS completed "
            "FROM game_sessions g "
            "WHERE g.start_time >= $1::date AND g.start_time < ($2::date + INTERVAL '1 day') "
            "GROUP BY 1 ORDER BY 1 ASC",
            filter.startDate, filter.endDate);

        Json::Value labels = Json::arrayValue;
        Json::Value started = Json::arrayValue;
        Json::Value completed = Json::arrayValue;
        for (const auto &row : rows) {
            labels.append(dayLabel(asString(row, "day")));
            started.append(jsonLong(asLong(row, "started")));
            completed.append(jsonLong(asLong(row, "completed")));
        }
        Json::Value series = Json::arrayValue;
        series.append(ReportResponseBuilder::makeSeries("Sessions Started", "bar", started));
        series.append(ReportResponseBuilder::makeSeries("Completed", "line", completed, 1));
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "games_sessions_by_type", "Game Sessions — Started vs Completed", "bar_line",
            labels, series, "Sessions", "Completed"));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("gameTitle", "Game")
        .addColumn("sessionsStarted", "Sessions", "number", "right")
        .addColumn("completedCount", "Completed", "number", "right")
        .addColumn("completionPct", "Completion", "percent", "right")
        .addColumn("avgSolveSec", "Avg Solve (Sec)", "number", "right");

    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT CASE g.game_type "
            "         WHEN 1 THEN 'Sudoku' "
            "         WHEN 2 THEN 'Word Search' "
            "         WHEN 3 THEN 'Graphic Daily Mini Crossword' "
            "         WHEN 4 THEN 'Riddles & Brain Teasers' "
            "         ELSE 'Game Type ' || COALESCE(g.game_type::TEXT, '?') "
            "       END AS game_title, "
            "       COUNT(*) AS sessions_started, "
            "       COUNT(*) FILTER (WHERE g.is_complete = TRUE) AS completed, "
            "       COALESCE(AVG(g.completion_time_seconds) "
            "                FILTER (WHERE g.is_complete = TRUE), 0) AS avg_solve_sec "
            "FROM game_sessions g "
            "WHERE g.start_time >= $1::date AND g.start_time < ($2::date + INTERVAL '1 day') "
            "GROUP BY g.game_type, 1 "
            "ORDER BY sessions_started DESC",
            filter.startDate, filter.endDate);

        for (const auto &row : rows) {
            const double started = (double)asLong(row, "sessions_started");
            const double completed = (double)asLong(row, "completed");
            Json::Value item(Json::objectValue);
            item["gameTitle"] = asString(row, "game_title");
            item["sessionsStarted"] = jsonLong((long long)started);
            item["completedCount"] = jsonLong((long long)completed);
            item["completionPct"] = jsonDouble(pct(completed, started));
            item["avgSolveSec"] = jsonDouble(asDouble(row, "avg_solve_sec"));
            builder.addRow(item);
        }
    }

    co_return builder.buildResult();
}

} // namespace gnp::services
