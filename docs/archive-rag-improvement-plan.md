# Graphic Archives RAG Improvement Plan

## Goal

Improve answer accuracy for natural-language research questions over OCR text in `newspaper_details.page_text`, while keeping the answer tied to verifiable archive evidence. Example: **“In which year did Kwame Nkrumah go to prison?”** should retrieve pages discussing imprisonment even if the OCR/article uses words such as “jailed” or “detained,” distinguish the event date from the newspaper issue date, and return a concise cited narrative.

## Current baseline

The initial `RagManager` called a local Ollama chat model for optional query expansion and answer generation. PostgreSQL full-text search then searched `page_text` and `supporting_text`, ranked page rows with `ts_rank_cd`, and returned `ts_headline` excerpts. Query words and generated expansions were flattened into one OR query. Retrieval was page-level; there was no embedding index or reranker.

This was a useful first release, but the broad OR query could rank a page that matched a common word (for example, “year”) over a page that directly describes the event. Expansion also improved recall at the risk of adding irrelevant terms.

## Phase 0 — Establish a measurable baseline

**Deliverables**

- Build a gold set of 50–100 research questions with the expected archive `detailId`/page IDs and, when known, the expected event year or date.
- Include paraphrases, people and place names, OCR spelling variants, questions with no answer in the archive, conflicting reports, and cases where only the issue date is known.
- Run the current search against this set and record retrieval and answer results before changing ranking.

**Metrics**

- Retrieval: Recall@5/10, MRR, and nDCG@5.
- Answer: exact-year/date accuracy, citation validity, and whether an unsupported date was asserted.
- Operations: Ollama failure rate, no-evidence rate, and p50/p95 end-to-end latency.

Keep any stored logs minimal: query, retrieved IDs/ranks, and outcome metrics; do not log full OCR pages or model credentials.

## Phase 1 — Make lexical retrieval more selective

Keep the existing PostgreSQL GIN index and improve how the question is converted to search clauses.

1. Change Ollama query expansion from a flat list of terms to structured output, for example:

   ```json
   {
     "entities": ["Kwame Nkrumah"],
     "event_terms": ["prison", "imprisoned", "jailed", "detained", "sentenced"],
     "alternate_queries": ["Nkrumah jailed", "Nkrumah imprisonment"]
   }
   ```

2. Preserve person/place names as high-value anchors. Build grouped queries such as `Nkrumah AND (prison OR jailed OR imprisoned OR detained)` rather than OR-ing every question word and expansion together. Do not use a word such as “year” as an event-date filter.
3. Search the original query and expanded alternatives as separate retrieval lanes. Combine/rank their results rather than allowing low-value expansions to swamp the original query.
4. Use a staged fallback: first require a strong entity plus an event term; if too few pages match, relax the entity constraint while keeping event terms; then fall back to the original full-text query.
5. Preserve the current date/publication filters and bound all user/model text. Keep query expansion optional and cap its output.

**Acceptance check:** the expected Nkrumah source page should appear in the top five for the original wording and at least one paraphrase, without unrelated “year” matches dominating the results.

## Phase 2 — Add passage chunks and a local embedding index

Page-level retrieval can return a page containing several unrelated articles. Index smaller passages so the answer model receives the relevant article text rather than only a page headline snippet.

1. Confirm that the PostgreSQL deployment supports `pgvector`. If it does not, select a compatible vector store before implementation; do not silently fall back to an unindexed full-table scan.
2. Choose and pin one Ollama embedding model. Record its name/version and vector dimension; do not mix embeddings from different models in one vector index.
3. Create a chunk table keyed to `newspaper_details.id`, with at least: chunk ID, detail ID, chunk number, original chunk text, content hash, embedding model/version, and embedding vector. Join the source row for publication/date/page/document metadata.
4. Split page text at paragraph/sentence boundaries where possible. A starting point is roughly 350–500 tokens per chunk with 50–80 tokens of overlap. Keep the raw OCR unchanged; only apply conservative normalization for search (whitespace normalization and carefully tested line-break/hyphen repair).
5. Backfill existing non-empty pages in batches using Ollama’s embedding API. Add an incremental background indexing path for new or updated OCR pages, with retries and idempotency based on a text hash. Do not embed the whole corpus during a user’s question request.
6. Add both a vector index and a full-text GIN index over the chunks. Validate chunk counts and sample stored embeddings before enabling vector retrieval.

## Phase 3 — Hybrid retrieval and reranking

For each question, embed the query once, then retrieve candidates from both the vector index and chunk full-text index, applying date/publication filters before final ranking.

