# Affiliate admin dashboard

`GET /api/v1/admin/get-affiliate-dashboard-data?startDate=YYYY-MM-DD&endDate=YYYY-MM-DD` (see `AdminController.h` for the configured admin prefix). Both query parameters are optional; if provided, each must be a valid ISO calendar date and `startDate <= endDate`. Invalid dates return HTTP 400. Dates include the whole end day, using the database connection's date/timezone semantics.

The usual `BaseApiResponse` envelope contains `result.stats` (four cards), `result.recentActivity` (up to five newest events) and `result.topAffiliates` (up to four affiliates ordered by commission earnings), plus `result.commissionTrend`. `icon` is a string such as `UsersIcon`, not a JavaScript component; the UI must map it to the corresponding icon. IDs are database IDs encoded as strings. Empty lists are returned as arrays and absent totals as zero.

* Total Affiliates counts non-rejected, non-submitted, non-deleted affiliate records created in the selected period. Pending Applications counts currently submitted records created in the period.
* Total Commission sums non-rejected commissions created in the period. Pending Payouts sums currently pending/processing payouts created in the period.
* `topAffiliates.referrals` counts non-rejected commission records as **referral conversions** (not clicks); no deployed referral-click table exists. `earnings` sums those commissions.
* Recent activity includes commission creation, affiliate signup, currently approved affiliates' last update as an approval event, and paid payouts' last update as a payout event. There is no status-transition history table, so subsequent updates may change the apparent approval/payout time. The `date` field is relative to query time.
* With both bounds supplied, `change`/`changeType` and `growth` compare with the immediately preceding interval of the same number of inclusive days. For open-ended ranges or a zero previous denominator, percentage changes are `0%` (growth `0`); open-ended `changeType` is `neutral`. Pending Applications shows the difference in pending counts as `N new`.

## Commission trend

`result.commissionTrend` is chart data only (no ECharts options):

```json
{ "xAxisData": ["2026-11-01", "2026-11-02"], "data": [120.0, 0.0] }
```

The arrays have matching lengths and include zero-valued buckets. Values are the sums of non-rejected affiliate commissions in Ghana cedis, filtered by the inclusive date range. For ranges of 1–31 days, buckets are daily (labels `YYYY-MM-DD`); 32–180 days, seven-day windows starting on `startDate` (labels `Week 1`, `Week 2`, …); 181–730 days, calendar months (`YYYY-MM`); longer ranges, calendar years (`YYYY`). A final partial week/month/year is included. Without both bounds, the trend defaults to the last 30 days; with only `endDate`, to the 30 days ending on that date; with only `startDate`, from that date through today. These chart defaults do not change the existing card/list filtering semantics.
