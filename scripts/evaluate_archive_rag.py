#!/usr/bin/env python3
"""Score a captured Graphic Archives RAG run against a curator-authored gold set.

Gold JSONL rows:
  {"id":"q1", "question":"...", "relevant_detail_ids":["..."],
   "expected_event_year":"1942"}

Result JSONL rows may be either the API response or a small capture record:
  {"id":"q1", "result":{"answer":"...", "citations":[...], ...},
   "latency_ms":123}

The script deliberately does not create historical labels or page IDs.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import statistics
import sys
from pathlib import Path
from typing import Any

YEAR_RE = re.compile(r"(?<!\d)(?:1[0-9]{3}|20[0-9]{2})(?!\d)")
CITATION_RE = re.compile(r"\[(S[0-9]+)\]")


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with path.open(encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, start=1):
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            try:
                value = json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"{path}:{line_number}: invalid JSON: {exc}") from exc
            if not isinstance(value, dict):
                raise ValueError(f"{path}:{line_number}: each row must be a JSON object")
            rows.append(value)
    return rows


def row_key(row: dict[str, Any]) -> str:
    value = row.get("id") or row.get("question") or row.get("query")
    return str(value) if value is not None else ""


def unwrap_result(row: dict[str, Any]) -> dict[str, Any]:
    inner = row.get("result")
    return inner if isinstance(inner, dict) else row


def detail_ids(result: dict[str, Any]) -> list[str]:
    if isinstance(result.get("retrievedIds"), list):
        return [str(value) for value in result["retrievedIds"]]
    citations = result.get("citations", [])
    if not isinstance(citations, list):
        return []
    found: list[str] = []
    for citation in citations:
        if isinstance(citation, dict) and citation.get("detailId"):
            value = str(citation["detailId"])
            if value not in found:
                found.append(value)
    return found


def answer_text(result: dict[str, Any]) -> str:
    value = result.get("answer", "")
    return value if isinstance(value, str) else ""


def dcg(relevances: list[int]) -> float:
    return sum(rel / math.log2(index + 2) for index, rel in enumerate(relevances))


def percentile(values: list[float], percent: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    position = (len(ordered) - 1) * percent
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gold", required=True, type=Path, help="curator-authored gold JSONL")
    parser.add_argument("--results", required=True, type=Path, help="captured API results JSONL")
    args = parser.parse_args()

    try:
        gold_rows = load_jsonl(args.gold)
        result_rows = load_jsonl(args.results)
    except (OSError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 2

    results_by_key = {row_key(row): row for row in result_rows if row_key(row)}
    recalls = {5: [], 10: []}
    reciprocal_ranks: list[float] = []
    ndcgs_at_5: list[float] = []
    year_checks: list[bool] = []
    date_checks: list[bool] = []
    unsupported_year_checks: list[bool] = []
    citation_validity: list[bool] = []
    latency_ms: list[float] = []
    no_evidence = 0
    matched_rows = 0

    for gold in gold_rows:
        key = row_key(gold)
        captured = results_by_key.get(key)
        if captured is None:
            continue
        matched_rows += 1
        result = unwrap_result(captured)
        retrieved = detail_ids(result)
        relevant_value = gold.get("relevant_detail_ids", gold.get("expected_detail_ids", []))
        relevant = {str(value) for value in relevant_value} if isinstance(relevant_value, list) else set()
        if not retrieved:
            no_evidence += 1

        if relevant:
            for k in (5, 10):
                hits = sum(1 for detail_id in retrieved[:k] if detail_id in relevant)
                recalls[k].append(hits / len(relevant))
            first_rank = next((i + 1 for i, value in enumerate(retrieved) if value in relevant), None)
            reciprocal_ranks.append(0.0 if first_rank is None else 1.0 / first_rank)
            rels = [1 if value in relevant else 0 for value in retrieved[:5]]
            ideal = [1] * min(len(relevant), 5)
            ideal_dcg = dcg(ideal)
            ndcgs_at_5.append(0.0 if ideal_dcg == 0 else dcg(rels) / ideal_dcg)

        answer = answer_text(result)
        citations = result.get("citations", [])
        valid_labels = {
            str(item.get("sourceId"))
            for item in citations
            if isinstance(item, dict)
            and item.get("sourceId")
            and item.get("detailId")
            and (not retrieved or str(item.get("detailId")) in retrieved)
        } if isinstance(citations, list) else set()
        used_labels = CITATION_RE.findall(answer)
        if used_labels:
            citation_validity.extend(label in valid_labels for label in used_labels)
        elif retrieved:
            citation_validity.append(False)

        direct_answer = answer.split("## What the archive says", 1)[0]
        expected_year = gold.get("expected_event_year")
        if expected_year not in (None, ""):
            year = str(expected_year)
            year_checks.append(year in direct_answer)
        expected_date = gold.get("expected_event_date")
        if expected_date not in (None, ""):
            date_checks.append(str(expected_date) in direct_answer)
        if expected_year in (None, "") and gold.get("no_answer") is True and result.get("answerGenerated") is True:
            # Check only the direct-answer section, not the displayed source
            # excerpts, which may contain publication/issue years.
            direct_answer = answer.split("## What the archive says", 1)[0]
            unsupported_year_checks.append(bool(YEAR_RE.search(direct_answer)))

        latency = captured.get("latency_ms", captured.get("latencyMs", result.get("latencyMs")))
        if isinstance(latency, (int, float)):
            latency_ms.append(float(latency))

    def average(values: list[float] | list[bool]) -> float | None:
        return None if not values else sum(float(value) for value in values) / len(values)

    report: dict[str, Any] = {
        "gold_questions": len(gold_rows),
        "matched_results": matched_rows,
        "retrieval": {
            "recall_at_5": average(recalls[5]),
            "recall_at_10": average(recalls[10]),
            "mrr": average(reciprocal_ranks),
            "ndcg_at_5": average(ndcgs_at_5),
            "questions_with_relevant_ids": len(reciprocal_ranks),
        },
        "answer": {
            "exact_event_year_accuracy": average(year_checks),
            "event_year_questions": len(year_checks),
            "exact_event_date_accuracy": average(date_checks),
            "event_date_questions": len(date_checks),
            "unsupported_event_year_rate_on_no_answer_questions": average(unsupported_year_checks),
            "no_answer_questions_checked": len(unsupported_year_checks),
            "citation_id_validity": average(citation_validity),
            "citation_labels_checked": len(citation_validity),
        },
        "operations": {
            "no_evidence_rate": None if matched_rows == 0 else no_evidence / matched_rows,
            "p50_latency_ms": percentile(latency_ms, 0.50),
            "p95_latency_ms": percentile(latency_ms, 0.95),
            "latency_samples": len(latency_ms),
        },
    }
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
