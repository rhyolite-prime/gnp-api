//
// ReportingService — Group 8: Admin & Security Audit report engines
// (admin_roles, api_key_audit, security_audit_logs, support_tickets_sla)
// plus the supporting Push Studio dashboard (§2.11).
//
// See docs/REPORTS_IMPLEMENTATION_PLAN_AND_API_SPEC.md §2.10 / §2.11.
//

#include "ReportingService.h"

#include <drogon/orm/DbClient.h>
#include <drogon/orm/Result.h>

#include "ReportSqlHelpers.h"
#include "constants/ErrorCodes.h"
#include "utils/ReportResponseBuilder.h"

using drogon::orm::DrogonDbException;
using gnp::dto::BaseApiResponse;
using gnp::dto::ReportFilterDto;
using gnp::utils::ReportResponseBuilder;
using namespace gnp::services::reportdetail;

namespace gnp::services {

// ---------------------------------------------------------------------------
// Endpoint 8.1 — POST /api/v1/reports/admin-roles
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildAdminRolesReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/admin-roles");

    // ---- Summary -----------------------------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  COUNT(*) AS total_admins, "
            "  COUNT(*) FILTER (WHERE LOWER(COALESCE(u.credential_type, '')) LIKE '%passkey%' "
            "                   OR LOWER(COALESCE(u.credential_type, '')) LIKE '%sso%' "
            "                   OR LOWER(COALESCE(u.credential_type, '')) LIKE '%entra%' "
            "                   OR LOWER(COALESCE(u.credential_type, '')) LIKE '%oidc%') AS sso_enforced, "
            "  COUNT(*) FILTER (WHERE u.last_active IS NULL "
            "                   OR u.last_active < NOW() - INTERVAL '90 days') AS dormant "
            "FROM users u WHERE u.is_admin_user = TRUE");

        const auto roleRows = co_await dbClient->execSqlCoro(
            "SELECT COUNT(*) AS privileged_roles FROM roles r "
            "WHERE LOWER(COALESCE(r.name, '')) LIKE '%admin%' "
            "   OR LOWER(COALESCE(r.name, '')) LIKE '%super%' "
            "   OR LOWER(COALESCE(r.name, '')) LIKE '%manager%'");

        const double total = (double)asLong(rows[0], "total_admins");

        builder.addSummary("totalAdmins", "Total Admins", total, "number");
        builder.addSummary("ssoEnforcedPct", "SSO Enforced",
                           pct((double)asLong(rows[0], "sso_enforced"), total), "percent");
        builder.addSummary("privilegedRoles", "Privileged Roles",
                           (double)asLong(roleRows[0], "privileged_roles"), "number");
        builder.addSummary("dormantAccounts", "Dormant Accounts (90d)",
                           (double)asLong(rows[0], "dormant"), "number");
    }

    // ---- Chart: admins_by_role_bar (horizontal_bar) ------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(CASE WHEN jsonb_typeof(u.roles) = 'array' "
            "                     THEN u.roles->>0 "
            "                     WHEN jsonb_typeof(u.roles) = 'string' "
            "                     THEN u.roles #>> '{}' "
            "                     ELSE NULL END, 'Staff') AS role_name, "
            "       COUNT(*) AS cnt "
            "FROM users u WHERE u.is_admin_user = TRUE "
            "GROUP BY 1 ORDER BY cnt DESC");

        Json::Value labels = Json::arrayValue;
        Json::Value data = Json::arrayValue;
        for (const auto &row : rows) {
            labels.append(asString(row, "role_name"));
            data.append(jsonLong(asLong(row, "cnt")));
        }
        Json::Value series = Json::arrayValue;
        series.append(ReportResponseBuilder::makeSeries("Admins", "bar", data));
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "admins_by_role_bar", "Admins by Role", "horizontal_bar",
            labels, series, "Admins", "", false));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("staffName", "Staff")
        .addColumn("email", "Email")
        .addColumn("role", "Role")
        .addColumn("authProvider", "Auth Provider")
        .addColumn("lastLogin", "Last Login")
        .addColumn("status", "Status", "badge");

    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT TRIM(CONCAT(COALESCE(u.first_name, ''), ' ', COALESCE(u.last_name, ''))) AS staff_name, "
            "       COALESCE(u.email, '') AS email, "
            "       COALESCE(CASE WHEN jsonb_typeof(u.roles) = 'array' "
            "                     THEN u.roles->>0 "
            "                     WHEN jsonb_typeof(u.roles) = 'string' "
            "                     THEN u.roles #>> '{}' "
            "                     ELSE NULL END, 'Staff') AS role_name, "
            "       COALESCE(NULLIF(u.credential_type, ''), 'Email + OTP') AS auth_provider, "
            "       COALESCE(to_char(u.last_active, 'YYYY-MM-DD HH24:MI'), '—') AS last_login, "
            "       CASE WHEN u.last_active IS NULL "
            "                 OR u.last_active < NOW() - INTERVAL '90 days' THEN 'Inactive' "
            "            WHEN u.is_active THEN 'Active' "
            "            ELSE 'Inactive' END AS status "
            "FROM users u WHERE u.is_admin_user = TRUE "
            "ORDER BY u.last_active DESC NULLS LAST "
            "LIMIT $1 OFFSET $2",
            filter.pageSize, (filter.pageNo - 1) * filter.pageSize);

        for (const auto &row : rows) {
            Json::Value item(Json::objectValue);
            item["staffName"] = asString(row, "staff_name");
            item["email"] = asString(row, "email");
            item["role"] = asString(row, "role_name");
            item["authProvider"] = asString(row, "auth_provider");
            item["lastLogin"] = asString(row, "last_login");
            item["status"] = ReportResponseBuilder::normalizeBadgeStatus(asString(row, "status"));
            builder.addRow(item);
        }
    }

    co_return builder.buildResult();
}