- Retrieve a wider candidate set from each lane (for example, top 25–30), then combine ranks with reciprocal rank fusion. This avoids adding cosine and PostgreSQL scores directly, since their scales are not comparable.
- Deduplicate near-identical chunks from the same page and preserve enough source diversity for research questions spanning multiple dates.
- Optionally rerank the best 10–20 candidates with a local reranker. If using Ollama chat for reranking, ask only for an ordered list of candidate IDs; do not ask it to create new evidence. Keep reranking behind a feature flag because it adds latency.
- Return a small evidence set (for example, 5–8 chunks) with source ID, detail ID, page number, publication name, issue date, and excerpt. Keep the existing `citations` response field compatible.

**Fallbacks:** if embeddings are unavailable, use the improved full-text lanes; if Ollama is unavailable, retain the current extractive response path.

## Phase 4 — Strengthen date-aware answer generation

Keep Ollama local and use a structured answer contract internally, then render the existing `result.answer` string for API compatibility. Suggested fields:

```json
{
  "direct_answer": "...",
  "narrative": "...",
  "event_dates": [{"date": "...", "claim": "...", "source_ids": ["S1"]}],
  "limitations": "..."
}
```

- Require a direct answer first, followed by a short chronological narrative in Markdown.
- Require citations for factual claims and dates. Validate each source ID against the retrieved evidence; reject or remove unknown IDs.
- Distinguish event dates in the article text from `publicationDate` metadata, which is the issue date. If the archive only establishes an issue date, say so rather than treating it as the event date.
- If no retrieved passage explicitly supports the requested year/date, say the evidence is insufficient. Do not fill gaps with general model knowledge.
- Keep each passage bounded and cap total prompt context. Continue treating OCR text as untrusted input, not instructions.

## Phase 5 — Evaluate and roll out safely

1. Re-run the gold set after each phase and compare against the Phase 0 baseline.
2. Add integration coverage for: Ollama reachable/unreachable, missing embedding, stale/reindexed OCR, query filters, unknown citations, issue-date/event-date distinction, contradictory sources, and no-evidence questions.
3. Enable vector search and reranking behind configuration flags. Start in development, then shadow or canary the new retrieval beside the current FTS path before switching default.
4. Monitor retrieval metrics, citation/date correctness, Ollama latency/errors, and indexing lag. Keep a reindex command/job and a documented rollback to FTS-only retrieval.

**Suggested initial release gates** (adjust after measuring the baseline): improve Recall@5 on the gold set, achieve at least 95% citation-ID validity, and make zero unsupported event-year claims on the date-answer test subset. Set the latency target based on the actual API/Ollama hardware.

## Recommended implementation order

1. Gold-question evaluation set and baseline.
2. Structured query expansion plus grouped/staged PostgreSQL FTS retrieval (quickest accuracy gain).
3. Chunk table, Ollama embedding backfill, and incremental indexing.
4. Hybrid vector + FTS retrieval with rank fusion.
5. Optional reranker, then date/citation validation and production rollout.

This order gives an early improvement without waiting for the embedding migration, while making it possible to tell whether each added component actually improves research results.

## Implementation status — 2026-10-16

- **Implemented in the API:** structured entity/event/alternate query planning; rule-based prison/arrest and related event synonyms; staged page-level FTS retrieval; optional passage FTS + Ollama vector search with RRF; optional local-chat candidate reranking; structured grounded answer generation; source-ID validation; event-year checks against cited OCR excerpts; and extractive fallback.
- **Implemented for indexing:** optional 768-dimension pgvector schema, sentence/paragraph-aware overlapping passage splitting, Ollama `/api/embed` calls in bounded batches, durable idempotent queueing for new OCR pages, retryable index processing, an authenticated admin batch endpoint, and an initial backfill SQL script. Vector retrieval and indexing are disabled by default.
- **Evaluation support:** `scripts/evaluate_archive_rag.py` scores a curator-supplied JSONL gold set, with an automated scoring test at `test/test_archive_rag_eval.py`. No fabricated gold page IDs or baseline metrics are included; Phase 0 measurement and the Recall@5/date release gates remain pending until archive labels are provided.
- **Deployment gate:** pgvector availability/version and the selected Ollama embedding model have not been verified against the target database/host. Apply the optional migration and validate 768-dimensional samples before enabling `GNP_RAG_VECTOR_ENABLED`.
- **Verification caveat:** no database-backed integration run or C++ build/test has been completed in this workspace. Run the repository build and the documented offline evaluation plus database/Ollama integration checks before production rollout.
