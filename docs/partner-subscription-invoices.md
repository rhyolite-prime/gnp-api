# Daily partner subscription invoices

For each partner with subscriptions starting on the requested date, the daily
job creates two pre-tax invoices: `Invoice for new subscriptions as of
YYYY-MM-DD` and `Invoice for renewed subscriptions as of YYYY-MM-DD`. A category
with no subscriptions has a zero-value invoice. Non-partner subscriptions are
ignored. The job uses the existing duration multiplier and contracted per-seat
rate, rounded to pesewas. It accepts a date or a timestamp beginning with a date.
A per-partner/date transaction and advisory lock prevent duplicate invoices on
retries or concurrent runs. Existing split invoices are not recalculated; run
the job after that day's onboarding is complete. The job continues scheduling
itself even when there are no subscriptions on a day.

Classification uses the latest successful renewal-history record matching both
user and subscription identifier, scoped to the same partner when that field is
present. A `past_billing_cycle` of `N/A` or empty denotes a first subscription;
a real previous cycle denotes renewal. If any subscription cannot be classified
because its matching history is missing, the whole partner/date is skipped and
`unclassifiedPartnersSkipped` is incremented; reconcile the history before
retrying. The current subscription table is mutable and history does not record each period's
start and end, so historical reruns after subsequent renewals and records with
missing/incorrect history cannot be reconstructed reliably. Reconcile those
records before generating invoices; a future immutable lifecycle ledger with
period dates would be needed for complete historical accuracy.

Existing combined invoices are never silently split or billed again. Generation
skips such partners and returns `legacyPartnersSkipped`. The report returns a
validation error if its period includes a combined invoice; reconcile it manually
before using the split report. Other invoice types such as quota top-ups are not
included in the two subscription lines.

The report has exactly two items, including zero-value categories. Quantity means
billed **seat-months**, not active users; the displayed unit price is the weighted
average across invoices. A zero-priced invoice makes the quantity and unit price
unknown, because the invoice table does not store seat-months separately. The end
of the reporting window is inclusive.

The report preserves the application's existing policy: VAT 15%, combined
GETFund/NHIL 5%, and a 5% discount, each computed on the subtotal and rounded to
the nearest pesewa, then `totalDue = subtotal + VAT + GETFund/NHIL - discount`.
These rates and the discount are existing application assumptions, **not a
verification of current Ghana tax law or a partner's contract**. Confirm them
with the business/tax team. Stored invoices, payments and balances remain
pre-tax as before; the report's tax-inclusive total is a preview, not a claim that
tax has been invoiced. `totalBalance` and `outstandingBalance` reflect the
actual pre-tax receivable. Changing to tax-inclusive invoicing requires an
explicit billing-policy decision and persisted tax/payment components.

## Tests

`test_partner_invoice_calculation.cc` tests rounding and duration boundaries.
`test_partner_invoice_sql.py` executes the service aggregation queries against
DuckDB fixtures (install `duckdb`); PostgreSQL integration testing is still needed
to exercise transaction commits, advisory locks, concurrent jobs, and ORM inserts.