// ---------------------------------------------------------------------------
// Endpoint 8.2 — POST /api/v1/reports/api-key-audit
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildApiKeyAuditReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/api-key-audit");

    const std::string statusFilter = filter.status.value_or("");

    // ---- Summary -----------------------------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  COUNT(*) AS total_keys, "
            "  COUNT(*) FILTER (WHERE k.revoked_at IS NULL AND k.is_active = TRUE) AS active_keys, "
            "  COUNT(*) FILTER (WHERE k.revoked_at IS NULL AND k.is_active = TRUE "
            "                   AND k.expires_at IS NOT NULL "
            "                   AND k.expires_at < NOW() + INTERVAL '30 days') AS expiring_soon, "
            "  COUNT(*) FILTER (WHERE k.revoked_at IS NOT NULL) AS revoked_keys "
            "FROM commercial_partner_api_keys k");

        builder.addSummary("totalKeysIssued", "Keys Issued", (double)asLong(rows[0], "total_keys"), "number");
        builder.addSummary("activeKeys", "Active Keys", (double)asLong(rows[0], "active_keys"), "number");
        builder.addSummary("expiringSoon", "Expiring (30d)", (double)asLong(rows[0], "expiring_soon"), "number");
        builder.addSummary("revokedKeys", "Revoked Keys", (double)asLong(rows[0], "revoked_keys"), "number");
    }

    // ---- Chart: api_key_posture_donut --------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  COUNT(*) FILTER (WHERE k.revoked_at IS NULL AND k.is_active = TRUE) AS active_keys, "
            "  COUNT(*) FILTER (WHERE k.revoked_at IS NULL AND k.is_active = FALSE) AS inactive_keys, "
            "  COUNT(*) FILTER (WHERE k.revoked_at IS NOT NULL) AS revoked_keys, "
            "  COUNT(*) FILTER (WHERE k.revoked_at IS NULL AND k.is_active = TRUE "
            "                   AND k.expires_at IS NOT NULL "
            "                   AND k.expires_at < NOW() + INTERVAL '30 days') AS expiring_soon "
            "FROM commercial_partner_api_keys k");

        Json::Value items = Json::arrayValue;
        {
            Json::Value item(Json::objectValue);
            item["name"] = "Active";
            item["value"] = jsonLong(asLong(rows[0], "active_keys"));
            items.append(item);
        }
        {
            Json::Value item(Json::objectValue);
            item["name"] = "Expiring Soon";
            item["value"] = jsonLong(asLong(rows[0], "expiring_soon"));
            items.append(item);
        }
        {
            Json::Value item(Json::objectValue);
            item["name"] = "Inactive";
            item["value"] = jsonLong(asLong(rows[0], "inactive_keys"));
            items.append(item);
        }
        {
            Json::Value item(Json::objectValue);
            item["name"] = "Revoked";
            item["value"] = jsonLong(asLong(rows[0], "revoked_keys"));
            items.append(item);
        }
        builder.addChart(ReportResponseBuilder::makeDonutChart(
            "api_key_posture_donut", "API Key Posture", items));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("keyPrefix", "Key")
        .addColumn("partnerName", "Partner")
        .addColumn("environment", "Environment")
        .addColumn("lastUsedAt", "Last Used")
        .addColumn("expiresAt", "Expires")
        .addColumn("status", "Status", "badge");

    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT LEFT(COALESCE(k.client_id, ''), 12) || '••••' AS key_prefix, "
            "       COALESCE(NULLIF(k.partner_name, ''), COALESCE(NULLIF(cp.name, ''), 'Unassigned')) AS partner_name, "
            "       COALESCE(NULLIF(k.environment, ''), 'Production') AS environment, "
            "       COALESCE(to_char(k.last_used_at, 'YYYY-MM-DD HH24:MI'), '—') AS last_used_at, "
            "       COALESCE(to_char(k.expires_at, 'YYYY-MM-DD'), '—') AS expires_at, "
            "       CASE WHEN k.revoked_at IS NOT NULL THEN 'Revoked' "
            "            WHEN k.is_active AND (k.expires_at IS NULL OR k.expires_at > NOW()) THEN 'Active' "
            "            WHEN k.expires_at IS NOT NULL AND k.expires_at <= NOW() THEN 'Expired' "
            "            ELSE 'Inactive' END AS status "
            "FROM commercial_partner_api_keys k "
            "LEFT JOIN commercial_partners cp ON cp.id = k.partner_id "
            "WHERE ($1 = '' "
            "       OR ($1 = 'active' AND k.revoked_at IS NULL AND k.is_active = TRUE) "
            "       OR ($1 = 'inactive' AND k.revoked_at IS NULL AND k.is_active = FALSE) "
            "       OR ($1 = 'pending' AND k.is_active = TRUE AND k.last_used_at IS NULL)) "
            "ORDER BY k.created_at DESC "
            "LIMIT $2 OFFSET $3",
            statusFilter, filter.pageSize, (filter.pageNo - 1) * filter.pageSize);

        for (const auto &row : rows) {
            Json::Value item(Json::objectValue);
            item["keyPrefix"] = asString(row, "key_prefix");
            item["partnerName"] = asString(row, "partner_name");
            item["environment"] = asString(row, "environment");
            item["lastUsedAt"] = asString(row, "last_used_at");
            item["expiresAt"] = asString(row, "expires_at");
            item["status"] = ReportResponseBuilder::normalizeBadgeStatus(asString(row, "status"));
            builder.addRow(item);
        }
    }

    co_return builder.buildResult();
}

