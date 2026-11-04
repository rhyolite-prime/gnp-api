"""SQL regression fixtures (requires `python3 -m pip install duckdb`).

Uses the actual service queries in DuckDB; a PostgreSQL integration test is
still needed for transaction/advisory-lock and driver behaviour.
"""
import pathlib
import re
import unittest

try:
    import duckdb
except ImportError:
    duckdb = None

ROOT = pathlib.Path(__file__).resolve().parents[1]


def sql_in(path, function_name):
    source = (ROOT / path).read_text()
    source = source[source.index(function_name):]
    return re.search(r'R"sql\((.*?)\)sql"', source, re.S).group(1)


@unittest.skipIf(duckdb is None, "install duckdb to run SQL fixtures")
class PartnerInvoiceQueries(unittest.TestCase):
    def setUp(self):
        self.db = duckdb.connect()
        self.db.execute("""CREATE TABLE user_subscriptions (
            partner_id VARCHAR, user_id VARCHAR, subscription_identifier VARCHAR,
            start_date TIMESTAMP, end_date TIMESTAMP)""")
        self.db.execute("""CREATE TABLE subscription_renewal_history (
            id INTEGER, partner_id VARCHAR, user_id VARCHAR, subscription_identifier VARCHAR,
            past_billing_cycle VARCHAR, transaction_status VARCHAR, created_at TIMESTAMP)""")
        self.db.execute("""CREATE TABLE partner_invoices (
            partner_id VARCHAR, description VARCHAR, created_at TIMESTAMP,
            invoice_amount DECIMAL(12,2), balance DECIMAL(12,2), unit_price DECIMAL(12,2))""")

    def test_classification_and_date_bounds(self):
        self.db.execute("""INSERT INTO user_subscriptions VALUES
            ('p1','u1','same','2026-01-10 13:20','2026-02-10 13:20'),
            ('p1','u2','same','2026-01-10 23:59:59','2026-03-10 23:59:59'),
            ('p2','u3','third','2026-01-10 00:00','2026-02-10 00:00'),
            ('p1','u4','fourth','2026-01-11 00:00','2026-02-11 00:00'),
            (NULL,'u5','fifth','2026-01-10 00:00','2026-02-10 00:00')""")
        self.db.execute("""INSERT INTO subscription_renewal_history VALUES
            (1,NULL,'u1','same','N/A','Successful','2026-01-01'),
            (2,'p1','u2','same','Monthly','Successful','2026-01-02'),
            (3,'p1','u2','same','Monthly','Failed','2026-01-03'),
            (4,'other','u3','third','Monthly','Successful','2026-01-03'),
            (5,'p1','u4','fourth','Monthly','Successful','2026-01-03')""")
        query = sql_in('services/partner_invoice/PartnerInvoiceService.cc',
                       'PartnerInvoiceService::generatePartnerInvoices')
        rows = self.db.execute(query, ['2026-01-10']).fetchall()
        self.assertEqual(len(rows), 3)
        self.assertEqual(sum(bool(row[2]) for row in rows), 1)
        self.assertEqual(sum(bool(row[3]) for row in rows), 1)
        self.assertTrue(next(row[3] for row in rows if row[0] == 'p2'))
        self.assertEqual(sorted(row[0] for row in rows), ['p1', 'p1', 'p2'])

    def test_report_includes_last_day_and_excludes_other_invoices(self):
        self.db.execute("""INSERT INTO partner_invoices VALUES
            ('p1','Invoice for new subscriptions as of 2026-01-01','2026-01-01 00:00', 10, 7, 5),
            ('p1','Invoice for renewed subscriptions as of 2026-01-31','2026-01-31 23:59', 30, 30, 10),
            ('p1','Additional subscriber quota invoice','2026-01-31 12:00', 900, 900, 3),
            ('p1','Invoice for new subscriptions as of 2026-02-01','2026-02-01 00:00', 900, 900, 3),
            ('p2','Invoice for new subscriptions as of 2026-01-01','2026-01-01 00:00', 900, 900, 3)""")
        query = sql_in('services/partners/CommercialPartnerService.cc',
                       'CommercialPartnerService::getPartnerInvoiceGenerationReport')
        args = ['p1', '2026-01-01', '2026-01-31',
                'Invoice for new subscriptions as of %',
                'Invoice for renewed subscriptions as of %',
                'Invoice for new and renewed subscriptions as of %']
        rows = {row[0]: row[1:] for row in self.db.execute(query, args).fetchall()}
        self.assertEqual(set(rows), {'new', 'renewed'})
        self.assertEqual(tuple(map(float, rows['new'])), (10.0, 7.0, 2.0, 0.0))
        self.assertEqual(tuple(map(float, rows['renewed'])), (30.0, 30.0, 3.0, 0.0))
        self.db.execute("""INSERT INTO partner_invoices VALUES
            ('p1','Invoice for new and renewed subscriptions as of 2026-01-05',
             '2026-01-05 00:00', 12, 12, 3)""")
        rows = self.db.execute(query, args).fetchall()
        self.assertIn('legacy', {row[0] for row in rows})


if __name__ == '__main__':
    unittest.main()
