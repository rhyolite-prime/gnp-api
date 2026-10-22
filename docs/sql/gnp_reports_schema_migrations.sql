-- ============================================================================
-- Graphic NewsPlus — Reports Platform Schema Migrations
-- Companion DDL for docs/REPORTS_IMPLEMENTATION_PLAN_AND_API_SPEC.md (§1.4)
--
-- Target   : PostgreSQL 16 (+ pgvector)
-- Safety   : Every statement is idempotent (IF NOT EXISTS / guarded DO blocks)
--            so this file can be re-run at any time without side effects.
--
-- Contents :
--   PART A — Non-breaking column additions on existing tables (Phase 0)
--   PART B — New event / ledger / telemetry tables (Phase 2 & 3)
--   PART C — Composite & analytical indexes (Phase 4)
--   PART D — daily_platform_kpi_snapshots refresh function (Phase 4 cron)
-- ============================================================================

BEGIN;

-- ============================================================================
-- PART A — EXISTING-TABLE COLUMN ADDITIONS (ALTER TABLE, non-breaking)
-- ============================================================================

-- ----------------------------------------------------------------------------
-- A.1 public.payments — richer transaction telemetry for `payment_transactions`,
--     `sales_revenue` and the single-copy funnel reports.
--     (payment_type / gateway_fee / net_amount / newspaper_id already exist in
--      production; ADD COLUMN IF NOT EXISTS keeps this migration re-runnable.)
-- ----------------------------------------------------------------------------
ALTER TABLE public.payments ADD COLUMN IF NOT EXISTS payment_type   VARCHAR(32)  DEFAULT 'subscription';
ALTER TABLE public.payments ADD COLUMN IF NOT EXISTS gateway_fee    NUMERIC(12,2) DEFAULT 0;
ALTER TABLE public.payments ADD COLUMN IF NOT EXISTS net_amount     NUMERIC(12,2);
ALTER TABLE public.payments ADD COLUMN IF NOT EXISTS failure_reason VARCHAR(120);
ALTER TABLE public.payments ADD COLUMN IF NOT EXISTS newspaper_id   UUID;

-- ----------------------------------------------------------------------------
-- A.2 public.user_subscriptions — renewal / churn lifecycle columns for
--     `subscription_renewals` and `subscriber_retention_cohorts`.
-- ----------------------------------------------------------------------------
ALTER TABLE public.user_subscriptions ADD COLUMN IF NOT EXISTS auto_renew          BOOLEAN DEFAULT TRUE;
ALTER TABLE public.user_subscriptions ADD COLUMN IF NOT EXISTS cancelled_at        TIMESTAMPTZ;
ALTER TABLE public.user_subscriptions ADD COLUMN IF NOT EXISTS cancellation_reason VARCHAR(120);
ALTER TABLE public.user_subscriptions ADD COLUMN IF NOT EXISTS dunning_recovered   BOOLEAN DEFAULT FALSE;

-- ----------------------------------------------------------------------------
-- A.3 public.ingestion_jobs — OCR pipeline telemetry for `ingestion_jobs`.
-- ----------------------------------------------------------------------------
ALTER TABLE public.ingestion_jobs ADD COLUMN IF NOT EXISTS page_count          INT DEFAULT 0;
ALTER TABLE public.ingestion_jobs ADD COLUMN IF NOT EXISTS articles_extracted  INT DEFAULT 0;
ALTER TABLE public.ingestion_jobs ADD COLUMN IF NOT EXISTS ocr_confidence_avg  NUMERIC(5,2);
ALTER TABLE public.ingestion_jobs ADD COLUMN IF NOT EXISTS started_at          TIMESTAMPTZ;
ALTER TABLE public.ingestion_jobs ADD COLUMN IF NOT EXISTS completed_at        TIMESTAMPTZ;

