//
// ReportingService — Group 3: Content & Newspaper report engines
// (newspaper_engagement, content_inventory, ingestion_jobs,
//  multimedia_originals_performance).
//
// See docs/REPORTS_IMPLEMENTATION_PLAN_AND_API_SPEC.md §2.5.
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
// Endpoint 3.1 — POST /api/v1/reports/newspaper-engagement
// Includes the `partners[]` quota progress bars rendered by the report viewer.
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildNewspaperEngagementReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/newspaper-engagement");

    // ---- partners[] quota progress -----------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT cp.name AS name, "
            "       COALESCE(cp.subscriber_quota, 0) AS quota, "
            "       COALESCE((SELECT COUNT(*) FROM users u "
            "                  WHERE u.partner_id = cp.id AND u.is_active = TRUE), 0) AS enrolled "
            "FROM commercial_partners cp "
            "ORDER BY cp.name ASC");

        Json::Value partners = Json::arrayValue;
        for (const auto &row : rows) {
            Json::Value item(Json::objectValue);
            item["name"] = asString(row, "name");
            item["enrolled"] = jsonLong(asLong(row, "enrolled"));
            item["quota"] = jsonLong(asLong(row, "quota"));
            partners.append(item);
        }
        builder.setPartners(partners);
    }

    // ---- Summary -----------------------------------------------------------
    long long totalViews = 0;
    long long copiesSold = 0;
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  (SELECT COUNT(*) FROM newspaper_engagement "
            "    WHERE viewed_at >= $1::date AND viewed_at < ($2::date + INTERVAL '1 day')) AS views_cur, "
            "  (SELECT COUNT(*) FROM edition_sales_ledger "
            "    WHERE sold_at >= $1::date AND sold_at < ($2::date + INTERVAL '1 day')) AS sold_cur, "
            "  (SELECT COALESCE(AVG(time_spent_seconds) / 60.0, 0) FROM newspaper_engagement "
            "    WHERE viewed_at >= $1::date AND viewed_at < ($2::date + INTERVAL '1 day')) AS avg_minutes, "
            "  (SELECT COUNT(*) FROM media_playback_events "
            "    WHERE media_type IN ('tts', 'audio') "
            "      AND played_at >= $1::date AND played_at < ($2::date + INTERVAL '1 day')) AS tts_listens",
            filter.startDate, filter.endDate);

        totalViews = asLong(rows[0], "views_cur");
        copiesSold = asLong(rows[0], "sold_cur");
        builder.addSummary("totalEditionViews", "Edition Views", (double)totalViews, "number");
        builder.addSummary("singleCopiesSold", "Single Copies Sold", (double)copiesSold, "number");
        builder.addSummary("avgReadMinutes", "Avg Read (Min)", asDouble(rows[0], "avg_minutes"), "number");
        builder.addSummary("audioTtsListens", "Audio / TTS Listens", (double)asLong(rows[0], "tts_listens"), "number");
    }

    // ---- Chart 1: quota_utilization_donut ----------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(SUM(COALESCE(cp.subscriber_quota, 0)), 0) AS quota, "
            "       COALESCE((SELECT COUNT(*) FROM users u "
            "                  WHERE u.partner_id IS NOT NULL AND u.is_active = TRUE), 0) AS enrolled "
            "FROM commercial_partners cp");

        const long long quota = asLong(rows[0], "quota");
        const long long enrolled = asLong(rows[0], "enrolled");
        Json::Value items = Json::arrayValue;
        {
            Json::Value item(Json::objectValue);
            item["name"] = "Enrolled Seats";
            item["value"] = jsonLong(enrolled);
            items.append(item);
        }
        {
            Json::Value item(Json::objectValue);
            item["name"] = "Remaining Quota";
            item["value"] = jsonLong(quota > enrolled ? quota - enrolled : 0);
            items.append(item);
        }
        builder.addChart(ReportResponseBuilder::makeDonutChart(
            "quota_utilization_donut", "Partner Quota Utilization", items));
    }

    // ---- Chart 2: engagement_and_sales_7d (bar_line) -----------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "WITH days AS ("
            "  SELECT generate_series(($1::date - INTERVAL '6 days'), $1::date, INTERVAL '1 day')::date AS day "
            ") "
            "SELECT to_char(d.day, 'YYYY-MM-DD') AS day, "
            "       COALESCE((SELECT COUNT(*) FROM newspaper_engagement ne "
            "                  WHERE date_trunc('day', ne.viewed_at) = d.day), 0) AS views, "
            "       COALESCE((SELECT COUNT(*) FROM edition_sales_ledger l "
            "                  WHERE date_trunc('day', l.sold_at) = d.day), 0) AS sold "
            "FROM days d ORDER BY 1 ASC",
            filter.endDate);

        Json::Value labels = Json::arrayValue;
        Json::Value views = Json::arrayValue;
        Json::Value sold = Json::arrayValue;
        for (const auto &row : rows) {
            labels.append(dayLabel(asString(row, "day")));
            views.append(jsonLong(asLong(row, "views")));
            sold.append(jsonLong(asLong(row, "sold")));
        }
        Json::Value series = Json::arrayValue;
        series.append(ReportResponseBuilder::makeSeries("Edition Views", "bar", views));
        series.append(ReportResponseBuilder::makeSeries("Copies Sold", "line", sold, 1));
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "engagement_and_sales_7d", "Engagement & Sales (Last 7 Days)", "bar_line",
            labels, series, "Views", "Copies"));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("publication", "Publication")
        .addColumn("editionsPublished", "Editions", "number", "right")
        .addColumn("totalViews", "Views", "number", "right")
        .addColumn("copiesSold", "Copies Sold", "number", "right")
        .addColumn("completionPct", "Completion", "percent", "right")
        .addColumn("revenueGhs", "Revenue (GHS)", "currency", "right");

    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(pub.name, ''), 'Unassigned') AS publication, "
            "       COUNT(DISTINCT n.id) AS editions, "
            "       COALESCE((SELECT COUNT(*) FROM newspaper_engagement ne "
            "                  JOIN newspapers nn ON nn.id = ne.newspaper_id "
            "                 WHERE nn.publication_id = pub.id "
            "                   AND ne.viewed_at >= $1::date "
            "                   AND ne.viewed_at < ($2::date + INTERVAL '1 day')), 0) AS views, "
            "       COALESCE((SELECT COUNT(*) FROM edition_sales_ledger l "
            "                  WHERE l.publication_id = pub.id "
            "                    AND l.sold_at >= $1::date "
            "                    AND l.sold_at < ($2::date + INTERVAL '1 day')), 0) AS copies_sold, "
            "       COALESCE((SELECT AVG(CASE WHEN ne.time_spent_seconds >= 120 THEN 100.0 ELSE 0.0 END) "
            "                   FROM newspaper_engagement ne "
            "                  JOIN newspapers nn ON nn.id = ne.newspaper_id "
            "                 WHERE nn.publication_id = pub.id "
            "                   AND ne.viewed_at >= $1::date "
            "                   AND ne.viewed_at < ($2::date + INTERVAL '1 day')), 0) AS completion_pct, "
            "       COALESCE((SELECT SUM(l.amount_paid::NUMERIC) FROM edition_sales_ledger l "
            "                  WHERE l.publication_id = pub.id "
            "                    AND l.sold_at >= $1::date "
            "                    AND l.sold_at < ($2::date + INTERVAL '1 day')), 0) AS revenue "
            "FROM publications pub "
            "JOIN newspapers n ON n.publication_id = pub.id "
            "                 AND n.publication_date >= $1::date "
            "                 AND n.publication_date < ($2::date + INTERVAL '1 day') "
            "GROUP BY pub.id, pub.name "
            "ORDER BY revenue DESC, views DESC",
            filter.startDate, filter.endDate);

        for (const auto &row : rows) {
            Json::Value item(Json::objectValue);
            item["publication"] = asString(row, "publication");
            item["editionsPublished"] = jsonLong(asLong(row, "editions"));
            item["totalViews"] = jsonLong(asLong(row, "views"));
            item["copiesSold"] = jsonLong(asLong(row, "copies_sold"));
            item["completionPct"] = jsonDouble(asDouble(row, "completion_pct"));
            item["revenueGhs"] = jsonDouble(asDouble(row, "revenue"));
            builder.addRow(item);
        }
    }

    co_return builder.buildResult();
}

