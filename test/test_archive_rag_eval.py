import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
EVALUATOR = REPO_ROOT / "scripts" / "evaluate_archive_rag.py"


class ArchiveRagEvaluationTest(unittest.TestCase):
    def test_report_scores_retrieval_dates_citations_and_latency(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            gold_path = root / "gold.jsonl"
            results_path = root / "results.jsonl"
            gold_path.write_text(
                "\n".join(
                    [
                        json.dumps(
                            {
                                "id": "q1",
                                "question": "Which year?",
                                "relevant_detail_ids": ["page-a", "page-b"],
                                "expected_event_year": "1942",
                                "expected_event_date": "1942-06-01",
                            }
                        ),
                        json.dumps(
                            {
                                "id": "q2",
                                "question": "No answer?",
                                "relevant_detail_ids": [],
                                "no_answer": True,
                            }
                        ),
                    ]
                )
                + "\n",
                encoding="utf-8",
            )
            results_path.write_text(
                "\n".join(
                    [
                        json.dumps(
                            {
                                "id": "q1",
                                "latency_ms": 100,
                                "result": {
                                    "answer": "## Answer\n1942 [S1]",
                                    "answerGenerated": True,
                                    "citations": [
                                        {"sourceId": "S1", "detailId": "page-a"},
                                        {"sourceId": "S2", "detailId": "page-b"},
                                    ],
                                },
                            }
                        ),
                        json.dumps(
                            {
                                "id": "q2",
                                "latency_ms": 200,
                                "result": {
                                    "answer": "The retrieved passages do not establish a date.",
                                    "answerGenerated": True,
                                    "citations": [],
                                },
                            }
                        ),
                    ]
                )
                + "\n",
                encoding="utf-8",
            )

            completed = subprocess.run(
                [
                    sys.executable,
                    str(EVALUATOR),
                    "--gold",
                    str(gold_path),
                    "--results",
                    str(results_path),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            report = json.loads(completed.stdout)

        self.assertEqual(report["retrieval"]["recall_at_5"], 1.0)
        self.assertEqual(report["retrieval"]["mrr"], 1.0)
        self.assertEqual(report["answer"]["exact_event_year_accuracy"], 1.0)
        self.assertEqual(report["answer"]["exact_event_date_accuracy"], 0.0)
        self.assertEqual(report["answer"]["citation_id_validity"], 1.0)
        self.assertEqual(report["answer"]["unsupported_event_year_rate_on_no_answer_questions"], 0.0)
        self.assertEqual(report["operations"]["p50_latency_ms"], 150.0)


if __name__ == "__main__":
    unittest.main()