-- Backfill page_count from the existing total_pages counter where present.
DO $$
BEGIN
    IF EXISTS (SELECT 1 FROM information_schema.columns
               WHERE table_schema = 'public' AND table_name = 'ingestion_jobs'
                 AND column_name = 'total_pages') THEN
        UPDATE public.ingestion_jobs
           SET page_count = COALESCE(total_pages, 0)
         WHERE COALESCE(page_count, 0) = 0 AND COALESCE(total_pages, 0) > 0;
    END IF;
END $$;

-- ----------------------------------------------------------------------------
-- A.4 public.commercial_partner_api_keys — key posture columns for
--     `api_key_audit`. (allowed_ips already exists; ip_whitelist is an alias
--      view column kept for parity with the spec.)
-- ----------------------------------------------------------------------------
ALTER TABLE public.commercial_partner_api_keys ADD COLUMN IF NOT EXISTS key_prefix   VARCHAR(32);
ALTER TABLE public.commercial_partner_api_keys ADD COLUMN IF NOT EXISTS environment  VARCHAR(32) DEFAULT 'Production';
ALTER TABLE public.commercial_partner_api_keys ADD COLUMN IF NOT EXISTS expires_at   TIMESTAMPTZ;
ALTER TABLE public.commercial_partner_api_keys ADD COLUMN IF NOT EXISTS revoked_at   TIMESTAMPTZ;

-- Backfill key_prefix from the first 12 characters of client_id.
UPDATE public.commercial_partner_api_keys
   SET key_prefix = LEFT(COALESCE(client_id, ''), 12)
 WHERE key_prefix IS NULL;

-- ----------------------------------------------------------------------------
-- A.5 public.archive_search_and_rag_logs — pgvector similarity telemetry so
--     `archive_rag_intelligence` can report AVG(1 - (q <=> emb)).
-- ----------------------------------------------------------------------------
ALTER TABLE public.archive_search_and_rag_logs ADD COLUMN IF NOT EXISTS avg_similarity NUMERIC(6,4);

-- ============================================================================
-- PART B — NEW TABLES (CREATE TABLE IF NOT EXISTS)
-- ============================================================================

