# Graphic News Plus (GNP) — API Feature Documentation

> **Framework:** [Drogon](https://github.com/drogonframework/drogon) (C++ async HTTP framework)  
> **Base URL Prefix:** `/api/v1`  
> **Authentication:** JWT (`JwtAuthFilter`) — applied per-route  
> **CORS allowed origins:** `http://localhost:3009`, `https://dev.graphicnewsplus.com`, `https://graphicnewsplus.com`, `https://www.graphicnewsplus.com`

---

## Table of Contents

1. [Authentication](#1-authentication)
2. [User Management](#2-user-management)
3. [Newspaper Management](#3-newspaper-management)
4. [Publications Management](#4-publications-management)
5. [Subscription Plans](#5-subscription-plans)
6. [Subscriptions & Content Access](#6-subscriptions--content-access)
7. [Payments](#7-payments)
8. [Coupons](#8-coupons)
9. [Campaigns](#9-campaigns)
10. [Commercial Partners](#10-commercial-partners)
11. [Partner API (External B2B)](#11-partner-api-external-b2b)
12. [Affiliate Marketing](#12-affiliate-marketing)
13. [Ingestion Jobs](#13-ingestion-jobs)
14. [Real-Time Notifications](#14-real-time-notifications)
15. [Admin Panel](#15-admin-panel)
16. [Security & Middleware](#16-security--middleware)

---

## 1. Authentication

**Prefix:** `/api/v1/auth`

The authentication module supports multiple sign-in strategies including classic email/OTP, password, passkeys (WebAuthn-style), and role-specific logins.

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/check-account-status` | Verify whether an account exists and its current status |
| `GET` | `/send-otp` | Send a one-time password to a user's registered email/phone |
| `POST` | `/verify-otp` | Validate the OTP sent to the user |
| `POST` | `/set-password` | Set or reset a user's password |
| `POST` | `/login` | Standard username & password sign-in |
| `POST` | `/login-via-pass-keys` | Authenticate using registered passkeys (WebAuthn) |
| `POST` | `/register-pass-keys` | Register new passkeys for a user |
| `POST` | `/admin-login` | Dedicated admin portal sign-in |
| `POST` | `/affiliate-login` | Sign-in endpoint for affiliate accounts |

---

## 2. User Management

**Prefix:** `/api/v1/users`

Full CRUD operations for user accounts, including account state management and passkey registration.

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/get-all` | Retrieve a paginated list of all users |
| `GET` | `/get-user-details` | Fetch detailed profile for a specific user |
| `POST` | `/create` | Create a new user account |
| `POST` | `/register-prospective-user` | Register a prospective/lead user (pre-subscription) |
| `POST` | `/register-pass-keys` | Register passkeys for a user account |
| `POST` | `/update` | Update user profile information |
| `POST` | `/update-profile-image` | Update user's profile picture |
| `GET` | `/lock-account` | Lock a user account (restrict access) |
| `GET` | `/unlock-account` | Unlock a previously locked account |
| `GET` | `/activate` | Activate a user account |
| `GET` | `/deactivate` | Deactivate a user account |
| `DELETE` | `/delete` | Permanently delete a user account |
| `POST` | `/generate-jwt-token` | Generate a JWT auth token for a user |

---

## 3. Newspaper Management

**Prefix:** `/api/v1/news-papers`

Manages the lifecycle of newspaper editions — ingestion, publishing, and retrieval, with both public and gated access.

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/get-all` | List all newspapers (paginated) |
| `GET` | `/get-redacted-details` | Fetch a newspaper's metadata without full content (teaser view) |
| `GET` | `/get-full-details` | Fetch a newspaper's full content (authenticated) |
| `GET` | `/get-free-newspaper-details-by-publication` | Get free newspapers under a specific publication |
| `GET` | `/get-paid-newspaper-details-by-publication` | Get premium/paid newspapers under a specific publication |
| `GET` | `/publish` | Mark a newspaper as published and visible to subscribers |
| `GET` | `/unpublish` | Retract/unpublish a newspaper edition |
| `GET` | `/increment-view-count` | Track/increment view analytics for a newspaper |
| `POST` | `/search-archives` | Full-text search of archived page text (see below) |
| `POST` | `/ingest` | Submit a new newspaper edition for ingestion into the system |
| `POST` | `/update` | Update newspaper metadata/content |
| `DELETE` | `/delete` | Remove a newspaper edition from the system |

### Archive Search (`/search-archives`)

Full-text search across the OCR page text (`newspaper_details.page_text` and
`supporting_text`). The endpoint is public; when a JWT is present the `userId`
claim is captured for the search audit log, otherwise searches are attributed
to "GNP System User".

**Request body (JSON):**

| Field | Type | Description |
|-------|------|-------------|
| `query` | string | **Required.** Search string. Bare words are full-text keywords (ANDed, stemmed, case-insensitive). Text wrapped in double quotes is matched exactly as typed, ignoring case — e.g. `"kwame nkrumah" independence 1957`. |
| `pageNo` | int | Page number (default 1) |
| `pageSize` | int | Results per page, capped at 100 (default 10) |
| `dateFrom` | string | Optional, `YYYY-MM-DD` — restrict to `publication_date >= dateFrom` |
| `dateTo` | string | Optional, `YYYY-MM-DD` — restrict to `publication_date <= dateTo` |
| `publicationDate` | string | Optional, `YYYY-MM-DD` — restrict to an exact publication date |
| `publicationYear` | int | Optional — restrict to one publication year (0 or omitted = no filter) |
| `sortOrder` | string | `"desc"`, `"asc"` or `""` (default). Empty = relevance-first ordering; `desc`/`asc` = newest/oldest `publication_date` first with relevance as tie-breaker |

**Response:** standard `BaseApiResponse`; `result.data` contains matching
pages ordered by relevance, each with `id`, `newspaperId`, `pageNumber`,
`publicationDate`, `snippet`, `relevance`, `tags`, `categories`, etc., plus
`totalCount` / `totalPages` pagination metadata.

`snippet` is an excerpt of the page text centred on the match: ~120
characters to the left and right of the first occurrence of any search term
(case-insensitive), with `...` markers where the text is truncated. If a row
matched only through stemming (no literal occurrence of a term), the snippet
falls back to the first 300 characters of the page text.

> Requires the indexes from `sql/newspaper_details_search_index.sql` to stay
> fast on large archives.

---

## 4. Publications Management

**Prefix:** `/api/v1/publications`

Manages publication brands/channels (e.g., "Daily Graphic") that group newspaper editions.

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/get-all` | List all publications |
| `POST` | `/create` | Register a new publication |
| `POST` | `/update` | Update publication details |
| `GET` | `/activate` | Activate a publication (make it visible) |
| `GET` | `/deactivate` | Deactivate a publication |
| `DELETE` | `/delete` | Remove a publication |

---

## 5. Subscription Plans

**Prefix:** `/api/v1/pricing-plans`

Manage the pricing tiers available for subscriptions.

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/get-all` | List all available subscription plans |
| `POST` | `/create` | Create a new subscription plan |
| `POST` | `/update` | Update an existing plan (price, duration, features) |
| `DELETE` | `/delete` | Delete a subscription plan |

---

## 6. Subscriptions & Content Access

**Prefix:** `/api/v1/subscription`

Core module that handles subscriber onboarding flows — guest, registered, and partner — including one-time purchases, recurring access, and entitlement validation.

### Subscription Flows

| Method | Endpoint | Auth | Description |
|--------|----------|------|-------------|
| `POST` | `/guest` | None | Initiate a subscription flow for a guest (unauthenticated) user |
| `POST` | `/user` | JWT | Initiate a subscription flow for an authenticated user |
| `POST` | `/renew` | None | Renew an existing subscription |
| `GET` | `/get-all` | None | Get all subscriptions (admin use) |
| `GET` | `/get-details` | None | Fetch subscription details for a user |

### One-Time Purchase (Buy a Single Copy)

| Method | Endpoint | Auth | Description |
|--------|----------|------|-------------|
| `POST` | `/guest-onetime-buy` | None | Guest initiates a single-edition purchase |
| `GET` | `/fulfill-guest-onetime` | None | Fulfill/confirm a guest one-time purchase after payment |
| `GET` | `/user-onetime-buy` | JWT | Authenticated user initiates a one-time purchase |
| `GET` | `/fulfill-user-onetime` | JWT | Fulfill/confirm a one-time purchase for an authenticated user |
| `POST` | `/buy-copy` | JWT | Purchase a specific newspaper copy |
| `GET` | `/fulfill-buy-copy` | None | Confirm payment and deliver a purchased copy |

### Entitlement & Access

| Method | Endpoint | Auth | Description |
|--------|----------|------|-------------|
| `GET` | `/validate-newspaper-entitlement` | JWT | Check if user is entitled to access a given newspaper |
| `POST` | `/grant-newspaper-access` | None | Manually grant access to a specific newspaper |
| `GET` | `/get-newspaper-redacted-details-via-unique-id` | JWT | Retrieve newspaper preview using a unique ID |
| `GET` | `/find-newspaper-by-date` | JWT | Find a newspaper edition by publication date |

---

## 7. Payments

**Prefix:** (Managed via `SubscriptionsController` and `AdminController`)  
**Payment Gateway:** Paystack integration

Payments are driven by subscription and one-time purchase flows. The system supports:

- **Payment initialization** — preparing a payment session with amount, reference, and user metadata
- **Payment verification** — confirming successful transactions with Paystack
- **Payment status updates** — recording confirmed/failed states
- **Payment ledger** — admin view of all processed payments (`/api/v1/admin/get-all-payments`)

**Key DTOs:**
- `CreatePaymentDto` — stores amount, reference, user ID, plan details
- `InitializePaymentRequest` / `InitializePaymentResponse` — Paystack session objects
- `VerifyPayResponse` — detailed Paystack verification result

---

## 8. Coupons

**Prefix:** (Managed via the `CouponService`, pending full controller exposure)

Discount/promotional coupon management feature.

| Operation | Description |
|-----------|-------------|
| `getAll` | Paginated list of coupons; filterable by `status`, `expiry`, and `couponCode` |
| `createAsync` | Create a new coupon with code, discount type, value, and expiry |
| `updateAsync` | Edit existing coupon details |
| `deleteCoupon` | Remove a coupon by ID |

**Key DTOs:**
- `CreateCouponDto` — coupon code, discount value/type, validity period
- `UpdateCouponDto` — fields for editing an existing coupon

---

## 9. Campaigns

**Prefix:** `/api/v1/campaigns` (public scheduling endpoint) and `/api/v1/admin` (admin CRUD)

Marketing and promotional campaign management.

| Method | Endpoint | Description |
|--------|----------|-------------|
| `POST` | `/campaigns/run-scheduled-campaign` | Trigger a scheduled campaign to run immediately |
| `GET` | `/admin/get-all-campaigns` | List all campaigns (admin) |
| `POST` | `/admin/create-campaign` | Create a new campaign |
| `GET` | `/admin/publish-campaign` | Publish/activate a campaign |
| `GET` | `/admin/delete-campaign` | Delete a campaign |

**Key DTO:** `CreateCampaignDto` — campaign name, target audience, discount/offer details, start/end dates, publication targeting.

---

## 10. Commercial Partners

**Prefix:** `/api/v1/admin` (partner management) and `/api/v1/partner-api` (partner-facing B2B API)

Enables onboarding and management of external commercial partners (e.g., corporations offering GNP access to their employees/customers).

### Partner Management (Admin)

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/get-all-partners` | List all registered partners |
| `GET` | `/get-partner-details` | Get full details for a specific partner |
| `GET` | `/get-partner-stats` | View partner-level subscription statistics |
| `GET` | `/get-partner-subscribers` | List all subscribers under a partner |
| `GET` | `/get-partner-subscription-summary` | Aggregate summary of partner subscriptions |
| `POST` | `/create-partner` | Register a new commercial partner |
| `POST` | `/update-partner` | Update partner profile/information |
| `GET` | `/update-partner-status` | Change partner account status |
| `DELETE` | `/delete-partner` | Remove a commercial partner |
| `POST` | `/create-partner-subscriber` | Add a subscriber under a partner account |
| `POST` | `/assign-partner-subscribers-plan` | Assign a subscription plan to partner subscribers |
| `DELETE` | `/delete-partner-subscriber` | Remove a subscriber from a partner |
| `GET` | `/enable-partner-subaccount` | Enable a partner's Paystack subaccount |
| `GET` | `/disable-partner-subaccount` | Disable a partner's Paystack subaccount |

### Partner API Key Management (Admin)

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/get-partner-api-keys` | List all API keys for a partner |
| `POST` | `/generate-partner-api-key` | Generate a new API key for a partner |
| `POST` | `/update-partner-api-key` | Update API key metadata or permissions |
| `DELETE` | `/revoke-partner-api-key` | Revoke/delete a partner API key |

---

## 11. Partner API (External B2B)

**Prefix:** `/api/v1/partner-api`

A dedicated public-facing API for commercial partners to manage their own subscribers programmatically. Authenticated via partner API keys.

| Method | Endpoint | Description |
|--------|----------|-------------|
| `POST` | `/onboard-subscriber` | Register a new subscriber on behalf of a partner |
| `GET` | `/check-subscriber-status` | Check the subscription status of a partner's subscriber |
| `GET` | `/retrieve-subscriber-details` | Fetch full subscriber details for a partner's user |

---

## 12. Affiliate Marketing

**Prefix:** `/api/v1/affiliate`

Full affiliate programme management — onboarding, commission tracking, and payout processing.

### Affiliate Management

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/get-all` | Paginated list of all affiliates (searchable, sortable by earnings) |
| `POST` | `/create` | Register a new affiliate (auto-creates user account + sends welcome email) |
| `POST` | `/update` | Update affiliate profile/information |
| `GET` | `/suspend` | Suspend an affiliate's account |
| `DELETE` | `/delete` | Remove an affiliate |

### Commissions & Payouts

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/get-all-commissions` | List all commissions across all affiliates (admin view) |
| `GET` | `/get-affiliate-commissions` | Get commissions for a specific affiliate |
| `GET` | `/get-all-payouts` | List all payouts across all affiliates (admin view) |
| `GET` | `/get-affiliate-payouts` | Get payouts for a specific affiliate |
| `GET` | `/issue-affiliate-payout` | Issue a payout for a single affiliate (processes unpaid commissions) |
| `GET` | `/issue-bulk-payouts` | Bulk payout processing for all affiliates with pending commissions |

**Key DTOs:**
- `CreateAffiliateDto` — name, email, phone, commission rate
- `UpdateAffiliateDto` — editable affiliate fields

---

## 13. Ingestion Jobs

**Prefix:** `/api/v1/admin` (admin management)

Background job management for automated newspaper ingestion pipelines.

| Method | Endpoint | Description |
|--------|----------|-------------|
| `GET` | `/get-all-ingestion-jobs` | List all configured ingestion jobs |
| `POST` | `/create-ingestion-job` | Create a new ingestion job (schedule, source, publication mapping) |
| `GET` | `/delete-ingestion-job` | Remove an ingestion job |

**Key DTO:** `IngestJobDto` — job name, schedule, source URL/path, target publication.

Also supports direct ingestion via:
- `POST /api/v1/admin/ingest-newspaper` — manually ingest a newspaper edition  
- `POST /api/v1/news-papers/ingest` — direct newspaper ingestion endpoint

---

## 14. Real-Time Notifications

**WebSocket Endpoint:** `/notifications`

A WebSocket hub (`NotificationsHub`) enabling real-time push notifications to connected clients.

| Feature | Description |
|---------|-------------|
| Connection Management | Tracks active WebSocket connections with a thread-safe connection registry |
| Message Broadcast | `broadcastMessage()` pushes a message to **all** connected clients simultaneously |
| Event Handling | Handles `handleNewMessage`, `handleNewConnection`, and `handleConnectionClosed` lifecycle events |

> **Use Cases:** Subscription confirmation alerts, payment status updates, campaign broadcasts, admin announcements.

---

## 15. Admin Panel

**Prefix:** `/api/v1/admin`

A consolidated, JWT-protected admin surface that aggregates management capabilities across all feature domains.

| Domain | Coverage |
|--------|----------|
| **Newspapers** | Get all, full details, publish, unpublish, ingest, update, delete |
| **Users** | Get all, details, create, update, profile image, lock/unlock, activate/deactivate, delete |
| **Subscription Plans** | Get all, create, update, delete |
| **User Subscriptions** | Get all, get details, renew |
| **Campaigns** | Get all, create, publish, delete |
| **Commercial Partners** | Full CRUD, subscriber management, plan assignment, subaccount controls, API key management |
| **Payments** | View all payment records |
| **Ingestion Jobs** | Get all, create, delete |

All admin endpoints require a valid JWT and are intended for back-office staff use only.

---

## 16. Security & Middleware

### JWT Auth Filter (`JwtAuthFilter`)

- Applied to all protected routes via Drogon's filter chain
- Validates the JWT token from the `Authorization` header or cookie
- Rejects unauthorized requests with a `401` response before the handler is reached

### CORS Filter (`CorsFilter`)

- Implements Cross-Origin Resource Sharing controls
- Allowed origins are explicitly configured to the GNP web domains and local dev
- All CORS pre-flight (`OPTIONS`) responses are handled at the framework level

### Passkeys Support

- The platform supports WebAuthn-style passkeys as an alternative authentication factor  
- Registration: `POST /api/v1/auth/register-pass-keys`  
- Login: `POST /api/v1/auth/login-via-pass-keys`

---

## Architecture Summary

```
GnpApi/
├── controllers/       # HTTP route handlers (Drogon HttpController)
├── services/          # Business logic layer (one subdirectory per domain)
├── models/            # Drogon ORM model classes (auto-generated from DB schema)
├── dto/               # Data Transfer Objects for request/response contracts
├── filters/           # Middleware: JwtAuthFilter, CorsFilter
├── plugins/           # Drogon plugin integrations
├── utils/             # Shared utilities (JWT helpers, string utils, etc.)
└── main.cc            # Application entry point — loads config and starts server
```

The API follows an **async-first design** using Drogon coroutines (`drogon::Task<T>`), enabling high concurrency without thread-blocking operations.
