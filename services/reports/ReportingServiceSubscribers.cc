//
// ReportingService — Group 2: User & Subscriber report engines
// (subscriber_demographics, plan_distribution, subscriber_retention_cohorts).
//
// See docs/REPORTS_IMPLEMENTATION_PLAN_AND_API_SPEC.md §2.4.
//

#include "ReportingService.h"

#include <drogon/orm/DbClient.h>
#include <drogon/orm/Result.h>

#include <map>
#include <vector>

#include "ReportSqlHelpers.h"
#include "utils/ReportResponseBuilder.h"

using gnp::dto::ReportFilterDto;
using gnp::utils::ReportResponseBuilder;
using namespace gnp::services::reportdetail;

namespace gnp::services {

// ---------------------------------------------------------------------------
// Endpoint 2.1 — POST /api/v1/reports/subscriber-demographics
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildSubscriberDemographicsReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/subscriber-demographics");

    // ---- Summary -----------------------------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  COUNT(*) AS total_registered, "
            "  COUNT(*) FILTER (WHERE u.is_active = TRUE) AS active_accounts, "
            "  COUNT(*) FILTER (WHERE u.is_active = TRUE "
            "                   AND COALESCE(u.country, '') <> '' "
            "                   AND LOWER(u.country) NOT LIKE '%ghana%') AS diaspora_active, "
            "  COUNT(*) FILTER (WHERE u.email_verified_at IS NOT NULL) AS verified_emails "
            "FROM users u "
            "WHERE u.created_at >= $1::date AND u.created_at < ($2::date + INTERVAL '1 day') "
            "  AND u.is_admin_user = FALSE",
            filter.startDate, filter.endDate);

        const long long total = asLong(rows[0], "total_registered");
        const long long active = asLong(rows[0], "active_accounts");
        const long long diaspora = asLong(rows[0], "diaspora_active");
        const long long verified = asLong(rows[0], "verified_emails");

        builder.addSummary("totalRegistered", "Total Registered", (double)total, "number");
        builder.addSummary("activeAccounts", "Active Accounts", (double)active, "number");
        builder.addSummary("diasporaSharePct", "Diaspora Share", pct((double)diaspora, (double)active), "percent");
        builder.addSummary("verifiedEmailsPct", "Verified Emails", pct((double)verified, (double)total), "percent");
    }

    // ---- Chart 1: signups_monthly_growth (stacked_bar) ---------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "WITH months AS ("
            "  SELECT generate_series(date_trunc('month', $1::date), "
            "                         date_trunc('month', $2::date), "
            "                         INTERVAL '1 month')::date AS bucket "
            ") "
            "SELECT to_char(m.bucket, 'YYYY-MM-DD') AS bucket, "
            "       COALESCE(SUM(CASE WHEN LOWER(COALESCE(u.country, '')) LIKE '%ghana%' "
            "                          OR COALESCE(u.country, '') = '' THEN 1 ELSE 0 END), 0) AS domestic, "
            "       COALESCE(SUM(CASE WHEN COALESCE(u.country, '') <> '' "
            "                          AND LOWER(u.country) NOT LIKE '%ghana%' THEN 1 ELSE 0 END), 0) AS diaspora "
            "FROM months m "
            "LEFT JOIN users u ON date_trunc('month', u.created_at) = m.bucket "
            "                 AND u.is_admin_user = FALSE "
            "GROUP BY 1 ORDER BY 1 ASC",
            filter.startDate, filter.endDate);

        Json::Value labels = Json::arrayValue;
        Json::Value domestic = Json::arrayValue;
        Json::Value diaspora = Json::arrayValue;
        for (const auto &row : rows) {
            labels.append(monthLabel(asString(row, "bucket")));
            domestic.append(jsonLong(asLong(row, "domestic")));
            diaspora.append(jsonLong(asLong(row, "diaspora")));
        }
        Json::Value series = Json::arrayValue;
        series.append(ReportResponseBuilder::makeSeries("Ghana (Domestic)", "bar", domestic));
        series.append(ReportResponseBuilder::makeSeries("Diaspora", "bar", diaspora));
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "signups_monthly_growth", "Monthly Signup Growth", "stacked_bar",
            labels, series, "Signups"));
    }

    // ---- Chart 2: readers_by_country_bar (horizontal_bar) ------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(TRIM(u.country), ''), 'Unspecified') AS country, COUNT(*) AS cnt "
            "FROM users u "
            "WHERE u.created_at >= $1::date AND u.created_at < ($2::date + INTERVAL '1 day') "
            "  AND u.is_admin_user = FALSE "
            "GROUP BY 1 ORDER BY cnt DESC LIMIT 8",
            filter.startDate, filter.endDate);

        Json::Value labels = Json::arrayValue;
        Json::Value data = Json::arrayValue;
        for (const auto &row : rows) {
            labels.append(asString(row, "country"));
            data.append(jsonLong(asLong(row, "cnt")));
        }
        Json::Value series = Json::arrayValue;
        series.append(ReportResponseBuilder::makeSeries("Readers", "bar", data));
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "readers_by_country_bar", "Readers by Country", "horizontal_bar",
            labels, series, "Users", "", false));
    }

    // ---- Chart 3: account_status_donut -------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  COUNT(*) FILTER (WHERE u.is_active = TRUE) AS active_cnt, "
            "  COUNT(*) FILTER (WHERE u.is_active = FALSE) AS inactive_cnt, "
            "  COUNT(*) FILTER (WHERE u.is_active = TRUE AND u.email_verified_at IS NULL) AS pending_cnt "
            "FROM users u "
            "WHERE u.created_at >= $1::date AND u.created_at < ($2::date + INTERVAL '1 day') "
            "  AND u.is_admin_user = FALSE",
            filter.startDate, filter.endDate);

        Json::Value items = Json::arrayValue;
        {
            Json::Value item(Json::objectValue);
            item["name"] = "Active";
            item["value"] = jsonLong(asLong(rows[0], "active_cnt"));
            items.append(item);
        }
        {
            Json::Value item(Json::objectValue);
            item["name"] = "Pending Verification";
            item["value"] = jsonLong(asLong(rows[0], "pending_cnt"));
            items.append(item);
        }
        {
            Json::Value item(Json::objectValue);
            item["name"] = "Inactive";
            item["value"] = jsonLong(asLong(rows[0], "inactive_cnt"));
            items.append(item);
        }
        builder.addChart(ReportResponseBuilder::makeDonutChart(
            "account_status_donut", "Account Status Split", items));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("country", "Country")
        .addColumn("totalUsers", "Users", "number", "right")
        .addColumn("paidSubscribers", "Paid", "number", "right")
        .addColumn("activePct", "Active", "percent", "right")
        .addColumn("primaryAuth", "Primary Auth")
        .addColumn("status", "Status", "badge");

    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(TRIM(u.country), ''), 'Unspecified') AS country, "
            "       COUNT(*) AS total_users, "
            "       COUNT(*) FILTER (WHERE EXISTS (SELECT 1 FROM user_subscriptions us "
            "                                       WHERE us.user_id = u.id AND us.is_active = TRUE)) AS paid_subscribers, "
            "       COUNT(*) FILTER (WHERE u.is_active = TRUE) AS active_users, "
            "       COALESCE(mode() WITHIN GROUP (ORDER BY COALESCE(NULLIF(u.credential_type, ''), 'Email + OTP')), "
            "                'Email + OTP') AS primary_auth "
            "FROM users u "
            "WHERE u.created_at >= $1::date AND u.created_at < ($2::date + INTERVAL '1 day') "
            "  AND u.is_admin_user = FALSE "
            "  AND ($3 = '' OR ($3 = 'active' AND u.is_active = TRUE) "
            "       OR ($3 = 'inactive' AND u.is_active = FALSE) "
            "       OR ($3 = 'pending' AND u.email_verified_at IS NULL)) "
            "GROUP BY 1 ORDER BY total_users DESC",
            filter.startDate, filter.endDate, filter.status.value_or(""));

        for (const auto &row : rows) {
            const double total = (double)asLong(row, "total_users");
            const double active = (double)asLong(row, "active_users");
            Json::Value item(Json::objectValue);
            item["country"] = asString(row, "country");
            item["totalUsers"] = jsonLong(asLong(row, "total_users"));
            item["paidSubscribers"] = jsonLong(asLong(row, "paid_subscribers"));
            item["activePct"] = jsonDouble(pct(active, total));
            item["primaryAuth"] = asString(row, "primary_auth");
            item["status"] = active > 0 ? "Active" : "Inactive";
            builder.addRow(item);
        }
    }

    co_return builder.buildResult();
}