// ---------------------------------------------------------------------------
// Endpoint 8.3 — POST /api/v1/reports/security-audit-logs (never cached)
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildSecurityAuditLogsReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/security-audit-logs");

    const std::string statusFilter = filter.status.value_or("");

    // ---- Summary -----------------------------------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  COUNT(*) AS privileged_events, "
            "  COUNT(*) FILTER (WHERE a.action LIKE 'ROLE_%') AS role_changes, "
            "  COUNT(*) FILTER (WHERE a.action LIKE 'INVOICE_%') AS invoice_overrides, "
            "  COUNT(*) FILTER (WHERE a.action LIKE 'LOGIN_%' AND a.status = 'Blocked') AS blocked_logins "
            "FROM security_audit_logs a "
            "WHERE a.created_at >= $1::date AND a.created_at < ($2::date + INTERVAL '1 day')",
            filter.startDate, filter.endDate);

        builder.addSummary("privilegedEvents30d", "Privileged Events",
                           (double)asLong(rows[0], "privileged_events"), "number");
        builder.addSummary("roleChanges", "Role Changes",
                           (double)asLong(rows[0], "role_changes"), "number");
        builder.addSummary("invoiceOverrides", "Invoice Actions",
                           (double)asLong(rows[0], "invoice_overrides"), "number");
        builder.addSummary("blockedLogins", "Blocked Logins",
                           (double)asLong(rows[0], "blocked_logins"), "number");
    }

    // ---- Chart: audit_events_by_module (horizontal_bar) --------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(a.module, ''), 'general') AS module, COUNT(*) AS cnt "
            "FROM security_audit_logs a "
            "WHERE a.created_at >= $1::date AND a.created_at < ($2::date + INTERVAL '1 day') "
            "GROUP BY 1 ORDER BY cnt DESC LIMIT 10",
            filter.startDate, filter.endDate);

        Json::Value labels = Json::arrayValue;
        Json::Value data = Json::arrayValue;
        for (const auto &row : rows) {
            labels.append(asString(row, "module"));
            data.append(jsonLong(asLong(row, "cnt")));
        }
        Json::Value series = Json::arrayValue;
        series.append(ReportResponseBuilder::makeSeries("Events", "bar", data));
        builder.addChart(ReportResponseBuilder::makeCartesianChart(
            "audit_events_by_module", "Audit Events by Module", "horizontal_bar",
            labels, series, "Events", "", false));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("timestamp", "Timestamp")
        .addColumn("actor", "Actor")
        .addColumn("action", "Action")
        .addColumn("resource", "Resource")
        .addColumn("ipAddress", "IP Address")
        .addColumn("status", "Status", "badge");

    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT to_char(a.created_at, 'YYYY-MM-DD HH24:MI') AS timestamp, "
            "       COALESCE(NULLIF(a.actor_name, ''), 'system') AS actor, "
            "       a.action AS action, "
            "       COALESCE(NULLIF(a.resource, ''), '—') AS resource, "
            "       COALESCE(NULLIF(a.ip_address, ''), '—') AS ip_address, "
            "       COALESCE(NULLIF(a.status, ''), 'Active') AS status "
            "FROM security_audit_logs a "
            "WHERE a.created_at >= $1::date AND a.created_at < ($2::date + INTERVAL '1 day') "
            "  AND ($3 = '' OR LOWER(a.status) = $3 "
            "       OR ($3 = 'active' AND a.status = 'Active') "
            "       OR ($3 = 'inactive' AND a.status <> 'Active') "
            "       OR ($3 = 'pending' AND a.status = 'Pending')) "
            "ORDER BY a.created_at DESC "
            "LIMIT $4 OFFSET $5",
            filter.startDate, filter.endDate, statusFilter,
            filter.pageSize, (filter.pageNo - 1) * filter.pageSize);

        for (const auto &row : rows) {
            Json::Value item(Json::objectValue);
            item["timestamp"] = asString(row, "timestamp");
            item["actor"] = asString(row, "actor");
            item["action"] = asString(row, "action");
            item["resource"] = asString(row, "resource");
            item["ipAddress"] = asString(row, "ip_address");
            item["status"] = ReportResponseBuilder::normalizeBadgeStatus(asString(row, "status"));
            builder.addRow(item);
        }
    }

    co_return builder.buildResult();
}

