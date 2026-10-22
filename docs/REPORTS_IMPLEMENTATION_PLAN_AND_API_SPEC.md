# Graphic NewsPlus — Complete Reports Implementation Plan & Backend API Specification

> **Frontend Repository:** `rhyolite-prime/gnp` (`pages/admin/reports/index.vue`, `services/reports.ts`, `utils/reportChartOption.js`, `mocks/reports/*.js`)  
> **Backend Repository:** `rhyolite-prime/gnp-api` (C++20 · Drogon HTTP Framework · PostgreSQL 16 + `pgvector` · Redis)  
> **Scope:** All **27 Platform Reports across 8 Report Groups** + **3 Supporting Endpoints** (30 total endpoints)

---

## Table of Contents

1. [Part I — Architectural Design & Phased Implementation Plan](#part-i--architectural-design--phased-implementation-plan)
   - [1.1 Taxonomy of the 6 Different Report Archetypes](#11-taxonomy-of-the-6-different-report-archetypes)
   - [1.2 Shared Backend Infrastructure in `gnp-api` (Build Once, Reuse Everywhere)](#12-shared-backend-infrastructure-in-gnp-api-build-once-reuse-everywhere)
   - [1.3 Critical Bugs to Fix First in Existing `gnp-api` Code](#13-critical-bugs-to-fix-first-in-existing-gnp-api-code)
   - [1.4 Database Schema Migrations Required (`ALTER TABLE` + New Tables)](#14-database-schema-migrations-required-alter-table--new-tables)
   - [1.5 Six-Phase Backend Rollout & Frontend Cutover Plan](#15-six-phase-backend-rollout--frontend-cutover-plan)
2. [Part II — Complete Request Parameters & Response Payload Contracts (All 30 Endpoints)](#part-ii--complete-request-parameters--response-payload-contracts-all-30-endpoints)
   - [2.1 Standard API Envelope & Common TypeScript / JSON Contracts](#21-standard-api-envelope--common-typescript--json-contracts)
   - [2.2 Exact Chart Object Schemas Consumed by Apache ECharts (`utils/reportChartOption.js`)](#22-exact-chart-object-schemas-consumed-by-apache-echarts-utilsreportchartoptionjs)
   - [2.3 Group 1: Revenue & Financial Endpoints (5 Reports + Invoice Email Dispatch)](#23-group-1-revenue--financial-endpoints-5-reports--invoice-email-dispatch)
   - [2.4 Group 2: User & Subscriber Endpoints (3 Reports)](#24-group-2-user--subscriber-endpoints-3-reports)
   - [2.5 Group 3: Content & Newspaper Endpoints (4 Reports)](#25-group-3-content--newspaper-endpoints-4-reports)
   - [2.6 Group 4: Commercial Partner B2B Endpoints (3 Reports + Partner Directory)](#26-group-4-commercial-partner-b2b-endpoints-3-reports--partner-directory)
   - [2.7 Group 5: Marketing & Campaigns Endpoints (5 Reports)](#27-group-5-marketing--campaigns-endpoints-5-reports)
   - [2.8 Group 6: Historical Archives & AI RAG Endpoints (2 Reports)](#28-group-6-historical-archives--ai-rag-endpoints-2-reports)
   - [2.9 Group 7: Interactive Games & Puzzles Endpoints (1 Report)](#29-group-7-interactive-games--puzzles-endpoints-1-report)
   - [2.10 Group 8: Admin & Security Audit Endpoints (4 Reports)](#210-group-8-admin--security-audit-endpoints-4-reports)
   - [2.11 Supporting Push Studio Dashboard Endpoint (`/api/v1/push/dashboard`)](#211-supporting-push-studio-dashboard-endpoint-apiv1pushdashboard)
   - [2.12 Master Endpoint Reference Matrix](#212-master-endpoint-reference-matrix)

---

# Part I — Architectural Design & Phased Implementation Plan

## 1.1 Taxonomy of the 6 Different Report Archetypes

Although `/admin/reports` renders all 27 reports through a unified, clean report viewer shell (`pages/admin/reports/index.vue`), the underlying backend SQL engines fall into **6 distinct engineering archetypes** with different query patterns, indexing needs, and caching strategies:

| Archetype | Reports in This Archetype | Primary Visuals | Query Pattern & Backend Engineering Strategy |
|---|---|---|---|
| **A. Financial & Ledger Time-Series** | `sales_revenue`, `payment_transactions`, `subscription_renewals`, `coupon_usage`, `affiliate_payout_reconciliation` | `stacked_bar`, `bar_line`, `donut`, `horizontal_bar` + currency ledger table | `NUMERIC(14,2)` aggregation in SQL (never `FLOAT`), `DATE_TRUNC('day'\|'month', paid_at)` bucketing, parallel window comparison (`[startDate - N days, startDate - 1 day]`) for `changePct`, composite indexes on `(paid_at, status)`. |
| **B. Printable B2B Statutory Tax Invoice** | `partner_invoices` + `partner-invoice-email` | Custom printable A4 `INVOICE` sheet + `stacked_bar` + `donut` + `jsPDF` export | Deterministic invoice number (`INV-GNP-YYYY-NNNN`), seat count × contracted rate + overage line items, exact Ghana statutory tax formula (`totalDue = subtotal + vat(15%) + getflNhil(5%) - discount`), async SMTP HTML dispatch. |
| **C. Conversion Funnels & Cohort Matrices** | `single_copy_paywall_funnel`, `subscriber_retention_cohorts`, `subscriber_demographics`, `plan_distribution` | `funnel`, `heatmap`, `stacked_bar`, `donut` | Conditional `COUNT(DISTINCT session_id) FILTER (WHERE stage = ...)` SQL funnel steps; self-joined monthly signup cohort CTE outputting `[monthOffset, cohortIndex, pct]` triples for Apache ECharts `heatmap`. |
| **D. Content, OCR Pipeline & AI RAG Telemetry** | `newspaper_engagement`, `content_inventory`, `ingestion_jobs`, `multimedia_originals_performance`, `archive_rag_intelligence`, `institutional_archive_credits`, `games_engagement_retention` | Partner quota cards + `bar_line`, `area_line`, `horizontal_bar`, `donut` | Timestamped read/purchase joins (`reading_history` + `edition_sales_ledger`) instead of mutable row counters; `pgvector` similarity telemetry (`AVG(1 - (q <=> emb))`); institutional credit ledger running balances. |
| **E. Multi-Channel Attribution & Push Telemetry** | `campaign_performance`, `affiliate_performance`, `push_notification_analytics` | `bar_line`, `horizontal_bar`, `donut` | Unified channel filter (`email \| sms \| push`); referral click-to-paid conversion join (`affiliate_referral_clicks` $\rightarrow$ `affiliate_commissions`); W3C VAPID broadcast delivery & click-through aggregation. |
| **F. Point-in-Time Posture, Audit & SLA Reports** | `partner_engagement`, `partner_quota`, `partner_api_usage`, `admin_roles`, `api_key_audit`, `security_audit_logs`, `support_tickets_sla` | `stacked_bar`, `bar_line`, `horizontal_bar`, `donut` + badge tables | Live relational joins for current state (`admin_roles`, `partner_quota`, `api_key_audit`); PostgreSQL ordered-set aggregates (`PERCENTILE_CONT(0.95) WITHIN GROUP (ORDER BY latency_ms)` and `PERCENTILE_CONT(0.50)` median reply minutes). |

---

## 1.2 Shared Backend Infrastructure in `gnp-api` (Build Once, Reuse Everywhere)

Before implementing individual report queries in `rhyolite-prime/gnp-api`, build **three small shared C++ components** so every single endpoint requires only its SQL query and row mapping:

### 1. Unified Request Parser (`dto/ReportDto.h`)
Upgrade `dto/ReportDto.h` so every endpoint accepts **both** `POST` JSON bodies and `GET` query parameters seamlessly:

```cpp
#pragma once
#include <drogon/HttpRequest.h>
#include <string>
#include <optional>

namespace gnp::dto {

struct ReportFilterDto {
    std::string startDate;                        // YYYY-MM-DD (defaults to TODAY - 29 days)
    std::string endDate;                          // YYYY-MM-DD (defaults to TODAY)
    std::string prevStartDate;                    // Auto-computed equal-length prior window start
    std::string prevEndDate;                      // Auto-computed equal-length prior window end
    std::optional<std::string> partnerId;         // UUID or slug of commercial partner
    std::optional<std::string> publicationId;     // UUID of Graphic publication
    std::optional<std::string> affiliateId;       // UUID of affiliate account
    std::optional<std::string> status;            // Normalized: omits "all" -> std::nullopt
    std::optional<std::string> plan;              // "standard" | "premium" | "corporate" (omits "all")
    std::optional<std::string> channel;           // "email" | "sms" | "push" (omits "all")
    int pageNo = 1;
    int pageSize = 100;

    static ReportFilterDto fromRequest(const drogon::HttpRequestPtr& req);
};

} // namespace gnp::dto
```

> **Key Rule:** When the frontend sends `"all"` or `""` for `status`, `plan`, or `channel` (or omits them), `ReportFilterDto::fromRequest` normalizes them to `std::nullopt` so SQL uses clean parameterized predicates:  
> `WHERE ($3::text IS NULL OR LOWER(status) = LOWER($3))`

### 2. Unified JSON Response Builder (`utils/ReportResponseBuilder.h`)
All 26 standard reports (everything except `partner_invoices`, which returns the printable invoice object + `charts`) share **one identical JSON contract**:

```cpp
namespace gnp::utils {

class ReportResponseBuilder {
public:
    ReportResponseBuilder& setEndpoint(const std::string& path);
    ReportResponseBuilder& addSummary(
        const std::string& key,
        const std::string& label,
        double value,
        const std::string& format,   // "currency" | "number" | "percent"
        double changePct);
    ReportResponseBuilder& addChart(const Json::Value& chartObj);
    ReportResponseBuilder& addColumn(
        const std::string& key,
        const std::string& label,
        const std::string& format = "text",   // "text" | "number" | "currency" | "percent" | "badge"
        const std::string& align = "left");   // "left" | "right"
    ReportResponseBuilder& addRow(const Json::Value& rowObj);
    ReportResponseBuilder& setPartners(const Json::Value& partnersArray); // Used on newspaper_engagement
    Json::Value buildEnvelope(const std::string& message = "Report generated successfully.") const;
};

} // namespace gnp::utils
```

### 3. Short-TTL Redis Report Cache (`utils/ReportCache.h`)
- Compute deterministic cache key: `gnp:report:<slug>:sha256(<normalizedFilterJson>)`.
- Cache heavy analytical responses in Redis for **`120 seconds`** (`bypassCache = true` for `partner_invoices` email dispatch and real-time audit logs).

---

## 1.3 Critical Bugs to Fix First in Existing `gnp-api` Code

Auditing `rhyolite-prime/gnp-api` identified **5 existing backend defects** that directly affect report accuracy and must be fixed in Phase 0:

1. **`controllers/ReportsController.cc:51` (`sendPartnerInvoiceEmail`):**
   - **Bug:** Calls `commercialPartnerService_->getPartnerInvoiceGenerationReport(dto)` instead of `commercialPartnerService_->sendPartnerInvoiceByMail(dto)`.
   - **Fix:** Dispatch the actual email via `sendPartnerInvoiceByMail(dto)` and return `{ "sent": true, "invoiceNo": "...", "recipientEmail": "..." }`.
2. **`services/CommercialPartnerService.cc:3679` (Partner Invoice Total Formula):**
   - **Bug:** Computes `double totalDue = totalInvoiceAmount + vat;` while ignoring `+ getflNhil - discount`.
   - **Fix:** `double totalDue = subtotal + vat + getflNhil - discount;` (15% VAT + 5% GETFund/NHIL − discount).
3. **`services/CommercialPartnerService.cc` (`getPartnerAnalyticsCharts`):**
   - **Bug:** Queries `publications.title`, which does not exist in `public.publications` (column is `publications.name`).
   - **Fix:** Replace `p.title` with `p.name`.
4. **`services/NewspaperService.cc:1485` (`getNewspaperEngagementReport`):**
   - **Bug:** Rejects requests when `partnerId` is empty and groups cumulative `newspapers.views` / `newspapers.sales` counters by `publication_date`.
   - **Fix:** Make `partnerId` optional for admin callers and aggregate timestamped events from `reading_history` and `edition_sales_ledger`.
5. **`services/AffiliateService.cc:1506` (`getAffiliateCommissions`):**
   - **Bug:** Queries non-existent `FROM commissions` and `affiliates.name`.
   - **Fix:** Query `FROM public.affiliate_commissions ac JOIN public.affiliates a ON a.id = ac.affiliate_id` using `(a.first_name || ' ' || a.last_name) AS affiliate_name`.

---

## 1.4 Database Schema Migrations Required (`ALTER TABLE` + New Tables)

Full executable DDL is already checked into [`docs/sql/gnp_reports_schema_migrations.sql`](sql/gnp_reports_schema_migrations.sql):

### Existing-Table Column Additions (`ALTER TABLE` — Non-Breaking)
1. **`public.payments`**: Add `payment_type VARCHAR(32) DEFAULT 'subscription'`, `gateway_fee NUMERIC(12,2) DEFAULT 0`, `net_amount NUMERIC(12,2)`, `failure_reason VARCHAR(120)`, `newspaper_id UUID`.
2. **`public.user_subscriptions`**: Add `auto_renew BOOLEAN DEFAULT TRUE`, `cancelled_at TIMESTAMPTZ`, `cancellation_reason VARCHAR(120)`, `dunning_recovered BOOLEAN DEFAULT FALSE`.
3. **`public.ingestion_jobs`**: Add `page_count INT DEFAULT 0`, `articles_extracted INT DEFAULT 0`, `ocr_confidence_avg NUMERIC(5,2)`, `started_at TIMESTAMPTZ`, `completed_at TIMESTAMPTZ`.
4. **`public.commercial_partner_api_keys`**: Add `key_prefix VARCHAR(32)`, `environment VARCHAR(32) DEFAULT 'Production'`, `ip_whitelist TEXT[]`, `expires_at TIMESTAMPTZ`, `revoked_at TIMESTAMPTZ`.

### New Tables Required (`CREATE TABLE`)
| New Table | Unlocks Report(s) |
|---|---|
| `public.subscription_lifecycle_events` | `subscription_renewals`, `subscriber_retention_cohorts` |
| `public.edition_sales_ledger` | `single_copy_paywall_funnel`, `newspaper_engagement`, `sales_revenue` |
| `public.campaign_recipient_events` | `campaign_performance` |
| `public.affiliate_referral_clicks` | `affiliate_performance` |
| `public.web_push_subscriptions` & `public.push_broadcasts` | `push_notification_analytics` & Push Studio (`/admin/push-notifications`) |
| `public.archive_search_and_rag_logs` | `archive_rag_intelligence` |
| `public.institutional_archive_accounts` & `public.archive_credit_transactions` | `institutional_archive_credits` |
| `public.audios` & `public.media_playback_events` | `multimedia_originals_performance` |
| `public.support_tickets` | `support_tickets_sla` |
| `public.security_audit_logs` | `security_audit_logs` |
| `public.daily_platform_kpi_snapshots` | Fast `<15ms` multi-month executive trend rollups |

---

## 1.5 Six-Phase Backend Rollout & Frontend Cutover Plan

Because `services/reports.ts` encapsulates **every single report behind its own async function**, the backend and frontend teams can ship **incrementally one phase (or even one report) at a time** with zero downtime:

| Phase | Deliverables | Reports Unlocked |
|---|---|---|
| **Phase 0 — Foundation & Bug Fixes (Days 1–2)** | Land `ReportFilterDto`, `ReportResponseBuilder`, fix all 5 existing `gnp-api` bugs, run `ALTER TABLE` statements on `payments`, `user_subscriptions`, `ingestion_jobs`, `commercial_partner_api_keys`. | Unblocks all downstream phases + fixes live Partner Invoice email & totals immediately. |
| **Phase 1 — Zero-New-Table Core Reports (Days 3–6)** | Implement all reports powered purely by **already existing tables** (`payments`, `users`, `user_profiles`, `countries`, `user_subscriptions`, `subscription_plans`, `publications`, `newspapers`, `magazines`, `articles`, `ingestion_jobs`, `commercial_partners`, `commercial_partner_sub_accounts`, `commercial_partner_api_logs`, `commercial_partner_api_keys`, `coupons`, `coupon_redemptions`, `affiliate_payouts`, `puzzles`, `puzzle_attempts`, `admin_users`, `roles`). | **16 endpoints live immediately:** `sales_revenue`, `payment_transactions`, `partner_invoices`, `partner-invoice-email`, `subscriber_demographics`, `plan_distribution`, `newspaper_engagement`, `content_inventory`, `ingestion_jobs`, `partner_engagement`, `partner_quota`, `partner_api_usage`, `coupon_usage`, `affiliate_payout_reconciliation`, `games_engagement_retention`, `admin_roles`, `api_key_audit`. |
| **Phase 2 — Lifecycle, Funnel & Attribution Tables (Days 7–10)** | Create & instrument `subscription_lifecycle_events`, `edition_sales_ledger`, `campaign_recipient_events`, `affiliate_referral_clicks` hooks inside `PaymentService`, `SubscriptionService`, `CampaignService`, `AffiliateService`. | **5 endpoints live:** `subscription_renewals`, `single_copy_paywall_funnel`, `subscriber_retention_cohorts`, `campaign_performance`, `affiliate_performance`. |
| **Phase 3 — Push, Multimedia, Archives & Governance Tables (Days 11–14)** | Create & instrument `web_push_subscriptions`, `push_broadcasts`, `audios`, `media_playback_events`, `archive_search_and_rag_logs`, `institutional_archive_accounts`, `archive_credit_transactions`, `security_audit_logs`, `support_tickets`. | **7 endpoints live:** `push_notification_analytics` (+ Push Studio `/api/v1/push/*`), `multimedia_originals_performance`, `archive_rag_intelligence`, `institutional_archive_credits`, `security_audit_logs`, `support_tickets_sla`. |
| **Phase 4 — Performance Hardening & Caching (Days 15–16)** | Enable composite indexes, Redis 120s query cache, and midnight cron writing to `daily_platform_kpi_snapshots`. | Sub-50ms p95 across 1-year date windows. |
| **Phase 5 — Frontend Cutover (`services/reports.ts`, Day 17)** | Replace each `getMockReportData(...)` call in `services/reports.ts` with `const res = await apiPost('/reports/<slug>', dto); return res.result;` behind runtime flag or per-endpoint cutover. | **Zero changes needed in `pages/admin/reports/index.vue` or `utils/reportChartOption.js`!** |

---

# Part II — Complete Request Parameters & Response Payload Contracts (All 30 Endpoints)

## 2.1 Standard API Envelope & Common TypeScript / JSON Contracts

### Standard Top-Level HTTP Response Envelope (All `gnp-api` Endpoints)
Every endpoint returns HTTP `200 OK` (with `Content-Type: application/json`) wrapped in the standard Graphic NewsPlus API envelope:

```ts
interface BaseApiResponse<T> {
  success: boolean;              // true on HTTP 200
  message: string;               // Human-readable status message
  unAuthorizedRequest: boolean;  // true only on 401/403
  error: { code: number; message: string } | null;
  result: T;                     // <--- Extracted directly into `activeReportData` or `invoiceData`
}
```

### Standard Report Request Body / Query Parameters (`ReportFilterDto`)
All report endpoints accept `POST /api/v1/reports/<slug>` with JSON body (recommended) **and** optionally `GET /api/v1/reports/<slug>` with identical query string parameters:

```ts
interface ReportFilterDto {
  startDate?: string;       // ISO date "YYYY-MM-DD", e.g. "2026-09-23"
  endDate?: string;         // ISO date "YYYY-MM-DD", e.g. "2026-10-22"
  partnerId?: string;       // Commercial partner ID, e.g. "p-mtn-001" or UUID (required ONLY for partner-invoices)
  status?: string;          // "active" | "inactive" | "pending" (omit or ignore if "all")
  plan?: string;            // "standard" | "premium" | "corporate" (omit or ignore if "all")
  channel?: string;         // "email" | "sms" | "push" (omit or ignore if "all")
  publicationId?: string;   // Optional publication UUID filter
}
```

### Standard Report `result` Payload Schema (Used by 26 of 27 Reports)
```ts
interface StandardReportPayload {
  endpoint: string;                // Canonical API path, e.g. "/api/v1/reports/sales-revenue"
  summary: SummaryMetric[];        // Rendered as the 4 KPI summary cards at top of viewer
  charts: ReportChart[];           // Rendered by Apache ECharts via `utils/reportChartOption.js`
  columns: ReportColumn[];         // Table header definitions + cell formatter instructions
  rows: Record<string, any>[];     // Plain array of row objects keyed by `column.key` (also exported to CSV!)
  partners?: PartnerQuotaSummary[]; // Optional — rendered as progress bars on `newspaper_engagement`
}

interface SummaryMetric {
  key: string;                     // Unique camelCase metric identifier
  label: string;                   // Card label displayed in UI
  value: number;                   // Raw numeric value (NEVER pre-formatted as a string!)
  format: 'currency' | 'number' | 'percent';
  // How the frontend formats `value`:
  // - 'currency' -> "GHS 312,480.00"
  // - 'percent'  -> "87.6%"   (send 87.6, NOT 0.876!)
  // - 'number'   -> "6,842"
  changePct?: number;              // Signed % delta vs equal prior period, e.g. 18.4 or -7.4
}

interface ReportColumn {
  key: string;                     // Exact property key to read from each object in `rows[]`
  label: string;                   // Column header text (also used as CSV column header!)
  align?: 'left' | 'right';        // Default 'left'; use 'right' for numbers, currency, percentages
  format?: 'currency' | 'number' | 'percent' | 'badge';
  // Cell formatting rules in `pages/admin/reports/index.vue`:
  // - omitted     -> Plain string (or "—" if null/empty)
  // - 'currency'  -> "GHS 18,720.00" (bold text)
  // - 'number'    -> "1,840"
  // - 'percent'   -> "88.1%" (send 88.1, NOT 0.881!)
  // - 'badge'     -> Colored pill badge:
  //     * Green pill (`bg-green-100 text-green-800`): strings containing "Active", "Settled", "Published", "Paid"
  //     * Yellow pill (`bg-yellow-100 text-yellow-800`): strings containing "Pending", "Scheduled"
  //     * Red pill (`bg-red-100 text-red-800`): strings containing "Failed", "Inactive", "Expired", "Revoked"
}

interface PartnerQuotaSummary {
  name: string;                    // e.g. "MTN Ghana PLC"
  enrolled: number;                // Active enrolled sub-accounts
  quota: number;                   // Contracted seat limit
}
```

---

## 2.2 Exact Chart Object Schemas Consumed by Apache ECharts (`utils/reportChartOption.js`)

`utils/reportChartOption.js` supports **7 exact `chart.type` values**. Every item in `result.charts[]` **must** conform to one of these 7 shapes:

### Shape 1: `stacked_bar` (Multi-Series Stacked Column Chart)
```json
{
  "id": "revenue_by_stream",
  "title": "Revenue Stream Trend (Last 10 Months)",
  "type": "stacked_bar",
  "span": "full",
  "labels": ["Jan", "Feb", "Mar"],
  "yAxisLabel": "GHS",
  "series": [
    { "name": "Individual Subscriptions", "data": [98400, 104200, 112500] },
    { "name": "Corporate B2B Invoices", "data": [64000, 68500, 72000] }
  ]
}
```

### Shape 2: `bar_line` (Single or Dual-Axis Bar + Smooth Line Chart)
```json
{
  "id": "daily_gateway_outcomes",
  "title": "Daily Payment Outcomes (Paystack & Hubtel)",
  "type": "bar_line",
  "span": "full",
  "labels": ["16 Oct", "17 Oct", "18 Oct"],
  "yAxisLabel": "Transactions",
  "secondaryYAxisLabel": "Success %",
  "series": [
    { "name": "Successful", "type": "bar", "data": [910, 945, 860] },
    { "name": "Success Rate %", "type": "line", "yAxisIndex": 1, "data": [95.8, 96.2, 95.4] }
  ]
}
```

### Shape 3: `area_line` (Multi-Series Smooth Area Trend Chart)
```json
{
  "id": "multimedia_format_growth",
  "title": "Weekly Consumption by Format",
  "type": "area_line",
  "span": "full",
  "labels": ["W1", "W2", "W3"],
  "yAxisLabel": "Streams / Reads",
  "series": [
    { "name": "Video Documentaries", "data": [8900, 9600, 10400] },
    { "name": "Podcasts & Audio Briefings", "data": [4800, 5350, 6100] }
  ]
}
```

### Shape 4: `donut` (Proportional Ring Chart)
```json
{
  "id": "revenue_by_plan_donut",
  "title": "Revenue Share by Subscription Plan",
  "type": "donut",
  "items": [
    { "name": "Premium All-Access", "value": 148200 },
    { "name": "Corporate Bulk Tier", "value": 96400 }
  ]
}
```

### Shape 5: `horizontal_bar` (Ranked Category Bar Chart)
```json
{
  "id": "payment_channel_bar",
  "title": "Collections by Payment Channel (GHS)",
  "type": "horizontal_bar",
  "labels": ["MTN Mobile Money", "Corporate Bank EFT", "Visa / Mastercard"],
  "series": [
    { "name": "Settled (GHS)", "data": [142800, 96400, 46920] }
  ]
}
```

### Shape 6: `funnel` (Multi-Stage Conversion Funnel)
```json
{
  "id": "paywall_conversion_funnel",
  "title": "Edition Paywall Conversion Funnel",
  "type": "funnel",
  "items": [
    { "name": "1. Preview Page Opened", "value": 48920 },
    { "name": "2. Paywall Modal Triggered", "value": 19450 },
    { "name": "3. Payment Prompt Initiated", "value": 5280 },
    { "name": "4. Single Copy Purchased", "value": 3135 }
  ]
}
```

### Shape 7: `heatmap` (2D Cohort Retention Matrix)
```json
{
  "id": "cohort_retention_heatmap",
  "title": "Monthly Signup Cohort Retention Matrix (%)",
  "type": "heatmap",
  "span": "full",
  "xLabels": ["Month 0", "Month 1", "Month 2"],
  "yLabels": ["Aug 2026", "Sep 2026", "Oct 2026"],
  "matrix": [
    [0, 0, 100], [1, 0, 89], [2, 0, 84],
    [0, 1, 100], [1, 1, 90],
    [0, 2, 100]
  ]
}
```

> **Layout Note:** Setting `"span": "full"` spans all 2 columns of the chart grid (`lg:col-span-2`). Omitting `"span"` renders two charts side by side (`lg:col-span-1`), unless there is only 1 chart in `charts[]` (which automatically spans full width).

---

## 2.3 Group 1: Revenue & Financial Endpoints (5 Reports + Invoice Email Dispatch)

### Endpoint 1.1 — Sales & Revenue Overview
- **Method & URL:** `POST /api/v1/reports/sales-revenue`
- **Frontend Caller:** `generateSalesReport(dto)`
- **Permission:** `reports.manage` or `payments.manage`
- **PostgreSQL Tables:** `payments`, `user_subscriptions`, `subscription_plans`, `commercial_partners`, `edition_sales_ledger`
- **Request Payload:**
  ```json
  {
    "startDate": "2026-09-23",
    "endDate": "2026-10-22",
    "plan": "premium"
  }
  ```
  - `startDate` (`string`, optional): `YYYY-MM-DD`
  - `endDate` (`string`, optional): `YYYY-MM-DD`
  - `plan` (`string`, optional): `"standard" | "premium" | "corporate"` (omitted when `"all"`)
- **Response `result` Payload:**
  - `summary`: `grossRevenueGhs` (`currency`), `netSettledGhs` (`currency`), `paidTransactions` (`number`), `arpuGhs` (`currency`)
  - `charts`:
    1. `revenue_by_stream` (`stacked_bar`, `span: "full"` — series: `"Individual Subscriptions"`, `"Corporate B2B Invoices"`, `"Archive Credits & Single Copy"`)
    2. `revenue_by_plan_donut` (`donut` — plan breakdown in GHS)
    3. `payment_channel_bar` (`horizontal_bar` — settled GHS per payment method)
  - `columns` & `rows[]` object schema:
    ```json
    {
      "date": "2026-10-22",
      "revenueStream": "Individual Subscriptions",
      "plan": "premium",
      "orders": 312,
      "grossAmount": 18720.0,
      "gatewayFees": 355.68,
      "netAmount": 18364.32,
      "status": "Settled"
    }
    ```

---

### Endpoint 1.2 — Payment Transactions Log
- **Method & URL:** `POST /api/v1/reports/payment-transactions`
- **Frontend Caller:** `generatePaymentTransactionsReport(dto)`
- **Permission:** `reports.manage` or `payments.manage`
- **PostgreSQL Tables:** `payments`, `users`, `user_profiles`, `subscription_plans`
- **Request Payload:**
  ```json
  {
    "startDate": "2026-09-23",
    "endDate": "2026-10-22",
    "status": "active"
  }
  ```
  - `startDate`, `endDate` (`string`, optional)
  - `status` (`string`, optional): `"active" | "inactive" | "pending"`
- **Response `result` Payload:**
  - `summary`: `totalVolumeGhs` (`currency`), `successRatePct` (`percent`), `successfulCount` (`number`), `failedOrAbandoned` (`number`)
  - `charts`:
    1. `daily_gateway_outcomes` (`bar_line`, `span: "full"` — `"Successful"` bars + `"Success Rate %"` secondary line)
    2. `payment_status_donut` (`donut` — `"Active / Settled"`, `"Pending Webhook"`, `"Failed / Declined"`)
    3. `failure_reasons_bar` (`horizontal_bar` — failure reasons count)
  - `columns` & `rows[]` object schema:
    ```json
    {
      "reference": "PSK-GNP-904821",
      "subscriber": "Kwame Mensah",
      "channel": "MTN MoMo (***4821)",
      "plan": "Premium Monthly",
      "amount": 60.0,
      "paidAt": "2026-10-22 09:42",
      "status": "Active"
    }
    ```

---

### Endpoint 1.3 — Partner Invoices (Printable A4 Invoice + Billing Charts)
- **Method & URL:** `POST /api/v1/reports/partner-invoices`
- **Frontend Caller:** `generatePartnerInvoice(dto)`
- **Permission:** `reports.manage` or `partners.invoices.view`
- **PostgreSQL Tables:** `commercial_partners`, `commercial_partner_sub_accounts`, `commercial_partner_api_logs`, `institutional_archive_accounts`
- **Request Payload:**
  ```json
  {
    "partnerId": "p-mtn-001",
    "startDate": "2026-09-23",
    "endDate": "2026-10-22",
    "status": "active"
  }
  ```
  - `partnerId` (`string`, **required**): Commercial partner UUID/ID
  - `startDate` (`string`, **required**): `YYYY-MM-DD` (`startDate <= endDate`)
  - `endDate` (`string`, **required**): `YYYY-MM-DD`
- **Exact Response `result` Payload (Special Printable Invoice Schema):**
  ```json
  {
    "invoiceNo": "INV-GNP-2026-1042",
    "partnerId": "p-mtn-001",
    "partnerName": "MTN Ghana PLC",
    "partnerAddress": "MTN House, Independence Avenue, West Ridge\nAccra, Ghana",
    "partnerEmail": "corporate.billing@mtn.com.gh",
    "items": [
      {
        "description": "Corporate Digital Newsstand Seats — Daily Graphic & Graphic Business",
        "details": "3,692 active employee seats × monthly enterprise rate",
        "quantity": 3692,
        "unitPrice": 12.5
      },
      {
        "description": "Partner Content Syndication API Overages",
        "details": "Verified feed calls above 100k monthly tier",
        "quantity": 120,
        "unitPrice": 15.0
      }
    ],
    "subtotal": 51550.0,
    "vat": 7732.5,
    "getflNhil": 2577.5,
    "discount": 2577.5,
    "totalDue": 59282.5,
    "charts": [
      {
        "id": "partner_billing_comparison",
        "title": "Partner Invoice Totals (Net vs Statutory Taxes)",
        "type": "stacked_bar",
        "labels": ["MTN Ghana", "Univ. of Ghana", "GCB Bank", "Bank of Ghana", "GNAT"],
        "yAxisLabel": "GHS",
        "series": [
          { "name": "Net Subtotal", "data": [48972.5, 30879.75, 30180.0, 13775.0, 12160.0] },
          { "name": "VAT + GETFund/NHIL (20%)", "data": [10310.0, 6501.0, 6036.0, 2755.0, 2560.0] }
        ]
      },
      {
        "id": "invoice_collection_status",
        "title": "Receivables Aging Breakdown",
        "type": "donut",
        "items": [
          { "name": "Paid on Time", "value": 112008.5 },
          { "name": "Current (Due < 14d)", "value": 36216.0 },
          { "name": "Overdue (15–30d)", "value": 15904.75 }
        ]
      }
    ]
  }
  ```

---

### Endpoint 1.3b — Send Partner Invoice via Email (Supporting Action Button)
- **Method & URL:** `POST /api/v1/reports/partner-invoice-email`
- **Frontend Caller:** `sendPartnerInvoiceAsEmail(payload)` (`Email Partner` button inside Partner Invoice viewer)
- **Permission:** `reports.manage` or `partners.invoices.view`
- **Request Payload:**
  ```json
  {
    "partnerId": "p-mtn-001",
    "startDate": "2026-09-23",
    "endDate": "2026-10-22"
  }
  ```
- **Response `result` Payload:**
  ```json
  {
    "sent": true,
    "invoiceNo": "INV-GNP-2026-1042",
    "recipientEmail": "corporate.billing@mtn.com.gh",
    "dispatchedAt": "2026-10-22T10:15:00Z"
  }
  ```

---

### Endpoint 1.4 — Subscription Renewals & Churn
- **Method & URL:** `POST /api/v1/reports/subscription-renewals`
- **Frontend Caller:** `generateSubscriptionRenewalsReport(dto)`
- **PostgreSQL Tables:** `user_subscriptions`, `subscription_plans`, `subscription_lifecycle_events` *(new)*
- **Request Params:** `{ "startDate": "YYYY-MM-DD", "endDate": "YYYY-MM-DD" }`
- **Response `result` Payload:**
  - `summary`: `renewalRatePct` (`percent`), `monthlyChurnPct` (`percent`), `netMrrAddedGhs` (`currency`), `recoveredDunning` (`number`)
  - `charts`: `mrr_bridge_trend` (`bar_line`, `span: "full"`), `churn_reasons_donut` (`donut`), `renewal_by_plan_bar` (`horizontal_bar`)
  - `rows[]` schema:
    ```json
    {
      "planName": "Premium Monthly (GHS 60)",
      "dueForRenewal": 1840,
      "renewedCount": 1621,
      "dunningRecovered": 148,
      "churnedCount": 41,
      "renewalRate": 88.1,
      "retainedMrr": 97260.0
    }
    ```

---

### Endpoint 1.5 — Single-Copy Paywall Funnel *(New Suggested Report)*
- **Method & URL:** `POST /api/v1/reports/single-copy-paywall-funnel`
- **Frontend Caller:** `generateSingleCopyFunnelReport(dto)`
- **PostgreSQL Tables:** `edition_sales_ledger` *(new)*, `newspapers`, `publications`, `payments`
- **Request Params:** `{ "startDate": "YYYY-MM-DD", "endDate": "YYYY-MM-DD" }`
- **Response `result` Payload:**
  - `summary`: `paywallViews` (`number`), `singleCopiesSold` (`number`), `conversionRatePct` (`percent`), `upsoldToPlan` (`number`)
  - `charts`: `paywall_conversion_funnel` (`funnel` — 5 stages), `single_copy_by_publication` (`horizontal_bar`)
  - `rows[]` schema:
    ```json
    {
      "publication": "Daily Graphic — Budget Preview Special",
      "issueDate": "2026-10-22",
      "previewReaders": 9420,
      "copiesSold": 742,
      "convRate": 7.9,
      "revenueGhs": 5936.0
    }
    ```

---

## 2.4 Group 2: User & Subscriber Endpoints (3 Reports)

### Endpoint 2.1 — Subscriber Demographics & Status
- **Method & URL:** `POST /api/v1/reports/subscriber-demographics`
- **Frontend Caller:** `generateSubscriberDemographicsReport(dto)`
- **PostgreSQL Tables:** `users`, `user_profiles`, `countries`, `user_subscriptions`
- **Request Params:** `{ "startDate": "YYYY-MM-DD", "endDate": "YYYY-MM-DD", "status": "active" | "inactive" | "pending" }`
- **Response `result` Payload:**
  - `summary`: `totalRegistered` (`number`), `activeAccounts` (`number`), `diasporaSharePct` (`percent`), `verifiedEmailsPct` (`percent`)
  - `charts`: `signups_monthly_growth` (`stacked_bar`, `span: "full"`), `readers_by_country_bar` (`horizontal_bar`), `account_status_donut` (`donut`)
  - `rows[]` schema:
    ```json
    {
      "country": "Ghana (Greater Accra & Ashanti)",
      "totalUsers": 18720,
      "paidSubscribers": 9840,
      "activePct": 86.4,
      "primaryAuth": "Email + OTP / Google",
      "status": "Active"
    }
    ```

---

### Endpoint 2.2 — Subscription Plan Distribution
- **Method & URL:** `POST /api/v1/reports/plan-distribution`
- **Frontend Caller:** `generatePlanDistributionReport(dto)`
- **PostgreSQL Tables:** `user_subscriptions`, `subscription_plans`, `commercial_partner_sub_accounts`
- **Request Params:** `{ "plan": "standard" | "premium" | "corporate" }`
- **Response `result` Payload:**
  - `summary`: `activePaidSeats` (`number`), `individualSeats` (`number`), `corporateSeats` (`number`), `annualSharePct` (`percent`)
  - `charts`: `subscribers_by_plan_donut` (`donut`), `mrr_vs_headcount_bar` (`bar_line`)
  - `rows[]` schema:
    ```json
    {
      "planCode": "PLN-PREM-M",
      "planName": "Premium All-6 Publications",
      "billingCycle": "Monthly",
      "unitPrice": 60.0,
      "subscribers": 4680,
      "mrrGhs": 280800.0,
      "status": "Active"
    }
    ```

---

### Endpoint 2.3 — Subscriber Retention Cohorts *(New Suggested Report)*
- **Method & URL:** `POST /api/v1/reports/subscriber-retention-cohorts`
- **Frontend Caller:** `generateSubscriberRetentionReport(dto)`
- **PostgreSQL Tables:** `user_subscriptions`, `subscription_lifecycle_events` *(new)*, `payments`
- **Request Params:** `{ "startDate": "YYYY-MM-DD", "endDate": "YYYY-MM-DD", "plan": "standard" | "premium" | "corporate" }`
- **Response `result` Payload:**
  - `summary`: `m1RetentionPct` (`percent`), `m3RetentionPct` (`percent`), `m6RetentionPct` (`percent`), `avgTenureMonths` (`number`)
  - `charts`: `cohort_retention_heatmap` (`heatmap`, `span: "full"` — `xLabels`, `yLabels`, `matrix: [[xIdx, yIdx, pct], ...]`)
  - `rows[]` schema:
    ```json
    {
      "cohortMonth": "May 2026",
      "initialSubscribers": 1660,
      "m1Pct": 86.0,
      "m3Pct": 74.0,
      "ltvGhs": 412.5
    }
    ```

---

## 2.5 Group 3: Content & Newspaper Endpoints (4 Reports)

### Endpoint 3.1 — Newspaper Engagement & Sales
- **Method & URL:** `POST /api/v1/reports/newspaper-engagement`
- **Frontend Caller:** `generateNewspaperEngagementReport(dto)`
- **PostgreSQL Tables:** `publications`, `newspapers`, `reading_history`, `edition_sales_ledger`, `commercial_partners`
- **Request Params:** `{ "startDate": "YYYY-MM-DD", "endDate": "YYYY-MM-DD" }`
- **Response `result` Payload (Includes `partners[]` Quota Progress Bars!):**
  - `partners`: `[{ "name": "MTN Ghana PLC", "enrolled": 3692, "quota": 5000 }, ...]`
  - `summary`: `totalEditionViews` (`number`), `singleCopiesSold` (`number`), `avgReadMinutes` (`number`), `audioTtsListens` (`number`)
  - `charts`: `quota_utilization_donut` (`donut`), `engagement_and_sales_7d` (`bar_line`)
  - `rows[]` schema:
    ```json
    {
      "publication": "Daily Graphic",
      "editionsPublished": 26,
      "totalViews": 94200,
      "copiesSold": 2140,
      "completionPct": 74.5,
      "revenueGhs": 17120.0
    }
    ```

---

### Endpoint 3.2 — Content Inventory Status
- **Method & URL:** `POST /api/v1/reports/content-inventory`
- **Frontend Caller:** `generateContentInventoryReport(dto)`
- **PostgreSQL Tables:** `publications`, `newspapers`, `magazines`, `articles`
- **Request Params:** `{ "status": "active" | "inactive" | "pending" }`
- **Response `result` Payload:**
  - `summary`: `totalIssuesCataloged` (`number`), `extractedArticles` (`number`), `magazinesCount` (`number`), `ocrReadyPct` (`percent`)
  - `charts`: `catalog_by_publication` (`horizontal_bar`), `inventory_status_donut` (`donut`)
  - `rows[]` schema:
    ```json
    {
      "title": "Daily Graphic",
      "frequency": "Daily (Mon–Sat)",
      "publishedCount": 7840,
      "articlesIndexed": 248900,
      "latestIssue": "2026-10-22",
      "status": "Published"
    }
    ```

---

### Endpoint 3.3 — Ingestion Job Logs
- **Method & URL:** `POST /api/v1/reports/ingestion-jobs`
- **Frontend Caller:** `generateIngestionJobLogsReport(dto)`
- **PostgreSQL Tables:** `ingestion_jobs`, `ingestion_artifacts`
- **Request Params:** `{ "startDate": "YYYY-MM-DD", "endDate": "YYYY-MM-DD", "status": "active" | "inactive" | "pending" }`
- **Response `result` Payload:**
  - `summary`: `jobsCompleted30d` (`number`), `pipelineSuccessPct` (`percent`), `avgProcessingSec` (`number`), `avgOcrConfidence` (`percent`)
  - `charts`: `ingestion_latency_trend` (`bar_line`, `span: "full"`)
  - `rows[]` schema:
    ```json
    {
      "jobId": "ING-2026-0894",
      "edition": "Daily Graphic — 22 Oct 2026",
      "pages": 48,
      "articlesExtracted": 168,
      "durationSec": 79,
      "completedAt": "2026-10-22 04:42",
      "status": "Active"
    }
    ```

---

### Endpoint 3.4 — Multimedia & Originals Performance *(New Suggested Report)*
- **Method & URL:** `POST /api/v1/reports/multimedia-originals-performance`
- **Frontend Caller:** `generateMultimediaOriginalsReport(dto)`
- **PostgreSQL Tables:** `videos`, `originals_articles`, `audios` *(new)*, `media_playback_events` *(new)*
- **Request Params:** `{ "startDate": "YYYY-MM-DD", "endDate": "YYYY-MM-DD" }`
- **Response `result` Payload:**
  - `summary`: `videoPlays` (`number`), `podcastStreams` (`number`), `originalsReads` (`number`), `avgCompletionPct` (`percent`)
  - `charts`: `multimedia_format_growth` (`area_line`, `span: "full"`)
  - `rows[]` schema:
    ```json
    {
      "title": "Graphic Business Roundtable: Cedi Stability & Export Growth",
      "format": "Video",
      "playsOrReads": 18420,
      "completionPct": 72.4,
      "subscriberConversions": 94
    }
    ```

---

## 2.6 Group 4: Commercial Partner B2B Endpoints (3 Reports + Partner Directory)

### Supporting Endpoint 4.0 — Commercial Partners Dropdown Directory
- **Method & URL:** `GET /api/v1/commercial-partners?pageNo=1&pageSize=100` *(called on mount by `pages/admin/reports/index.vue` via `getCommercialPartners({ pageNo: 1, pageSize: 100 })`)*
- **Response `result` Payload:**
  ```json
  {
    "totalCount": 5,
    "pageNo": 1,
    "pageSize": 100,
    "data": [
      {
        "id": "p-mtn-001",
        "name": "MTN Ghana PLC",
        "address": "MTN House, Independence Avenue, West Ridge\nAccra, Ghana",
        "email": "corporate.billing@mtn.com.gh",
        "quota": 5000,
        "enrolled": 3692,
        "status": "active"
      }
    ]
  }
  ```

---

### Endpoint 4.1 — Partner Sub-account Engagement
- **Method & URL:** `POST /api/v1/reports/partner-engagement`
- **Frontend Caller:** `generatePartnerEngagementReport(dto)`
- **PostgreSQL Tables:** `commercial_partners`, `commercial_partner_sub_accounts`, `reading_history`, `publications`
- **Request Params:** `{ "startDate": "YYYY-MM-DD", "endDate": "YYYY-MM-DD" }`
- **Response `result` Payload:**
  - `summary`: `activeCorporateReaders` (`number`), `totalPartnerReads` (`number`), `avgReadsPerSeat` (`number`), `seatActivationPct` (`percent`)
  - `charts`: `partner_reads_trend` (`stacked_bar`, `span: "full"`)
  - `rows[]` schema:
    ```json
    {
      "partnerName": "MTN Ghana PLC",
      "enrolledSeats": 3692,
      "monthlyActiveReaders": 3210,
      "editionReads": 45940,
      "topPublication": "Daily Graphic",
      "status": "Active"
    }
    ```

---

### Endpoint 4.2 — Partner Quota & Usage
- **Method & URL:** `POST /api/v1/reports/partner-quota`
- **Frontend Caller:** `generatePartnerQuotaReport()`
- **PostgreSQL Tables:** `commercial_partners`, `commercial_partner_sub_accounts`
- **Request Params:** `{}` *(snapshot of current quota allocations)*
- **Response `result` Payload:**
  - `summary`: `totalAllocatedSeats` (`number`), `totalEnrolledSeats` (`number`), `remainingQuota` (`number`), `overallUtilizationPct` (`percent`)
  - `charts`: `partner_quota_bars` (`stacked_bar`, `span: "full"`)
  - `rows[]` schema:
    ```json
    {
      "partnerName": "University of Ghana (Balme Library)",
      "assignedQuota": 3000,
      "enrolledSeats": 2790,
      "remainingSeats": 210,
      "utilizationPct": 93.0,
      "status": "Pending"
    }
    ```

---

### Endpoint 4.3 — Partner API Usage Stats
- **Method & URL:** `POST /api/v1/reports/partner-api-usage`
- **Frontend Caller:** `generatePartnerApiUsageReport(dto)`
- **PostgreSQL Tables:** `commercial_partner_api_logs`, `commercial_partner_api_keys`, `commercial_partners`
- **Request Params:** `{ "startDate": "YYYY-MM-DD", "endDate": "YYYY-MM-DD" }`
- **Response `result` Payload:**
  - `summary`: `totalApiRequests` (`number`), `apiSuccessRatePct` (`percent`), `p95LatencyMs` (`number`), `rateLimitedCount` (`number`)
  - `charts`: `api_calls_and_latency` (`bar_line`, `span: "full"`)
  - `rows[]` schema:
    ```json
    {
      "partnerName": "MTN Ghana PLC",
      "endpointRoute": "POST /partner/sub-accounts/sso-token",
      "requests30d": 184200,
      "avgLatencyMs": 96,
      "errorRatePct": 0.4,
      "status": "Active"
    }
    ```

---

## 2.7 Group 5: Marketing & Campaigns Endpoints (5 Reports)

### Endpoint 5.1 — Campaign Performance
- **Method & URL:** `POST /api/v1/reports/campaign-performance`
- **Frontend Caller:** `generateCampaignPerformanceReport(dto)`
- **PostgreSQL Tables:** `campaigns`, `campaign_recipient_events` *(new)*
- **Request Params:** `{ "startDate": "YYYY-MM-DD", "endDate": "YYYY-MM-DD", "channel": "email" | "sms" | "push" }`
- **Response `result` Payload:**
  - `summary`: `messagesDelivered` (`number`), `openOrViewRatePct` (`percent`), `clickThroughRatePct` (`percent`), `attributedRevenueGhs` (`currency`)
  - `charts`: `channel_performance_comparison` (`bar_line`, `span: "full"`)
  - `rows[]` schema:
    ```json
    {
      "campaignName": "2027 Budget Morning Briefing Push",
      "channel": "push",
      "delivered": 48200,
      "clicks": 8917,
      "ctrPct": 18.5,
      "revenueGhs": 19400.0,
      "status": "Active"
    }
    ```

---

### Endpoint 5.2 — Coupon Usage & Effectiveness
- **Method & URL:** `POST /api/v1/reports/coupon-usage`
- **Frontend Caller:** `generateCouponUsageReport(dto)`
- **PostgreSQL Tables:** `coupons`, `coupon_redemptions`, `payments`
- **Request Params:** `{ "startDate": "YYYY-MM-DD", "endDate": "YYYY-MM-DD", "status": "active" | "inactive" | "pending" }`
- **Response `result` Payload:**
  - `summary`: `totalRedemptions` (`number`), `discountGrantedGhs` (`currency`), `netOrderValueGhs` (`currency`), `redemptionRatePct` (`percent`)
  - `charts`: `top_coupons_bar` (`stacked_bar`, `span: "full"`)
  - `rows[]` schema:
    ```json
    {
      "code": "ANNUAL20",
      "discountRule": "20% Off Annual Plan",
      "redeemed": 456,
      "maxUses": 600,
      "netRevenueGhs": 26400.0,
      "status": "Active"
    }
    ```

---

### Endpoint 5.3 — Affiliate Referral Performance *(New Suggested Report)*
- **Method & URL:** `POST /api/v1/reports/affiliate-performance`
- **Frontend Caller:** `generateAffiliatePerformanceReport(dto)`
- **PostgreSQL Tables:** `affiliates`, `affiliate_commissions`, `affiliate_referral_clicks` *(new)*
- **Request Params:** `{ "startDate": "YYYY-MM-DD", "endDate": "YYYY-MM-DD" }`
- **Response `result` Payload:**
  - `summary`: `activeAffiliates` (`number`), `referralConversions` (`number`), `referredRevenueGhs` (`currency`), `commissionsEarnedGhs` (`currency`)
  - `charts`: `top_affiliates_bar` (`horizontal_bar`), `affiliate_traffic_sources` (`donut`)
  - `rows[]` schema:
    ```json
    {
      "affiliateName": "Tech Review GH",
      "referralCode": "TECHGH",
      "clicks": 4820,
      "conversions": 450,
      "commissionGhs": 5200.0,
      "status": "Active"
    }
    ```

---

### Endpoint 5.4 — Affiliate Payout Reconciliation *(New Suggested Report)*
- **Method & URL:** `POST /api/v1/reports/affiliate-payout-reconciliation`
- **Frontend Caller:** `generateAffiliatePayoutReport(dto)`
- **PostgreSQL Tables:** `affiliate_payouts`, `affiliates`
- **Request Params:** `{ "startDate": "YYYY-MM-DD", "endDate": "YYYY-MM-DD", "status": "active" | "inactive" | "pending" }`
- **Response `result` Payload:**
  - `summary`: `settledPayoutsGhs` (`currency`), `pendingPayoutsGhs` (`currency`), `withholdingTaxGhs` (`currency`), `momoSuccessPct` (`percent`)
  - `charts`: `payout_methods_donut` (`donut`)
  - `rows[]` schema:
    ```json
    {
      "payoutRef": "PO-AFF-2041",
      "affiliateName": "Tech Review GH",
      "rail": "MTN MoMo",
      "grossAmount": 2600.0,
      "netPaid": 2405.0,
      "status": "Settled"
    }
    ```

---

### Endpoint 5.5 — Push Notification Analytics *(New Suggested Report)*
- **Method & URL:** `POST /api/v1/reports/push-notification-analytics`
- **Frontend Caller:** `generatePushNotificationReport(dto)`
- **PostgreSQL Tables:** `web_push_subscriptions` *(new)*, `push_broadcasts` *(new)*
- **Request Params:** `{ "startDate": "YYYY-MM-DD", "endDate": "YYYY-MM-DD" }`
- **Response `result` Payload:**
  - `summary`: `subscribedBrowsers` (`number`), `pushDelivered30d` (`number`), `avgPushCtrPct` (`percent`), `prunedStaleEndpoints` (`number`)
  - `charts`: `push_ctr_by_topic` (`horizontal_bar`), `push_browser_split` (`donut`)
  - `rows[]` schema:
    ```json
    {
      "broadcastTitle": "BREAKING: Bank of Ghana Holds Policy Rate Following MPC Review",
      "topic": "Breaking News",
      "delivered": 14610,
      "clicked": 3272,
      "ctrPct": 22.4,
      "status": "Settled"
    }
    ```

---

## 2.8 Group 6: Historical Archives & AI RAG Endpoints (2 Reports)

### Endpoint 6.1 — Archive Search & AI RAG Intelligence *(New Suggested Report)*
- **Method & URL:** `POST /api/v1/reports/archive-rag-intelligence`
- **Frontend Caller:** `generateArchiveRagReport(dto)`
- **PostgreSQL Tables:** `archives`, `archive_articles`, `archive_embeddings` (`pgvector`), `archive_search_and_rag_logs` *(new)*
- **Request Params:** `{ "startDate": "YYYY-MM-DD", "endDate": "YYYY-MM-DD" }`
- **Response `result` Payload:**
  - `summary`: `archiveSearches` (`number`), `ragCitationsServed` (`number`), `avgVectorSimilarity` (`percent`), `zeroResultRatePct` (`percent`)
  - `charts`: `archive_decade_demand` (`bar_line`, `span: "full"` — 1950s through 2020s)
  - `rows[]` schema:
    ```json
    {
      "decade": "1960 – 1969 (First Republic)",
      "indexedPages": 42100,
      "keywordSearches": 3290,
      "ragQueries": 1420,
      "topQuery": "Akosombo Hydroelectric Dam Construction"
    }
    ```

---

### Endpoint 6.2 — Institutional Archive Credit Ledger *(New Suggested Report)*
- **Method & URL:** `POST /api/v1/reports/institutional-archive-credits`
- **Frontend Caller:** `generateInstitutionalArchiveCreditsReport(dto)`
- **PostgreSQL Tables:** `institutional_archive_accounts` *(new)*, `archive_credit_transactions` *(new)*
- **Request Params:** `{ "startDate": "YYYY-MM-DD", "endDate": "YYYY-MM-DD", "status": "active" | "inactive" | "pending" }`
- **Response `result` Payload:**
  - `summary`: `activeInstitutions` (`number`), `creditsConsumed` (`number`), `topupRevenueGhs` (`currency`), `clippingsExported` (`number`)
  - `charts`: `institution_credit_consumption` (`horizontal_bar`)
  - `rows[]` schema:
    ```json
    {
      "institution": "University of Ghana (Balme Library)",
      "purchasedCredits": 12000,
      "consumedCredits": 9420,
      "remainingBalance": 2580,
      "billedGhs": 14400.0,
      "status": "Active"
    }
    ```

---

## 2.9 Group 7: Interactive Games & Puzzles Endpoints (1 Report)

### Endpoint 7.1 — Daily Games & Puzzles Engagement *(New Suggested Report)*
- **Method & URL:** `POST /api/v1/reports/games-engagement-retention`
- **Frontend Caller:** `generateGamesEngagementReport(dto)`
- **PostgreSQL Tables:** `puzzles`, `puzzle_attempts`, `puzzle_leaderboards`, `user_subscriptions`
- **Request Params:** `{ "startDate": "YYYY-MM-DD", "endDate": "YYYY-MM-DD" }`
- **Response `result` Payload:**
  - `summary`: `dailyPuzzleStarts` (`number`), `completionRatePct` (`percent`), `streakHolders7d` (`number`), `churnReductionLift` (`percent`)
  - `charts`: `games_sessions_by_type` (`bar_line`, `span: "full"`)
  - `rows[]` schema:
    ```json
    {
      "gameTitle": "Graphic Daily Mini Crossword",
      "sessionsStarted": 29400,
      "completedCount": 22461,
      "completionPct": 76.4,
      "avgSolveSec": 198
    }
    ```

---

## 2.10 Group 8: Admin & Security Audit Endpoints (4 Reports)

### Endpoint 8.1 — Admin Roles & Access
- **Method & URL:** `POST /api/v1/reports/admin-roles`
- **Frontend Caller:** `generateAdminRolesReport()`
- **PostgreSQL Tables:** `admin_users`, `roles`, `permissions`, `role_permissions`
- **Request Params:** `{}`
- **Response `result` Payload:**
  - `summary`: `totalAdmins` (`number`), `ssoEnforcedPct` (`percent`), `privilegedRoles` (`number`), `dormantAccounts` (`number`)
  - `charts`: `admins_by_role_bar` (`horizontal_bar`)
  - `rows[]` schema:
    ```json
    {
      "staffName": "Kofi Boateng",
      "email": "kofi.boateng@graphic.com.gh",
      "role": "Super Admin",
      "authProvider": "Microsoft Entra SSO",
      "lastLogin": "2026-10-22 09:50",
      "status": "Active"
    }
    ```

---

### Endpoint 8.2 — Partner API Key Audit
- **Method & URL:** `POST /api/v1/reports/api-key-audit`
- **Frontend Caller:** `generateApiKeyAuditReport(dto)`
- **PostgreSQL Tables:** `commercial_partner_api_keys`, `commercial_partners`
- **Request Params:** `{ "status": "active" | "inactive" | "pending" }`
- **Response `result` Payload:**
  - `summary`: `totalKeysIssued` (`number`), `activeKeys` (`number`), `expiringSoon` (`number`), `revokedKeys` (`number`)
  - `charts`: `api_key_posture_donut` (`donut`)
  - `rows[]` schema:
    ```json
    {
      "keyPrefix": "gnp_live_mtn_9f2a••••",
      "partnerName": "MTN Ghana PLC",
      "environment": "Production",
      "lastUsedAt": "2026-10-22 09:58",
      "expiresAt": "2027-06-30",
      "status": "Active"
    }
    ```

---

### Endpoint 8.3 — Security & Admin Audit Trail *(New Suggested Report)*
- **Method & URL:** `POST /api/v1/reports/security-audit-logs`
- **Frontend Caller:** `generateSecurityAuditLogsReport(dto)`
- **PostgreSQL Tables:** `security_audit_logs` *(new)*, `admin_users`
- **Request Params:** `{ "startDate": "YYYY-MM-DD", "endDate": "YYYY-MM-DD", "status": "active" | "inactive" | "pending" }`
- **Response `result` Payload:**
  - `summary`: `privilegedEvents30d` (`number`), `roleChanges` (`number`), `invoiceOverrides` (`number`), `blockedLogins` (`number`)
  - `charts`: `audit_events_by_module` (`horizontal_bar`)
  - `rows[]` schema:
    ```json
    {
      "timestamp": "2026-10-22 09:34",
      "actor": "Ama Serwaa",
      "action": "INVOICE_EMAIL_DISPATCHED",
      "resource": "INV-GNP-2026-1042 (MTN Ghana)",
      "ipAddress": "197.251.142.18",
      "status": "Active"
    }
    ```

---

### Endpoint 8.4 — Support Tickets & SLA Report *(New Suggested Report)*
- **Method & URL:** `POST /api/v1/reports/support-tickets-sla`
- **Frontend Caller:** `generateSupportTicketsSlaReport(dto)`
- **PostgreSQL Tables:** `support_tickets` *(new)*, `users`, `admin_users`
- **Request Params:** `{ "startDate": "YYYY-MM-DD", "endDate": "YYYY-MM-DD", "status": "active" | "inactive" | "pending" }`
- **Response `result` Payload:**
  - `summary`: `ticketsOpened30d` (`number`), `slaMetPct` (`percent`), `firstResponseMin` (`number`), `csatScorePct` (`percent`)
  - `charts`: `tickets_by_category_donut` (`donut`)
  - `rows[]` schema:
    ```json
    {
      "ticketNo": "TKT-4821",
      "category": "MoMo Payment Delay",
      "subscriber": "Kofi Boateng",
      "firstReplyMin": 14,
      "assignedTo": "Esi Mensimah",
      "status": "Active"
    }
    ```

---

## 2.11 Supporting Push Studio Dashboard Endpoint (`/api/v1/push/dashboard`)

Rendered directly inside `/admin/push-notifications` (`fetchPushDashboard()` in `services/pushNotifications.ts`):
- **Method & URL:** `GET /api/v1/push/dashboard`
- **Request Params:** None
- **Response `result` Payload:**
  ```json
  {
    "vapidPublicKey": "<ECDSA-P256-URL-SAFE-BASE64>",
    "vapidSubject": "mailto:digital-alerts@graphic.com.gh",
    "summary": {
      "activeBrowserSubscriptions": 14820,
      "liveConnectedClients": 1,
      "totalBroadcastsSent": 41,
      "scheduledQueueCount": 1,
      "avgDeliveryRatePct": 98.1,
      "avgCtrPct": 17.5,
      "optInRatePct": 42.6
    },
    "charts": {
      "days": ["09 Oct", "10 Oct", "..."],
      "dailyDelivered": [24100, 26800, 25400],
      "dailyClicked": [4120, 4890, 4510],
      "dailyNewOptIns": [94, 112, 105],
      "categoryPerformance": [
        { "category": "morning_edition", "label": "Morning Edition Ready (05:30 GMT)", "sent": 14, "ctrPct": 22.4 }
      ],
      "platformBreakdown": [
        { "name": "Chrome on Android (Ghana Mobile)", "value": 6840 }
      ]
    },
    "recentBroadcasts": [
      {
        "id": "BCAST-20261022-01",
        "title": "BREAKING: Bank of Ghana Holds Policy Rate Steady Following Inflation Review",
        "body": "Read Graphic Business’ full breakdown...",
        "category": "breaking_news",
        "urgency": "high",
        "deepLink": "/newspapers",
        "targetSegment": "all_subscribers",
        "status": "SENT",
        "deliveredCount": 14510,
        "ctrPercent": 18.6,
        "sentAt": "2026-10-22T08:30:04Z"
      }
    ]
  }
  ```

---

## 2.12 Master Endpoint Reference Matrix

| # | Group | Report ID | HTTP Method & Endpoint Path | Accepted Filter Params | Rollout Phase |
|---|---|---|---|---|---|
| 1 | Financial | `sales_revenue` | `POST /api/v1/reports/sales-revenue` | `startDate`, `endDate`, `plan` | Phase 1 |
| 2 | Financial | `payment_transactions` | `POST /api/v1/reports/payment-transactions` | `startDate`, `endDate`, `status` | Phase 1 |
| 3 | Financial | `partner_invoices` | `POST /api/v1/reports/partner-invoices` | `partnerId`*, `startDate`*, `endDate`*, `status` | Phase 1 |
| 4 | Financial *(Action)* | `partner_invoice_email` | `POST /api/v1/reports/partner-invoice-email` | `partnerId`*, `startDate`*, `endDate`* | Phase 0 / 1 |
| 5 | Financial | `subscription_renewals` | `POST /api/v1/reports/subscription-renewals` | `startDate`, `endDate` | Phase 2 |
| 6 | Financial *(New)* | `single_copy_paywall_funnel` | `POST /api/v1/reports/single-copy-paywall-funnel` | `startDate`, `endDate` | Phase 2 |
| 7 | Users | `subscriber_demographics` | `POST /api/v1/reports/subscriber-demographics` | `startDate`, `endDate`, `status` | Phase 1 |
| 8 | Users | `plan_distribution` | `POST /api/v1/reports/plan-distribution` | `plan` | Phase 1 |
| 9 | Users *(New)* | `subscriber_retention_cohorts` | `POST /api/v1/reports/subscriber-retention-cohorts` | `startDate`, `endDate`, `plan` | Phase 2 |
| 10 | Content | `newspaper_engagement` | `POST /api/v1/reports/newspaper-engagement` | `startDate`, `endDate` | Phase 1 |
| 11 | Content | `content_inventory` | `POST /api/v1/reports/content-inventory` | `status` | Phase 1 |
| 12 | Content | `ingestion_jobs` | `POST /api/v1/reports/ingestion-jobs` | `startDate`, `endDate`, `status` | Phase 1 |
| 13 | Content *(New)* | `multimedia_originals_performance` | `POST /api/v1/reports/multimedia-originals-performance` | `startDate`, `endDate` | Phase 3 |
| 14 | Partners *(Lookup)* | `commercial_partners` | `GET /api/v1/commercial-partners` | `pageNo`, `pageSize` | Phase 1 (Existing) |
| 15 | Partners | `partner_engagement` | `POST /api/v1/reports/partner-engagement` | `startDate`, `endDate` | Phase 1 |
| 16 | Partners | `partner_quota` | `POST /api/v1/reports/partner-quota` | *(none)* | Phase 1 |
| 17 | Partners | `partner_api_usage` | `POST /api/v1/reports/partner-api-usage` | `startDate`, `endDate` | Phase 1 |
| 18 | Marketing | `campaign_performance` | `POST /api/v1/reports/campaign-performance` | `startDate`, `endDate`, `channel` | Phase 2 |
| 19 | Marketing | `coupon_usage` | `POST /api/v1/reports/coupon-usage` | `startDate`, `endDate`, `status` | Phase 1 |
| 20 | Marketing *(New)* | `affiliate_performance` | `POST /api/v1/reports/affiliate-performance` | `startDate`, `endDate` | Phase 2 |
| 21 | Marketing *(New)* | `affiliate_payout_reconciliation` | `POST /api/v1/reports/affiliate-payout-reconciliation` | `startDate`, `endDate`, `status` | Phase 1 |
| 22 | Marketing *(New)* | `push_notification_analytics` | `POST /api/v1/reports/push-notification-analytics` | `startDate`, `endDate` | Phase 3 |
| 23 | Archives *(New)* | `archive_rag_intelligence` | `POST /api/v1/reports/archive-rag-intelligence` | `startDate`, `endDate` | Phase 3 |
| 24 | Archives *(New)* | `institutional_archive_credits` | `POST /api/v1/reports/institutional-archive-credits` | `startDate`, `endDate`, `status` | Phase 3 |
| 25 | Games *(New)* | `games_engagement_retention` | `POST /api/v1/reports/games-engagement-retention` | `startDate`, `endDate` | Phase 1 |
| 26 | Admin | `admin_roles` | `POST /api/v1/reports/admin-roles` | *(none)* | Phase 1 |
| 27 | Admin | `api_key_audit` | `POST /api/v1/reports/api-key-audit` | `status` | Phase 1 |
| 28 | Admin *(New)* | `security_audit_logs` | `POST /api/v1/reports/security-audit-logs` | `startDate`, `endDate`, `status` | Phase 3 |
| 29 | Admin *(New)* | `support_tickets_sla` | `POST /api/v1/reports/support-tickets-sla` | `startDate`, `endDate`, `status` | Phase 3 |
| 30 | Push Studio | `push_dashboard` | `GET /api/v1/push/dashboard` | *(none)* | Phase 3 |