// ---------------------------------------------------------------------------
// Endpoint 3.2 — POST /api/v1/reports/content-inventory
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildContentInventoryReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/content-inventory");

    // ---- Summary -----------------------------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  (SELECT COUNT(*) FROM newspapers) AS issues, "
            "  (SELECT COUNT(*) FROM newspaper_details) AS articles, "
            "  (SELECT COUNT(*) FROM publications WHERE type = 0) AS magazines, "
            "  (SELECT COUNT(*) FROM newspapers n "
            "    WHERE EXISTS (SELECT 1 FROM newspaper_details d WHERE d.newspaper_id = n.id)) AS ocr_ready");

        const long long issues = asLong(rows[0], "issues");
        const long long ocrReady = asLong(rows[0], "ocr_ready");

        builder.addSummary("totalIssuesCataloged", "Issues Cataloged", (double)issues, "number");
        builder.addSummary("extractedArticles", "Extracted Articles / Pages", (double)asLong(rows[0], "articles"), "number");
        builder.addSummary("magazinesCount", "Magazines", (double)asLong(rows[0], "magazines"), "number");
        builder.addSummary("ocrReadyPct", "OCR Ready", pct((double)ocrReady, (double)issues), "percent");
    }

    // ---- Chart 1: catalog_by_publication (horizontal_bar) ------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(pub.name, ''), 'Unassigned') AS publication, COUNT(n.id) AS issues "
            "FROM publications pub "
            "LEFT JOIN newspapers n ON n.publication_id = pub.id "
            "GROUP BY 1 ORDER BY issues DESC");

        Json::Value labels = Json::arrayValue;
        Json::Value data = Json::arrayValue;
        for (const auto &row : rows) {
            labels.append(asString(row, "publication"));
            data.append(jsonLong(asLong(row, "issues")));
        }
        Json::Value series = Json::arrayValue;
        series.append(ReportResponseBuilder::makeSeries("Issues Cataloged", "bar", data));
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "catalog_by_publication", "Catalog Size by Publication", "horizontal_bar",
            labels, series, "Issues", "", false));
    }

    // ---- Chart 2: inventory_status_donut -----------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  COUNT(*) FILTER (WHERE n.is_published = TRUE AND n.is_archived = FALSE) AS published_cnt, "
            "  COUNT(*) FILTER (WHERE n.is_published = FALSE) AS pending_cnt, "
            "  COUNT(*) FILTER (WHERE n.is_archived = TRUE) AS archived_cnt "
            "FROM newspapers n");

        Json::Value items = Json::arrayValue;
        {
            Json::Value item(Json::objectValue);
            item["name"] = "Published";
            item["value"] = jsonLong(asLong(rows[0], "published_cnt"));
            items.append(item);
        }
        {
            Json::Value item(Json::objectValue);
            item["name"] = "Pending";
            item["value"] = jsonLong(asLong(rows[0], "pending_cnt"));
            items.append(item);
        }
        {
            Json::Value item(Json::objectValue);
            item["name"] = "Archived";
            item["value"] = jsonLong(asLong(rows[0], "archived_cnt"));
            items.append(item);
        }
        builder.addChart(ReportResponseBuilder::makeDonutChart(
            "inventory_status_donut", "Inventory Status", items));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("title", "Publication")
        .addColumn("frequency", "Frequency")
        .addColumn("publishedCount", "Published", "number", "right")
        .addColumn("articlesIndexed", "Articles Indexed", "number", "right")
        .addColumn("latestIssue", "Latest Issue")
        .addColumn("status", "Status", "badge");

    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(pub.name, ''), 'Unassigned') AS title, "
            "       COALESCE(NULLIF(pub.publishing_days, ''), 'N/A') AS frequency, "
            "       COUNT(n.id) AS published_count, "
            "       COALESCE((SELECT COUNT(*) FROM newspaper_details d "
            "                  JOIN newspapers nn ON nn.id = d.newspaper_id "
            "                 WHERE nn.publication_id = pub.id), 0) AS articles_indexed, "
            "       COALESCE(to_char(MAX(n.publication_date), 'YYYY-MM-DD'), '—') AS latest_issue, "
            "       CASE WHEN pub.is_active THEN 'Published' ELSE 'Inactive' END AS status "
            "FROM publications pub "
            "LEFT JOIN newspapers n ON n.publication_id = pub.id AND n.is_published = TRUE "
            "WHERE ($1 = '' OR ($1 = 'active' AND pub.is_active = TRUE) "
            "       OR ($1 = 'inactive' AND pub.is_active = FALSE) "
            "       OR ($1 = 'pending' AND pub.is_active = FALSE)) "
            "GROUP BY pub.id, pub.name, pub.publishing_days, pub.is_active "
            "ORDER BY published_count DESC",
            filter.status.value_or(""));

        for (const auto &row : rows) {
            Json::Value item(Json::objectValue);
            item["title"] = asString(row, "title");
            item["frequency"] = asString(row, "frequency");
            item["publishedCount"] = jsonLong(asLong(row, "published_count"));
            item["articlesIndexed"] = jsonLong(asLong(row, "articles_indexed"));
            item["latestIssue"] = asString(row, "latest_issue");
            item["status"] = ReportResponseBuilder::normalizeBadgeStatus(asString(row, "status"));
            builder.addRow(item);
        }
    }

    co_return builder.buildResult();
}