// ---------------------------------------------------------------------------
// Endpoint 8.4 — POST /api/v1/reports/support-tickets-sla
// ---------------------------------------------------------------------------
drogon::Task<Json::Value> ReportingService::buildSupportTicketsSlaReport(const ReportFilterDto &filter) {
    auto dbClient = drogon::app().getDbClient();
    ReportResponseBuilder builder;
    builder.setEndpoint("/api/v1/reports/support-tickets-sla");

    const std::string statusFilter = filter.status.value_or("");

    // ---- Summary (median reply minutes via PERCENTILE_CONT(0.50)) ----------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT "
            "  COUNT(*) AS tickets_opened, "
            "  COUNT(*) FILTER (WHERE t.first_response_at IS NOT NULL "
            "                   AND t.sla_due_at IS NOT NULL "
            "                   AND t.first_response_at <= t.sla_due_at) AS sla_met, "
            "  COUNT(*) FILTER (WHERE t.first_response_at IS NOT NULL) AS responded, "
            "  COALESCE(PERCENTILE_CONT(0.50) WITHIN GROUP ("
            "    ORDER BY EXTRACT(EPOCH FROM (t.first_response_at - t.created_at)) / 60.0 "
            "  ), 0) AS median_reply_minutes, "
            "  COALESCE(AVG(t.csat_rating), 0) AS avg_csat "
            "FROM support_tickets t "
            "WHERE t.created_at >= $1::date AND t.created_at < ($2::date + INTERVAL '1 day')",
            filter.startDate, filter.endDate);

        const double opened = (double)asLong(rows[0], "tickets_opened");
        const double slaMet = (double)asLong(rows[0], "sla_met");
        const double responded = (double)asLong(rows[0], "responded");
        const double avgCsat = asDouble(rows[0], "avg_csat");

        builder.addSummary("ticketsOpened30d", "Tickets Opened", opened, "number");
        builder.addSummary("slaMetPct", "SLA Met", pct(slaMet, responded), "percent");
        builder.addSummary("firstResponseMin", "First Response (Median Min)",
                           asDouble(rows[0], "median_reply_minutes"), "number");
        builder.addSummary("csatScorePct", "CSAT", pct(avgCsat, 5.0), "percent");
    }

    // ---- Chart: tickets_by_category_donut ----------------------------------
    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(t.category, ''), 'General') AS category, COUNT(*) AS cnt "
            "FROM support_tickets t "
            "WHERE t.created_at >= $1::date AND t.created_at < ($2::date + INTERVAL '1 day') "
            "GROUP BY 1 ORDER BY cnt DESC",
            filter.startDate, filter.endDate);

        Json::Value items = Json::arrayValue;
        for (const auto &row : rows) {
            Json::Value item(Json::objectValue);
            item["name"] = asString(row, "category");
            item["value"] = jsonLong(asLong(row, "cnt"));
            items.append(item);
        }
        builder.addChart(ReportResponseBuilder::makeDonutChart(
            "tickets_by_category_donut", "Tickets by Category", items));
    }

    // ---- Table -------------------------------------------------------------
    builder.addColumn("ticketNo", "Ticket")
        .addColumn("category", "Category")
        .addColumn("subscriber", "Subscriber")
        .addColumn("firstReplyMin", "First Reply (Min)", "number", "right")
        .addColumn("assignedTo", "Assigned To")
        .addColumn("status", "Status", "badge");

    {
        const auto rows = co_await dbClient->execSqlCoro(
            "SELECT COALESCE(NULLIF(t.ticket_number, ''), 'TKT-' || UPPER(LEFT(REPLACE(t.id::text, '-', ''), 6))) AS ticket_no, "
            "       COALESCE(NULLIF(t.category, ''), 'General') AS category, "
            "       COALESCE(TRIM(CONCAT(COALESCE(u.first_name, ''), ' ', COALESCE(u.last_name, ''))), 'Guest') AS subscriber, "
            "       ROUND(COALESCE(EXTRACT(EPOCH FROM (t.first_response_at - t.created_at)) / 60.0, 0), 1) AS first_reply_min, "
            "       COALESCE(TRIM(CONCAT(COALESCE(adm.first_name, ''), ' ', COALESCE(adm.last_name, ''))), 'Unassigned') AS assigned_to, "
            "       COALESCE(NULLIF(t.status, ''), 'Open') AS status "
            "FROM support_tickets t "
            "LEFT JOIN users u ON u.id = t.user_id "
            "LEFT JOIN users adm ON adm.id = t.assigned_admin_id "
            "WHERE t.created_at >= $1::date AND t.created_at < ($2::date + INTERVAL '1 day') "
            "  AND ($3 = '' OR LOWER(t.status) = $3 "
            "       OR ($3 = 'active' AND t.status IN ('Active', 'Open')) "
            "       OR ($3 = 'inactive' AND t.status IN ('Closed', 'Resolved')) "
            "       OR ($3 = 'pending' AND t.status = 'Pending')) "
            "ORDER BY t.created_at DESC "
            "LIMIT $4 OFFSET $5",
            filter.startDate, filter.endDate, statusFilter,
            filter.pageSize, (filter.pageNo - 1) * filter.pageSize);

        for (const auto &row : rows) {
            Json::Value item(Json::objectValue);
            item["ticketNo"] = asString(row, "ticket_no");
            item["category"] = asString(row, "category");
            item["subscriber"] = asString(row, "subscriber");
            item["firstReplyMin"] = jsonDouble(asDouble(row, "first_reply_min"));
            item["assignedTo"] = asString(row, "assigned_to");
            item["status"] = ReportResponseBuilder::normalizeBadgeStatus(asString(row, "status"));
            builder.addRow(item);
        }
    }

    co_return builder.buildResult();
}