-- ----------------------------------------------------------------------------
-- B.1 subscription_lifecycle_events — unlocks `subscription_renewals`,
--     `subscriber_retention_cohorts`.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS public.subscription_lifecycle_events (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    subscription_id UUID,
    user_id         UUID,
    partner_id      UUID,
    from_plan_id    UUID,
    to_plan_id      UUID,
    event_type      VARCHAR(40) NOT NULL,      -- signup|renewal|upgrade|downgrade|churn|dunning_started|dunning_recovered|reactivation
    mrr_delta       NUMERIC(14,2) DEFAULT 0,
    payment_id      UUID,
    reason_code     VARCHAR(100),
    metadata        JSONB DEFAULT '{}'::jsonb,
    occurred_at     TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- ----------------------------------------------------------------------------
-- B.2 edition_sales_ledger — timestamped single-copy sales; unlocks
--     `single_copy_paywall_funnel`, `newspaper_engagement`, `sales_revenue`.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS public.edition_sales_ledger (
    id                UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    newspaper_id      UUID NOT NULL,
    publication_id    UUID NOT NULL,
    user_id           UUID,
    buyer_email       VARCHAR(255),
    is_guest_purchase BOOLEAN NOT NULL DEFAULT FALSE,
    payment_id        UUID,
    affiliate_id      UUID,
    unit_price        NUMERIC(12,2) NOT NULL DEFAULT 0,
    discount_applied  NUMERIC(12,2) NOT NULL DEFAULT 0,
    amount_paid       NUMERIC(12,2) NOT NULL DEFAULT 0,
    sold_at           TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- ----------------------------------------------------------------------------
-- B.3 campaign_recipient_events — unlocks `campaign_performance`.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS public.campaign_recipient_events (
    id                 UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    campaign_id        UUID NOT NULL,
    user_id            UUID,
    channel            VARCHAR(30) NOT NULL,   -- email|sms|push
    event_type         VARCHAR(30) NOT NULL,   -- delivered|opened|clicked|converted|bounced|unsubscribed
    payment_id         UUID,
    attributed_revenue NUMERIC(14,2) DEFAULT 0,
    occurred_at        TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- ----------------------------------------------------------------------------
-- B.4 affiliate_referral_clicks — unlocks `affiliate_performance`.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS public.affiliate_referral_clicks (
    id             UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    affiliate_id   UUID NOT NULL,
    referral_code  VARCHAR(30) NOT NULL,
    visitor_hash   CHAR(64) NOT NULL,
    landing_path   VARCHAR(350),
    traffic_source VARCHAR(50) NOT NULL DEFAULT 'direct',
    device_type    VARCHAR(20) NOT NULL DEFAULT 'unknown',
    country_code   CHAR(2),
    converted      BOOLEAN NOT NULL DEFAULT FALSE,
    commission_id  UUID,
    clicked_at     TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- ----------------------------------------------------------------------------
-- B.5 web_push_subscriptions + push_broadcasts — unlock
--     `push_notification_analytics` and the Push Studio dashboard.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS public.web_push_subscriptions (
    id               UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id          UUID,
    endpoint         TEXT NOT NULL UNIQUE,
    p256dh_key       TEXT NOT NULL,
    auth_secret      TEXT NOT NULL,
    browser          VARCHAR(40),
    os               VARCHAR(40),
    device_type      VARCHAR(20) DEFAULT 'unknown',
    user_agent       TEXT,
    opted_in_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    last_seen_at     TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    failure_count    INT NOT NULL DEFAULT 0,
    is_active        BOOLEAN NOT NULL DEFAULT TRUE,
    pruned_at        TIMESTAMPTZ
);

CREATE TABLE IF NOT EXISTS public.push_broadcasts (
    id               UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    title            VARCHAR(255) NOT NULL,
    body             TEXT,
    topic            VARCHAR(60) NOT NULL DEFAULT 'general',
    category         VARCHAR(60) NOT NULL DEFAULT 'general',
    urgency          VARCHAR(10) NOT NULL DEFAULT 'normal',   -- very-low|low|normal|high
    deep_link        VARCHAR(350),
    target_segment   VARCHAR(60) NOT NULL DEFAULT 'all_subscribers',
    status           VARCHAR(20) NOT NULL DEFAULT 'SCHEDULED',-- SCHEDULED|SENDING|SENT|FAILED|CANCELLED
    scheduled_for    TIMESTAMPTZ,
    sent_at          TIMESTAMPTZ,
    target_count     INT NOT NULL DEFAULT 0,
    delivered_count  INT NOT NULL DEFAULT 0,
    clicked_count    INT NOT NULL DEFAULT 0,
    failed_count     INT NOT NULL DEFAULT 0,
    created_by       UUID,
    created_at       TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- ----------------------------------------------------------------------------
-- B.6 archive_search_and_rag_logs — unlocks `archive_rag_intelligence`.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS public.archive_search_and_rag_logs (
    id                 UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id            UUID,
    institution_id     UUID,
    query_mode         VARCHAR(30) NOT NULL,   -- keyword|search|rag
    query_text         TEXT NOT NULL DEFAULT '',
    target_decade      SMALLINT,
    publication_id     UUID,
    results_count      INT DEFAULT 0,
    citations_returned SMALLINT DEFAULT 0,
    cited_detail_ids   UUID[],
    latency_ms         INT DEFAULT 0,
    credits_consumed   INT DEFAULT 0,
    created_at         TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- ----------------------------------------------------------------------------
-- B.7 institutional_archive_accounts + archive_credit_transactions — unlock
--     `institutional_archive_credits`.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS public.institutional_archive_accounts (
    id                  UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    institution_name    VARCHAR(255) NOT NULL,
    sector              VARCHAR(60) NOT NULL DEFAULT 'Academic',
    contact_email       VARCHAR(255) NOT NULL,
    ip_ranges           TEXT[],
    seat_limit          INT NOT NULL DEFAULT 0,
    credits_allocated   INT NOT NULL DEFAULT 0,
    credits_remaining   INT NOT NULL DEFAULT 0,
    contract_value_ghs  NUMERIC(14,2) NOT NULL DEFAULT 0,
    status              VARCHAR(20) NOT NULL DEFAULT 'Active',  -- Active|Suspended|Expired
    expires_at          TIMESTAMPTZ NOT NULL,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS public.archive_credit_transactions (
    id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    institution_id  UUID NOT NULL REFERENCES public.institutional_archive_accounts(id) ON DELETE CASCADE,
    user_id         UUID,
    tx_type         VARCHAR(30) NOT NULL,     -- topup|consume|export|refund|adjustment
    credits_delta   INT NOT NULL,             -- positive = credit, negative = debit
    balance_after   INT NOT NULL DEFAULT 0,
    reference_note  VARCHAR(255),
    created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- ----------------------------------------------------------------------------
-- B.8 audios + media_playback_events — unlock `multimedia_originals_performance`.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS public.audios (
    id               UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    title            VARCHAR(500) NOT NULL,
    series_name      VARCHAR(255),
    description      TEXT,
    file_path        VARCHAR(1000) NOT NULL,
    duration_seconds INT NOT NULL DEFAULT 0,
    status           INT NOT NULL DEFAULT 1,   -- constants::StatusTypes (1 = PUBLISHED)
    plays            INT NOT NULL DEFAULT 0,
    likes            INT NOT NULL DEFAULT 0,
    created_by       UUID,
    created_at       TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS public.media_playback_events (
    id                 UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    media_type         VARCHAR(20) NOT NULL,   -- video|audio|tts|originals
    media_id           UUID NOT NULL,
    originals_category VARCHAR(40),
    user_id            UUID,
    watch_seconds      INT DEFAULT 0,
    completion_pct     NUMERIC(5,2) DEFAULT 0,
    device_type        VARCHAR(20) DEFAULT 'unknown',
    played_at          TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- ----------------------------------------------------------------------------
-- B.9 support_tickets — unlocks `support_tickets_sla`.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS public.support_tickets (
    id                UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    ticket_number     VARCHAR(20) NOT NULL UNIQUE,
    subject           VARCHAR(255) NOT NULL,
    description       TEXT NOT NULL DEFAULT '',
    user_id           UUID,
    partner_id        UUID,
    category          VARCHAR(50) NOT NULL DEFAULT 'General',
    priority          VARCHAR(20) NOT NULL DEFAULT 'Normal',
    status            VARCHAR(20) NOT NULL DEFAULT 'Open',    -- Open|Pending|Active|Resolved|Closed
    assigned_admin_id UUID,
    first_response_at TIMESTAMPTZ,
    sla_due_at        TIMESTAMPTZ NOT NULL,
    resolved_at       TIMESTAMPTZ,
    csat_rating       SMALLINT,                               -- 1..5
    created_at        TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at        TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- ----------------------------------------------------------------------------
-- B.10 security_audit_logs — unlocks `security_audit_logs`.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS public.security_audit_logs (
    id          UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    actor_id    UUID,
    actor_name  VARCHAR(160) NOT NULL DEFAULT 'system',
    action      VARCHAR(80) NOT NULL,         -- LOGIN_FAILED|ROLE_CHANGED|INVOICE_EMAIL_DISPATCHED|API_KEY_REVOKED|...
    module      VARCHAR(60) NOT NULL DEFAULT 'general',
    resource    VARCHAR(255),
    severity    VARCHAR(10) NOT NULL DEFAULT 'info',   -- info|warn|critical
    ip_address  VARCHAR(64),
    user_agent  TEXT,
    status      VARCHAR(20) NOT NULL DEFAULT 'Active', -- Active = allowed, Blocked = denied
    metadata    JSONB DEFAULT '{}'::jsonb,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- ----------------------------------------------------------------------------
-- B.11 daily_platform_kpi_snapshots — fast executive trend rollups (Phase 4).
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS public.daily_platform_kpi_snapshots (
    snapshot_date            DATE PRIMARY KEY,
    gross_revenue_ghs        NUMERIC(14,2) NOT NULL DEFAULT 0,
    net_revenue_ghs          NUMERIC(14,2) NOT NULL DEFAULT 0,
    mrr_ghs                  NUMERIC(14,2) NOT NULL DEFAULT 0,
    active_b2c_subscribers   INT NOT NULL DEFAULT 0,
    active_b2b_partner_seats INT NOT NULL DEFAULT 0,
    new_signups              INT NOT NULL DEFAULT 0,
    churned_subscriptions    INT NOT NULL DEFAULT 0,
    edition_views            INT NOT NULL DEFAULT 0,
    single_copies_sold       INT NOT NULL DEFAULT 0,
    archive_rag_queries      INT NOT NULL DEFAULT 0,
    game_sessions_completed  INT NOT NULL DEFAULT 0,
    partner_api_requests     INT NOT NULL DEFAULT 0,
    breakdown_json           JSONB NOT NULL DEFAULT '{}'::jsonb,
    computed_at              TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- ============================================================================
-- PART C — COMPOSITE & ANALYTICAL INDEXES (Phase 4 performance hardening)
-- ============================================================================

CREATE INDEX IF NOT EXISTS idx_payments_created_status          ON public.payments (created_at, status);
CREATE INDEX IF NOT EXISTS idx_payments_type_created            ON public.payments (payment_type, created_at);
CREATE INDEX IF NOT EXISTS idx_payments_partner_created         ON public.payments (partner_id, created_at);
CREATE INDEX IF NOT EXISTS idx_user_subscriptions_active_end    ON public.user_subscriptions (is_active, end_date);
CREATE INDEX IF NOT EXISTS idx_user_subscriptions_start         ON public.user_subscriptions (start_date);
CREATE INDEX IF NOT EXISTS idx_user_subscriptions_cancelled     ON public.user_subscriptions (cancelled_at);
CREATE INDEX IF NOT EXISTS idx_lifecycle_events_occurred        ON public.subscription_lifecycle_events (occurred_at, event_type);
CREATE INDEX IF NOT EXISTS idx_lifecycle_events_subscription    ON public.subscription_lifecycle_events (subscription_id, occurred_at);
CREATE INDEX IF NOT EXISTS idx_edition_sales_sold_at            ON public.edition_sales_ledger (sold_at);
CREATE INDEX IF NOT EXISTS idx_edition_sales_newspaper          ON public.edition_sales_ledger (newspaper_id, sold_at);
CREATE INDEX IF NOT EXISTS idx_edition_sales_publication        ON public.edition_sales_ledger (publication_id, sold_at);
CREATE INDEX IF NOT EXISTS idx_newspaper_engagement_viewed      ON public.newspaper_engagement (viewed_at);
CREATE INDEX IF NOT EXISTS idx_newspaper_engagement_newspaper   ON public.newspaper_engagement (newspaper_id, viewed_at);
CREATE INDEX IF NOT EXISTS idx_publication_reads_read_at        ON public.publication_reads (read_at);
CREATE INDEX IF NOT EXISTS idx_publication_reads_partner        ON public.publication_reads (partner_id, read_at);
CREATE INDEX IF NOT EXISTS idx_campaign_events_occurred         ON public.campaign_recipient_events (occurred_at, event_type);
CREATE INDEX IF NOT EXISTS idx_campaign_events_campaign         ON public.campaign_recipient_events (campaign_id, event_type);
CREATE INDEX IF NOT EXISTS idx_affiliate_clicks_clicked         ON public.affiliate_referral_clicks (clicked_at);
CREATE INDEX IF NOT EXISTS idx_affiliate_clicks_affiliate       ON public.affiliate_referral_clicks (affiliate_id, clicked_at);
CREATE INDEX IF NOT EXISTS idx_affiliate_commissions_created    ON public.affiliate_commissions (created_at, status);
CREATE INDEX IF NOT EXISTS idx_affiliate_payouts_created        ON public.affiliate_payouts (created_at, status);
CREATE INDEX IF NOT EXISTS idx_web_push_active                  ON public.web_push_subscriptions (is_active);
CREATE INDEX IF NOT EXISTS idx_push_broadcasts_sent             ON public.push_broadcasts (sent_at);
CREATE INDEX IF NOT EXISTS idx_push_broadcasts_status           ON public.push_broadcasts (status, scheduled_for);
CREATE INDEX IF NOT EXISTS idx_archive_logs_created             ON public.archive_search_and_rag_logs (created_at, query_mode);
CREATE INDEX IF NOT EXISTS idx_archive_logs_decade              ON public.archive_search_and_rag_logs (target_decade);
CREATE INDEX IF NOT EXISTS idx_archive_credit_tx_created        ON public.archive_credit_transactions (created_at, tx_type);
CREATE INDEX IF NOT EXISTS idx_archive_credit_tx_institution    ON public.archive_credit_transactions (institution_id, created_at);
CREATE INDEX IF NOT EXISTS idx_media_playback_played            ON public.media_playback_events (played_at, media_type);
CREATE INDEX IF NOT EXISTS idx_support_tickets_created          ON public.support_tickets (created_at, status);
CREATE INDEX IF NOT EXISTS idx_security_audit_created           ON public.security_audit_logs (created_at, module);
CREATE INDEX IF NOT EXISTS idx_partner_api_logs_created         ON public.partner_api_request_logs (created_at);
CREATE INDEX IF NOT EXISTS idx_partner_api_logs_partner         ON public.partner_api_request_logs (partner_id, created_at);
CREATE INDEX IF NOT EXISTS idx_partner_invoices_created         ON public.partner_invoices (created_at, status);
CREATE INDEX IF NOT EXISTS idx_ingestion_jobs_created           ON public.ingestion_jobs (created_at, status);
CREATE INDEX IF NOT EXISTS idx_game_sessions_start              ON public.game_sessions (start_time, game_type);
CREATE INDEX IF NOT EXISTS idx_coupon_usage_used_at             ON public.coupon_usage (used_at);
CREATE INDEX IF NOT EXISTS idx_users_created_country            ON public.users (created_at, country);

-- ============================================================================
-- PART D — DAILY KPI SNAPSHOT REFRESH (midnight cron target, Phase 4)
-- Schedule with pg_cron (or the Quartz scheduler) as:
--   SELECT cron.schedule('gnp-kpi-snapshot', '5 0 * * *',
--                        $$SELECT public.gnp_refresh_daily_kpi_snapshot(CURRENT_DATE - 1)$$);
-- ============================================================================

CREATE OR REPLACE FUNCTION public.gnp_refresh_daily_kpi_snapshot(p_day DATE)
RETURNS VOID
LANGUAGE plpgsql
AS $$
DECLARE
    v_day_start TIMESTAMPTZ := p_day::TIMESTAMPTZ;
    v_day_end   TIMESTAMPTZ := (p_day + INTERVAL '1 day')::TIMESTAMPTZ;
BEGIN
    INSERT INTO public.daily_platform_kpi_snapshots AS s (
        snapshot_date, gross_revenue_ghs, net_revenue_ghs, mrr_ghs,
        active_b2c_subscribers, active_b2b_partner_seats, new_signups,
        churned_subscriptions, edition_views, single_copies_sold,
        archive_rag_queries, game_sessions_completed, partner_api_requests,
        breakdown_json, computed_at)
    SELECT
        p_day,
        COALESCE((SELECT SUM(COALESCE(p.gross_amount, p.amount_paid, 0)::NUMERIC)
                    FROM public.payments p
                   WHERE p.status = 'Success'
                     AND p.created_at >= v_day_start AND p.created_at < v_day_end), 0),
        COALESCE((SELECT SUM(COALESCE(p.net_amount, p.amount_paid, 0)::NUMERIC)
                    FROM public.payments p
                   WHERE p.status = 'Success'
                     AND p.created_at >= v_day_start AND p.created_at < v_day_end), 0),
        COALESCE((SELECT SUM(COALESCE(us.plan_fee, 0)::NUMERIC)
                    FROM public.user_subscriptions us
                   WHERE us.is_active = TRUE AND us.partner_id IS NULL
                     AND us.start_date <= v_day_end
                     AND (us.end_date IS NULL OR us.end_date >= v_day_start)), 0),
        COALESCE((SELECT COUNT(*) FROM public.user_subscriptions us
                   WHERE us.is_active = TRUE AND us.partner_id IS NULL
                     AND us.start_date <= v_day_end
                     AND (us.end_date IS NULL OR us.end_date >= v_day_start)), 0),
        COALESCE((SELECT COUNT(*) FROM public.users u
                   WHERE u.partner_id IS NOT NULL AND u.is_active = TRUE), 0),
        COALESCE((SELECT COUNT(*) FROM public.users u
                   WHERE u.created_at >= v_day_start AND u.created_at < v_day_end), 0),
        COALESCE((SELECT COUNT(*) FROM public.subscription_lifecycle_events e
                   WHERE e.event_type = 'churn'
                     AND e.occurred_at >= v_day_start AND e.occurred_at < v_day_end), 0),
        COALESCE((SELECT COUNT(*) FROM public.newspaper_engagement ne
                   WHERE ne.viewed_at >= v_day_start AND ne.viewed_at < v_day_end), 0),
        COALESCE((SELECT COUNT(*) FROM public.edition_sales_ledger l
                   WHERE l.sold_at >= v_day_start AND l.sold_at < v_day_end), 0),
        COALESCE((SELECT COUNT(*) FROM public.archive_search_and_rag_logs a
                   WHERE a.created_at >= v_day_start AND a.created_at < v_day_end), 0),
        COALESCE((SELECT COUNT(*) FROM public.game_sessions g
                   WHERE g.is_complete = TRUE
                     AND g.start_time >= v_day_start AND g.start_time < v_day_end), 0),
        COALESCE((SELECT COUNT(*) FROM public.partner_api_request_logs l
                   WHERE l.created_at >= v_day_start AND l.created_at < v_day_end), 0),
        '{}'::JSONB,
        NOW()
    ON CONFLICT (snapshot_date) DO UPDATE SET
        gross_revenue_ghs        = EXCLUDED.gross_revenue_ghs,
        net_revenue_ghs          = EXCLUDED.net_revenue_ghs,
        mrr_ghs                  = EXCLUDED.mrr_ghs,
        active_b2c_subscribers   = EXCLUDED.active_b2c_subscribers,
        active_b2b_partner_seats = EXCLUDED.active_b2b_partner_seats,
        new_signups              = EXCLUDED.new_signups,
        churned_subscriptions    = EXCLUDED.churned_subscriptions,
        edition_views            = EXCLUDED.edition_views,
        single_copies_sold       = EXCLUDED.single_copies_sold,
        archive_rag_queries      = EXCLUDED.archive_rag_queries,
        game_sessions_completed  = EXCLUDED.game_sessions_completed,
        partner_api_requests     = EXCLUDED.partner_api_requests,
        computed_at              = NOW();
END;
$$;

COMMIT;