// ---------------------------------------------------------------------------
// Endpoint 3.3 — POST /api/v1/reports/ingestion-jobs
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildIngestionJobsReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/ingestion-jobs");

    const std::string statusFilter = filter.status.value_or("");

    // ---- Summary -----------------------------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  COUNT(*) FILTER (WHERE LOWER(i.status) LIKE 'complete%' "
            "                   OR LOWER(i.status) LIKE 'success%') AS completed, "
            "  COUNT(*) AS total, "
            "  COALESCE(AVG(i.processing_duration_ms) / 1000.0, 0) AS avg_seconds, "
            "  COALESCE(AVG(i.ocr_confidence_avg), 0) AS avg_ocr_confidence "
            "FROM ingestion_jobs i "
            "WHERE i.created_at >= $1::date AND i.created_at < ($2::date + INTERVAL '1 day')",
            filter.startDate, filter.endDate);

        const long long completed = asLong(rows[0], "completed");
        const long long total = asLong(rows[0], "total");

        builder.addSummary("jobsCompleted30d", "Jobs Completed", (double)completed, "number");
        builder.addSummary("pipelineSuccessPct", "Pipeline Success", pct((double)completed, (double)total), "percent");
        builder.addSummary("avgProcessingSec", "Avg Processing (Sec)", asDouble(rows[0], "avg_seconds"), "number");
        builder.addSummary("avgOcrConfidence", "Avg OCR Confidence", asDouble(rows[0], "avg_ocr_confidence"), "percent");
    }

    // ---- Chart: ingestion_latency_trend (bar_line) -------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT to_char(i.created_at, 'YYYY-MM-DD') AS day, "
            "       COUNT(*) AS jobs, "
            "       COALESCE(AVG(i.processing_duration_ms) / 1000.0, 0) AS avg_seconds "
            "FROM ingestion_jobs i "
            "WHERE i.created_at >= $1::date AND i.created_at < ($2::date + INTERVAL '1 day') "
            "GROUP BY 1 ORDER BY 1 ASC",
            filter.startDate, filter.endDate);

        Json::Value labels = Json::arrayValue;
        Json::Value jobs = Json::arrayValue;
        Json::Value seconds = Json::arrayValue;
        for (const auto &row : rows) {
            labels.append(dayLabel(asString(row, "day")));
            jobs.append(jsonLong(asLong(row, "jobs")));
            seconds.append(jsonDouble(asDouble(row, "avg_seconds")));
        }
        Json::Value series = Json::arrayValue;
        series.append(ReportResponseBuilder::makeSeries("Jobs", "bar", jobs));
        series.append(ReportResponseBuilder::makeSeries("Avg Duration (Sec)", "line", seconds, 1));
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "ingestion_latency_trend", "Ingestion Volume & Latency", "bar_line",
            labels, series, "Jobs", "Seconds"));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("jobId", "Job")
        .addColumn("edition", "Edition")
        .addColumn("pages", "Pages", "number", "right")
        .addColumn("articlesExtracted", "Articles", "number", "right")
        .addColumn("durationSec", "Duration (Sec)", "number", "right")
        .addColumn("completedAt", "Completed At")
        .addColumn("status", "Status", "badge");

    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT 'ING-' || UPPER(LEFT(REPLACE(i.id::text, '-', ''), 8)) AS job_id, "
            "       COALESCE(NULLIF(n.title, ''), 'Edition ' || COALESCE(to_char(i.publication_date, 'YYYY-MM-DD'), '?')) AS edition, "
            "       COALESCE(i.total_pages, i.page_count, 0) AS pages, "
            "       COALESCE(i.articles_extracted, 0) AS articles_extracted, "
            "       ROUND(COALESCE(i.processing_duration_ms, 0) / 1000.0, 2) AS duration_sec, "
            "       COALESCE(to_char(i.completed_at, 'YYYY-MM-DD HH24:MI'), "
            "                to_char(i.updated_at, 'YYYY-MM-DD HH24:MI')) AS completed_at, "
            "       i.status AS status "
            "FROM ingestion_jobs i "
            "LEFT JOIN newspapers n ON n.id = i.newspaper_id "
            "WHERE i.created_at >= $1::date AND i.created_at < ($2::date + INTERVAL '1 day') "
            "  AND ($3 = '' OR LOWER(i.status) = $3 "
            "       OR ($3 = 'active' AND LOWER(i.status) LIKE 'complete%') "
            "       OR ($3 = 'inactive' AND LOWER(i.status) LIKE 'fail%') "
            "       OR ($3 = 'pending' AND LOWER(i.status) LIKE 'process%')) "
            "ORDER BY i.created_at DESC "
            "LIMIT $4 OFFSET $5",
            filter.startDate, filter.endDate, statusFilter,
            filter.pageSize, (filter.pageNo - 1) * filter.pageSize);

        for (const auto &row : rows) {
            Json::Value item(Json::objectValue);
            item["jobId"] = asString(row, "job_id");
            item["edition"] = asString(row, "edition");
            item["pages"] = jsonLong(asLong(row, "pages"));
            item["articlesExtracted"] = jsonLong(asLong(row, "articles_extracted"));
            item["durationSec"] = jsonDouble(asDouble(row, "duration_sec"));
            item["completedAt"] = asString(row, "completed_at");
            item["status"] = ReportResponseBuilder::normalizeBadgeStatus(asString(row, "status"));
            builder.addRow(item);
        }
    }

    co_return builder.buildResult();
}

