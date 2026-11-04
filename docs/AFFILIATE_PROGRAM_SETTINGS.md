# Affiliate program settings

Admin `create-affiliate-program-settings` accepts JSON matching the `affiliate_program_settings` table:

```json
{
  "defaultCommissionRate": 10,
  "minimumPayoutAmount": 50,
  "maximumPayoutAmount": 10000,
  "payoutSchedule": 30,
  "sessionDuration": 30,
  "termsAndConditions": "Program terms"
}
```

The first request must include `defaultCommissionRate`, `minimumPayoutAmount`, `maximumPayoutAmount` and `termsAndConditions`. Later requests may contain only the columns to change; omitted fields retain their prior values. Numeric fields must be non-negative; minimum payout must not exceed maximum payout. `payoutSchedule` and `sessionDuration` are optional non-negative integers. Both the create/update response and `get-affiliate-program-settings` response return the persisted row under `result.data` (or `null` if no row exists on GET). Invalid input receives HTTP 400.

Writes are serialized with a PostgreSQL transaction advisory lock. Existing rows are consolidated into one: the lowest UUID is retained and updated, and extra rows are deleted in the same transaction. This ensures repeated or concurrent calls through this endpoint cannot add a second row. Other direct database writers must follow the same singleton convention.