// ---------------------------------------------------------------------------
// Endpoint 2.2 — POST /api/v1/reports/plan-distribution
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildPlanDistributionReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/plan-distribution");

    const std::string planFilter = filter.plan.value_or("");

    // ---- Summary -----------------------------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  COUNT(*) FILTER (WHERE us.is_active = TRUE) AS active_seats, "
            "  COUNT(*) FILTER (WHERE us.is_active = TRUE AND us.partner_id IS NULL) AS individual_seats, "
            "  COUNT(*) FILTER (WHERE us.is_active = TRUE AND us.partner_id IS NOT NULL) AS corporate_seats, "
            "  COUNT(*) FILTER (WHERE us.is_active = TRUE "
            "                   AND (LOWER(COALESCE(us.billing_cycle, '')) LIKE '%annual%' "
            "                        OR LOWER(COALESCE(us.billing_cycle, '')) LIKE '%year%')) AS annual_seats "
            "FROM user_subscriptions us "
            "LEFT JOIN subscription_plans sp ON sp.id = us.subscription_plan_id "
            "WHERE ($1 = '' OR LOWER(sp.name) LIKE '%' || $1 || '%' OR LOWER(sp.plan_type) = $1)",
            planFilter);

        const double active = (double)asLong(rows[0], "active_seats");
        const double individual = (double)asLong(rows[0], "individual_seats");
        const double corporate = (double)asLong(rows[0], "corporate_seats");
        const double annual = (double)asLong(rows[0], "annual_seats");

        builder.addSummary("activePaidSeats", "Active Paid Seats", active, "number");
        builder.addSummary("individualSeats", "Individual Seats", individual, "number");
        builder.addSummary("corporateSeats", "Corporate Seats", corporate, "number");
        builder.addSummary("annualSharePct", "Annual Billing Share", pct(annual, active), "percent");
    }

    // ---- Chart 1: subscribers_by_plan_donut --------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(sp.name, ''), 'Unassigned') AS plan_name, COUNT(*) AS cnt "
            "FROM user_subscriptions us "
            "LEFT JOIN subscription_plans sp ON sp.id = us.subscription_plan_id "
            "WHERE us.is_active = TRUE "
            "  AND ($1 = '' OR LOWER(sp.name) LIKE '%' || $1 || '%' OR LOWER(sp.plan_type) = $1) "
            "GROUP BY 1 ORDER BY cnt DESC",
            planFilter);

        Json::Value items = Json::arrayValue;
        for (const auto &row : rows) {
            Json::Value item(Json::objectValue);
            item["name"] = asString(row, "plan_name");
            item["value"] = jsonLong(asLong(row, "cnt"));
            items.append(item);
        }
        builder.addChart(ReportResponseBuilder::makeDonutChart(
            "subscribers_by_plan_donut", "Subscribers by Plan", items));
    }

    // ---- Chart 2: mrr_vs_headcount_bar (bar_line) --------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(sp.name, ''), 'Unassigned') AS plan_name, "
            "       COUNT(*) AS subscribers, "
            "       COALESCE(SUM(COALESCE(us.plan_fee, 0)::NUMERIC), 0) AS mrr "
            "FROM user_subscriptions us "
            "LEFT JOIN subscription_plans sp ON sp.id = us.subscription_plan_id "
            "WHERE us.is_active = TRUE "
            "  AND ($1 = '' OR LOWER(sp.name) LIKE '%' || $1 || '%' OR LOWER(sp.plan_type) = $1) "
            "GROUP BY 1 ORDER BY mrr DESC",
            planFilter);

        Json::Value labels = Json::arrayValue;
        Json::Value headcount = Json::arrayValue;
        Json::Value mrr = Json::arrayValue;
        for (const auto &row : rows) {
            labels.append(asString(row, "plan_name"));
            headcount.append(jsonLong(asLong(row, "subscribers")));
            mrr.append(jsonDouble(asDouble(row, "mrr")));
        }
        Json::Value series = Json::arrayValue;
        series.append(ReportResponseBuilder::makeSeries("Subscribers", "bar", headcount));
        series.append(ReportResponseBuilder::makeSeries("MRR (GHS)", "line", mrr, 1));
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "mrr_vs_headcount_bar", "MRR vs Headcount by Plan", "bar_line",
            labels, series, "Subscribers", "MRR (GHS)", false));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("planCode", "Plan Code")
        .addColumn("planName", "Plan")
        .addColumn("billingCycle", "Billing")
        .addColumn("unitPrice", "Unit Price (GHS)", "currency", "right")
        .addColumn("subscribers", "Subscribers", "number", "right")
        .addColumn("mrrGhs", "MRR (GHS)", "currency", "right")
        .addColumn("status", "Status", "badge");

    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(sp.id::text, 'unassigned') AS plan_code, "
            "       COALESCE(NULLIF(sp.name, ''), 'Unassigned') AS plan_name, "
            "       COALESCE(NULLIF(us.billing_cycle, ''), 'N/A') AS billing_cycle, "
            "       ROUND(AVG(NULLIF(COALESCE(us.plan_fee, 0)::NUMERIC, 0)), 2) AS unit_price, "
            "       COUNT(*) AS subscribers, "
            "       COALESCE(SUM(COALESCE(us.plan_fee, 0)::NUMERIC), 0) AS mrr "
            "FROM user_subscriptions us "
            "LEFT JOIN subscription_plans sp ON sp.id = us.subscription_plan_id "
            "WHERE us.is_active = TRUE "
            "  AND ($1 = '' OR LOWER(sp.name) LIKE '%' || $1 || '%' OR LOWER(sp.plan_type) = $1) "
            "GROUP BY 1, 2, 3 ORDER BY mrr DESC",
            planFilter);

        for (const auto &row : rows) {
            Json::Value item(Json::objectValue);
            item["planCode"] = asString(row, "plan_code");
            item["planName"] = asString(row, "plan_name");
            item["billingCycle"] = asString(row, "billing_cycle");
            item["unitPrice"] = jsonDouble(asDouble(row, "unit_price"));
            item["subscribers"] = jsonLong(asLong(row, "subscribers"));
            item["mrrGhs"] = jsonDouble(asDouble(row, "mrr"));
            item["status"] = "Active";
            builder.addRow(item);
        }
    }

    co_return builder.buildResult();
}