// ---------------------------------------------------------------------------
// Endpoint 3.4 — POST /api/v1/reports/multimedia-originals-performance
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildMultimediaOriginalsPerformanceReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/multimedia-originals-performance");

    // ---- Summary -----------------------------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  COUNT(*) FILTER (WHERE e.media_type = 'video') AS video_plays, "
            "  COUNT(*) FILTER (WHERE e.media_type IN ('audio', 'tts')) AS podcast_streams, "
            "  COUNT(*) FILTER (WHERE e.media_type = 'originals') AS originals_reads, "
            "  COALESCE(AVG(e.completion_pct), 0) AS avg_completion "
            "FROM media_playback_events e "
            "WHERE e.played_at >= $1::date AND e.played_at < ($2::date + INTERVAL '1 day')",
            filter.startDate, filter.endDate);

        builder.addSummary("videoPlays", "Video Plays", (double)asLong(rows[0], "video_plays"), "number");
        builder.addSummary("podcastStreams", "Podcast Streams", (double)asLong(rows[0], "podcast_streams"), "number");
        builder.addSummary("originalsReads", "Originals Reads", (double)asLong(rows[0], "originals_reads"), "number");
        builder.addSummary("avgCompletionPct", "Avg Completion", asDouble(rows[0], "avg_completion"), "percent");
    }

    // ---- Chart: multimedia_format_growth (area_line) -----------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "WITH weeks AS ("
            "  SELECT generate_series(date_trunc('week', $1::date), "
            "                         date_trunc('week', $2::date), "
            "                         INTERVAL '1 week')::date AS bucket "
            ") "
            "SELECT to_char(w.bucket, 'YYYY-MM-DD') AS bucket, "
            "       COALESCE(SUM(CASE WHEN e.media_type = 'video' THEN 1 ELSE 0 END), 0) AS video_cnt, "
            "       COALESCE(SUM(CASE WHEN e.media_type IN ('audio', 'tts') THEN 1 ELSE 0 END), 0) AS audio_cnt, "
            "       COALESCE(SUM(CASE WHEN e.media_type = 'originals' THEN 1 ELSE 0 END), 0) AS originals_cnt "
            "FROM weeks w "
            "LEFT JOIN media_playback_events e "
            "  ON date_trunc('week', e.played_at) = w.bucket "
            "GROUP BY 1 ORDER BY 1 ASC",
            filter.startDate, filter.endDate);

        Json::Value labels = Json::arrayValue;
        Json::Value video = Json::arrayValue;
        Json::Value audio = Json::arrayValue;
        Json::Value originals = Json::arrayValue;
        for (const auto &row : rows) {
            labels.append(dayLabel(asString(row, "bucket")));
            video.append(jsonLong(asLong(row, "video_cnt")));
            audio.append(jsonLong(asLong(row, "audio_cnt")));
            originals.append(jsonLong(asLong(row, "originals_cnt")));
        }
        Json::Value series = Json::arrayValue;
        series.append(ReportResponseBuilder::makeSeries("Video Documentaries", "bar", video));
        series.append(ReportResponseBuilder::makeSeries("Podcasts & Audio Briefings", "bar", audio));
        series.append(ReportResponseBuilder::makeSeries("Originals Reads", "bar", originals));
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "multimedia_format_growth", "Weekly Consumption by Format", "area_line",
            labels, series, "Streams / Reads"));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("title", "Title")
        .addColumn("format", "Format")
        .addColumn("playsOrReads", "Plays / Reads", "number", "right")
        .addColumn("completionPct", "Completion", "percent", "right")
        .addColumn("subscriberConversions", "Conversions", "number", "right");

    {
        // Video rows.
        const auto videoRows = co_await dbClient->execSqlCoro(
            "SELECT v.title AS title, 'Video' AS format, "
            "       COALESCE(v.views, 0) AS plays, "
            "       COALESCE((SELECT AVG(e.completion_pct) FROM media_playback_events e "
            "                  WHERE e.media_type = 'video' AND e.media_id = v.id), 0) AS completion, "
            "       COALESCE((SELECT COUNT(DISTINCT e.user_id) FROM media_playback_events e "
            "                  WHERE e.media_type = 'video' AND e.media_id = v.id "
            "                    AND e.user_id IS NOT NULL "
            "                    AND EXISTS (SELECT 1 FROM payments p "
            "                                 WHERE p.user_id = e.user_id AND p.status = 'Success' "
            "                                   AND p.created_at >= e.played_at)), 0) AS conversions "
            "FROM videos v "
            "ORDER BY plays DESC "
            "LIMIT $1 OFFSET $2",
            filter.pageSize, (filter.pageNo - 1) * filter.pageSize);

        for (const auto &row : videoRows) {
            Json::Value item(Json::objectValue);
            item["title"] = asString(row, "title");
            item["format"] = asString(row, "format");
            item["playsOrReads"] = jsonLong(asLong(row, "plays"));
            item["completionPct"] = jsonDouble(asDouble(row, "completion"));
            item["subscriberConversions"] = jsonLong(asLong(row, "conversions"));
            builder.addRow(item);
        }

        // Audio / podcast rows.
        const auto audioRows = co_await dbClient->execSqlCoro(
            "SELECT a.title AS title, 'Podcast' AS format, "
            "       COALESCE(a.plays, 0) AS plays, "
            "       COALESCE((SELECT AVG(e.completion_pct) FROM media_playback_events e "
            "                  WHERE e.media_type IN ('audio', 'tts') AND e.media_id = a.id), 0) AS completion, "
            "       COALESCE((SELECT COUNT(DISTINCT e.user_id) FROM media_playback_events e "
            "                  WHERE e.media_type IN ('audio', 'tts') AND e.media_id = a.id "
            "                    AND e.user_id IS NOT NULL "
            "                    AND EXISTS (SELECT 1 FROM payments p "
            "                                 WHERE p.user_id = e.user_id AND p.status = 'Success' "
            "                                   AND p.created_at >= e.played_at)), 0) AS conversions "
            "FROM audios a "
            "ORDER BY plays DESC "
            "LIMIT $1 OFFSET $2",
            filter.pageSize, (filter.pageNo - 1) * filter.pageSize);

        for (const auto &row : audioRows) {
            Json::Value item(Json::objectValue);
            item["title"] = asString(row, "title");
            item["format"] = asString(row, "format");
            item["playsOrReads"] = jsonLong(asLong(row, "plays"));
            item["completionPct"] = jsonDouble(asDouble(row, "completion"));
            item["subscriberConversions"] = jsonLong(asLong(row, "conversions"));
            builder.addRow(item);
        }

        // Originals category rollup.
        const auto originalRows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(e.originals_category, ''), 'Originals') AS title, "
            "       'Originals' AS format, "
            "       COUNT(*) AS plays, "
            "       COALESCE(AVG(e.completion_pct), 0) AS completion, "
            "       COUNT(DISTINCT e.user_id) FILTER (WHERE e.user_id IS NOT NULL "
            "         AND EXISTS (SELECT 1 FROM payments p "
            "                      WHERE p.user_id = e.user_id AND p.status = 'Success' "
            "                        AND p.created_at >= e.played_at)) AS conversions "
            "FROM media_playback_events e "
            "WHERE e.media_type = 'originals' "
            "  AND e.played_at >= $1::date AND e.played_at < ($2::date + INTERVAL '1 day') "
            "GROUP BY 1 ORDER BY plays DESC",
            filter.startDate, filter.endDate);

        for (const auto &row : originalRows) {
            Json::Value item(Json::objectValue);
            item["title"] = asString(row, "title");
            item["format"] = asString(row, "format");
            item["playsOrReads"] = jsonLong(asLong(row, "plays"));
            item["completionPct"] = jsonDouble(asDouble(row, "completion"));
            item["subscriberConversions"] = jsonLong(asLong(row, "conversions"));
            builder.addRow(item);
        }
    }

    co_return builder.buildResult();
}

} // namespace gnp::services