// ---------------------------------------------------------------------------
// GET /api/v1/push/dashboard — Push Studio dashboard (§2.11)
// ---------------------------------------------------------------------------
drogon::Task<BaseApiResponse> ReportingService::getPushDashboard() {
    auto dbClient = drogon::app().getDbClient();
    BaseApiResponse response;

    try {
        Json::Value result(Json::objectValue);

        // VAPID public key / subject — surface via custom app config when present.
        const Json::Value &custom = drogon::app().getCustomConfig();
        result["vapidPublicKey"] = custom.isMember("vapidPublicKey")
                                       ? custom["vapidPublicKey"].asString()
                                       : std::string("");
        result["vapidSubject"] = custom.isMember("vapidSubject")
                                     ? custom["vapidSubject"].asString()
                                     : std::string("mailto:digital-alerts@graphic.com.gh");

        // ---- summary -------------------------------------------------------
        Json::Value summary(Json::objectValue);
        {
            const auto rows = co_await dbClient->execSqlCoro(
                "SELECT "
                "  (SELECT COUNT(*) FROM web_push_subscriptions s WHERE s.is_active = TRUE) AS active_subs, "
                "  (SELECT COUNT(*) FROM push_broadcasts b WHERE b.status = 'SENT') AS total_sent, "
                "  (SELECT COUNT(*) FROM push_broadcasts b "
                "    WHERE b.status IN ('SCHEDULED', 'SENDING') "
                "      AND (b.scheduled_for IS NULL OR b.scheduled_for > NOW())) AS scheduled_queue, "
                "  (SELECT COALESCE(AVG(CASE WHEN b.target_count > 0 "
                "                             THEN (b.delivered_count::NUMERIC / b.target_count) * 100 "
                "                             ELSE 0 END), 0) "
                "     FROM push_broadcasts b WHERE b.status = 'SENT') AS avg_delivery_rate, "
                "  (SELECT COALESCE(AVG(CASE WHEN b.delivered_count > 0 "
                "                             THEN (b.clicked_count::NUMERIC / b.delivered_count) * 100 "
                "                             ELSE 0 END), 0) "
                "     FROM push_broadcasts b WHERE b.status = 'SENT') AS avg_ctr, "
                "  (SELECT COUNT(*) FROM users u) AS total_users");

            const double activeSubs = (double)asLong(rows[0], "active_subs");
            const double totalUsers = (double)asLong(rows[0], "total_users");

            summary["activeBrowserSubscriptions"] = jsonLong((long long)activeSubs);
            summary["liveConnectedClients"] = jsonLong(0); // WebSocket hub count not tracked
            summary["totalBroadcastsSent"] = jsonLong(asLong(rows[0], "total_sent"));
            summary["scheduledQueueCount"] = jsonLong(asLong(rows[0], "scheduled_queue"));
            summary["avgDeliveryRatePct"] = jsonDouble(asDouble(rows[0], "avg_delivery_rate"));
            summary["avgCtrPct"] = jsonDouble(asDouble(rows[0], "avg_ctr"));
            summary["optInRatePct"] = jsonDouble(pct(activeSubs, totalUsers));
        }
        result["summary"] = summary;

        // ---- charts --------------------------------------------------------
        Json::Value charts(Json::objectValue);
        {
            const auto rows = co_await dbClient->execSqlCoro(
                "WITH days AS ("
                "  SELECT generate_series(CURRENT_DATE - INTERVAL '13 days', "
                "                         CURRENT_DATE, INTERVAL '1 day')::date AS day "
                ") "
                "SELECT to_char(d.day, 'YYYY-MM-DD') AS day, "
                "       COALESCE((SELECT SUM(b.delivered_count) FROM push_broadcasts b "
                "                  WHERE date_trunc('day', b.sent_at) = d.day), 0) AS delivered, "
                "       COALESCE((SELECT SUM(b.clicked_count) FROM push_broadcasts b "
                "                  WHERE date_trunc('day', b.sent_at) = d.day), 0) AS clicked, "
                "       COALESCE((SELECT COUNT(*) FROM web_push_subscriptions s "
                "                  WHERE date_trunc('day', s.opted_in_at) = d.day), 0) AS new_opt_ins "
                "FROM days d ORDER BY 1 ASC");

            Json::Value days = Json::arrayValue;
            Json::Value dailyDelivered = Json::arrayValue;
            Json::Value dailyClicked = Json::arrayValue;
            Json::Value dailyNewOptIns = Json::arrayValue;
            for (const auto &row : rows) {
                days.append(dayLabel(asString(row, "day")));
                dailyDelivered.append(jsonLong(asLong(row, "delivered")));
                dailyClicked.append(jsonLong(asLong(row, "clicked")));
                dailyNewOptIns.append(jsonLong(asLong(row, "new_opt_ins")));
            }
            charts["days"] = days;
            charts["dailyDelivered"] = dailyDelivered;
            charts["dailyClicked"] = dailyClicked;
            charts["dailyNewOptIns"] = dailyNewOptIns;

            const auto categoryRows = co_await dbClient->execSqlCoro(
                "SELECT COALESCE(NULLIF(b.category, ''), 'general') AS category, "
                "       COUNT(*) AS sent, "
                "       COALESCE(AVG(CASE WHEN b.delivered_count > 0 "
                "                         THEN (b.clicked_count::NUMERIC / b.delivered_count) * 100 "
                "                         ELSE 0 END), 0) AS ctr "
                "FROM push_broadcasts b "
                "WHERE b.sent_at IS NOT NULL "
                "GROUP BY 1 ORDER BY sent DESC LIMIT 10");

            Json::Value categoryPerformance = Json::arrayValue;
            for (const auto &row : categoryRows) {
                Json::Value item(Json::objectValue);
                item["category"] = asString(row, "category");
                item["label"] = asString(row, "category");
                item["sent"] = jsonLong(asLong(row, "sent"));
                item["ctrPct"] = jsonDouble(asDouble(row, "ctr"));
                categoryPerformance.append(item);
            }
            charts["categoryPerformance"] = categoryPerformance;

            const auto platformRows = co_await dbClient->execSqlCoro(
                "SELECT TRIM(CONCAT(COALESCE(NULLIF(s.browser, ''), 'Unknown'), "
                "                   CASE WHEN COALESCE(s.os, '') <> '' THEN ' on ' || s.os ELSE '' END)) "
                "           AS platform, "
                "       COUNT(*) AS value "
                "FROM web_push_subscriptions s "
                "WHERE s.is_active = TRUE "
                "GROUP BY 1 ORDER BY value DESC LIMIT 10");

            Json::Value platformBreakdown = Json::arrayValue;
            for (const auto &row : platformRows) {
                Json::Value item(Json::objectValue);
                item["name"] = asString(row, "platform");
                item["value"] = jsonLong(asLong(row, "value"));
                platformBreakdown.append(item);
            }
            charts["platformBreakdown"] = platformBreakdown;
        }
        result["charts"] = charts;

        // ---- recentBroadcasts ----------------------------------------------
        {
            const auto rows = co_await dbClient->execSqlCoro(
                "SELECT 'BCAST-' || to_char(b.created_at, 'YYYYMMDD') || '-' "
                "           || UPPER(LEFT(REPLACE(b.id::text, '-', ''), 2)) AS id, "
                "       b.title AS title, "
                "       COALESCE(NULLIF(b.body, ''), '') AS body, "
                "       COALESCE(NULLIF(b.category, ''), 'general') AS category, "
                "       COALESCE(NULLIF(b.urgency, ''), 'normal') AS urgency, "
                "       COALESCE(NULLIF(b.deep_link, ''), '') AS deep_link, "
                "       COALESCE(NULLIF(b.target_segment, ''), 'all_subscribers') AS target_segment, "
                "       COALESCE(NULLIF(b.status, ''), 'SCHEDULED') AS status, "
                "       COALESCE(b.delivered_count, 0) AS delivered_count, "
                "       CASE WHEN COALESCE(b.delivered_count, 0) > 0 "
                "            THEN ROUND((b.clicked_count::NUMERIC / b.delivered_count) * 100, 1) "
                "            ELSE 0 END AS ctr_percent, "
                "       COALESCE(to_char(b.sent_at, 'YYYY-MM-DD\"T\"HH24:MI:SS\"Z\"'), '') AS sent_at "
                "FROM push_broadcasts b "
                "ORDER BY b.created_at DESC LIMIT 10");

            Json::Value recentBroadcasts = Json::arrayValue;
            for (const auto &row : rows) {
                Json::Value item(Json::objectValue);
                item["id"] = asString(row, "id");
                item["title"] = asString(row, "title");
                item["body"] = asString(row, "body");
                item["category"] = asString(row, "category");
                item["urgency"] = asString(row, "urgency");
                item["deepLink"] = asString(row, "deep_link");
                item["targetSegment"] = asString(row, "target_segment");
                item["status"] = asString(row, "status");
                item["deliveredCount"] = jsonLong(asLong(row, "delivered_count"));
                item["ctrPercent"] = jsonDouble(asDouble(row, "ctr_percent"));
                item["sentAt"] = asString(row, "sent_at");
                recentBroadcasts.append(item);
            }
            result["recentBroadcasts"] = recentBroadcasts;
        }

        response.success = true;
        response.message = "Push dashboard retrieved successfully.";
        response.result = result;
        co_return response;
    } catch (const DrogonDbException &e) {
        response.success = false;
        response.message = "Failed to retrieve push dashboard.";
        response.error["code"] = constants::ERR_DB_QUERY;
        response.error["detail"] = e.base().what();
        co_return response;
    }
}

} // namespace gnp::services