// ---------------------------------------------------------------------------
// Endpoint 2.3 — POST /api/v1/reports/subscriber-retention-cohorts
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildSubscriberRetentionCohortsReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/subscriber-retention-cohorts");

    // Cohort retention matrix: cohort month x month-offset -> retained %.
    struct CohortRow {
        std::string cohort;      // "2026-05-01"
        long long initial = 0;
        std::map<int, double> retention; // offset -> pct
        double ltv = 0.0;
    };
    std::vector<CohortRow> cohorts;

    {
        const auto rows = co_await dbClient->execSqlCoro(
            "WITH cohort_subs AS ("
            "  SELECT date_trunc('month', us.start_date)::date AS cohort, "
            "         us.id AS sub_id, us.end_date, us.cancelled_at "
            "  FROM user_subscriptions us "
            "  LEFT JOIN subscription_plans sp ON sp.id = us.subscription_plan_id "
            "  WHERE us.start_date >= $1::date AND us.start_date < ($2::date + INTERVAL '1 day') "
            "    AND ($3 = '' OR LOWER(sp.name) LIKE '%' || $3 || '%' OR LOWER(sp.plan_type) = $3) "
            "), offsets AS ("
            "  SELECT generate_series(0, 11) AS off "
            ") "
            "SELECT to_char(c.cohort, 'YYYY-MM-DD') AS cohort, "
            "       o.off AS month_offset, "
            "       COUNT(*) AS initial_count, "
            "       COUNT(*) FILTER (WHERE (c.cancelled_at IS NULL "
            "                                OR c.cancelled_at > c.cohort + make_interval(months => o.off)) "
            "                        AND c.end_date > c.cohort + make_interval(months => o.off)) AS retained "
            "FROM cohort_subs c "
            "CROSS JOIN offsets o "
            "WHERE c.cohort + make_interval(months => o.off) <= LEAST(NOW(), $2::date::timestamp + INTERVAL '1 month') "
            "GROUP BY 1, 2 ORDER BY 1 ASC, 2 ASC",
            filter.startDate, filter.endDate, filter.plan.value_or(""));

        for (const auto &row : rows) {
            const std::string cohort = asString(row, "cohort");
            const int offset = (int)asLong(row, "month_offset");
            const long long initial = asLong(row, "initial_count");
            const long long retained = asLong(row, "retained");

            CohortRow *target = nullptr;
            for (auto &c : cohorts) {
                if (c.cohort == cohort) {
                    target = &c;
                    break;
                }
            }
            if (target == nullptr) {
                CohortRow c;
                c.cohort = cohort;
                cohorts.push_back(c);
                target = &cohorts.back();
            }
            if (offset == 0) {
                target->initial = initial;
            }
            target->retention[offset] = pct((double)retained, (double)initial);
        }

        // LTV per cohort (total settled revenue / initial subscribers).
        const auto ltvRows = co_await dbClient->execSqlCoro(
            "SELECT to_char(date_trunc('month', us.start_date), 'YYYY-MM-DD') AS cohort, "
            "       COUNT(DISTINCT us.id) AS initial_count, "
            "       COALESCE(SUM(p.amount_paid::NUMERIC), 0) AS revenue "
            "FROM user_subscriptions us "
            "LEFT JOIN payments p ON p.user_id = us.user_id AND p.status = 'Success' "
            "WHERE us.start_date >= $1::date AND us.start_date < ($2::date + INTERVAL '1 day') "
            "GROUP BY 1 ORDER BY 1 ASC",
            filter.startDate, filter.endDate);
        for (const auto &row : ltvRows) {
            const std::string cohort = asString(row, "cohort");
            const double revenue = asDouble(row, "revenue");
            const long long initial = asLong(row, "initial_count");
            for (auto &c : cohorts) {
                if (c.cohort == cohort) {
                    c.ltv = initial > 0 ? round2(revenue / (double)initial) : 0.0;
                }
            }
        }
    }

    // ---- Summary (weighted M1 / M3 / M6 retention, avg tenure) -------------
    {
        double m1Sum = 0.0;
        double m3Sum = 0.0;
        double m6Sum = 0.0;
        long m1Count = 0;
        long m3Count = 0;
        long m6Count = 0;
        for (const auto &c : cohorts) {
            if (c.retention.count(1)) {
                m1Sum += c.retention.at(1);
                ++m1Count;
            }
            if (c.retention.count(3)) {
                m3Sum += c.retention.at(3);
                ++m3Count;
            }
            if (c.retention.count(6)) {
                m6Sum += c.retention.at(6);
                ++m6Count;
            }
        }

        const auto tenureRows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(AVG(EXTRACT(EPOCH FROM ("
            "         COALESCE(us.cancelled_at, us.end_date, NOW()) - us.start_date"
            "       )) / 2592000.0), 0) AS avg_tenure_months "
            "FROM user_subscriptions us "
            "WHERE us.start_date >= $1::date AND us.start_date < ($2::date + INTERVAL '1 day')",
            filter.startDate, filter.endDate);

        builder.addSummary("m1RetentionPct", "M1 Retention",
                           m1Count > 0 ? m1Sum / m1Count : 0.0, "percent");
        builder.addSummary("m3RetentionPct", "M3 Retention",
                           m3Count > 0 ? m3Sum / m3Count : 0.0, "percent");
        builder.addSummary("m6RetentionPct", "M6 Retention",
                           m6Count > 0 ? m6Sum / m6Count : 0.0, "percent");
        builder.addSummary("avgTenureMonths", "Avg Tenure (Months)",
                           asDouble(tenureRows[0], "avg_tenure_months"), "number");
    }

    // ---- Chart: cohort_retention_heatmap -----------------------------------
    {
        Json::Value xLabels = Json::arrayValue;
        for (int i = 0; i < 12; ++i) {
            xLabels.append("Month " + std::to_string(i));
        }
        Json::Value yLabels = Json::arrayValue;
        Json::Value matrix = Json::arrayValue;
        for (size_t y = 0; y < cohorts.size(); ++y) {
            yLabels.append(monthLabel(cohorts[y].cohort));
            for (const auto &kv : cohorts[y].retention) {
                Json::Value cell = Json::arrayValue;
                cell.append(kv.first);                         // x index (month offset)
                cell.append(static_cast<int>(y));              // y index (cohort)
                cell.append(jsonDouble(kv.second));            // retention %
                matrix.append(cell);
            }
        }
        builder.addChart(ReportResponseBuilder::makeHeatmapChart(
            "cohort_retention_heatmap", "Monthly Signup Cohort Retention Matrix (%)",
            xLabels, yLabels, matrix));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("cohortMonth", "Cohort")
        .addColumn("initialSubscribers", "Initial Subscribers", "number", "right")
        .addColumn("m1Pct", "M1", "percent", "right")
        .addColumn("m3Pct", "M3", "percent", "right")
        .addColumn("ltvGhs", "LTV (GHS)", "currency", "right");

    for (const auto &c : cohorts) {
        Json::Value item(Json::objectValue);
        item["cohortMonth"] = monthLabel(c.cohort);
        item["initialSubscribers"] = jsonLong(c.initial);
        item["m1Pct"] = jsonDouble(c.retention.count(1) ? c.retention.at(1) : 0.0);
        item["m3Pct"] = jsonDouble(c.retention.count(3) ? c.retention.at(3) : 0.0);
        item["ltvGhs"] = jsonDouble(c.ltv);
        builder.addRow(item);
    }

    co_return builder.buildResult();
}

} // namespace gnp::services
