# Graphic Archives RAG API

The archive question-answering module retrieves evidence from OCR in
`public.newspaper_details.page_text` and `supporting_text`, then returns a
cited answer. The existing API contract remains available: `result.answer` is
Markdown, `result.citations` contains source records, and Ollama failures fall
back to retrieved evidence. Query expansion, embeddings, reranking, and indexing
all use a **local Ollama server**; no OpenAI-compatible provider is used.

## Question endpoint

`POST /api/v1/news-papers/ask-archives`

Requires a GNP bearer JWT (`Authorization: Bearer <token>`). CORS preflight
`OPTIONS` requests use the existing JWT filter behavior.

### Request

```json
{
  "question": "In which year did Kwame Nkrumah go to prison?",
  "topK": 5
}
```

- `question` (required; aliases: `query`, `q`): up to 2,000 characters.
- `topK` (optional; aliases: `top_k`, `limit`): 1–12 results; defaults to 5.
- `dateFrom`, `dateTo`, `publicationDate`: valid `YYYY-MM-DD` issue-date filters.
- `publicationYear`: optional issue year from 1 to 9999. Do not combine it with
  `publicationDate`.
- `publicationId` and `publicationName`: optional publication filters.

`result.answer` is a short Markdown response with a direct answer followed by a
chronological account. Citation labels such as `[S1]` refer to
`result.citations[].sourceId`. Citations preserve the existing detail/page and
publication metadata; passage-level results additionally include `chunkId`.
The API adds retrieval-plan and `dateValidationStatus` metadata without
removing the existing response fields. The generator must distinguish an
article's event date from `publicationDate` (the newspaper issue date). An
explicit event year/date is retained only when it appears in a cited OCR
excerpt; otherwise the response says that the passages do not establish it
and shows the supporting excerpts. Verify scans before relying on historical
claims because OCR can be incomplete or incorrect.

## Local Ollama configuration

The backend calls Ollama's native `POST /api/chat` endpoint. For vector search
and indexing it calls native `POST /api/embed`.

```sh
# Defaults when the API and Ollama share a host:
export GNP_RAG_BASE_URL='http://127.0.0.1:11434'
export GNP_RAG_CHAT_PATH='/api/chat'
export GNP_RAG_MODEL='llama3.2'              # installed chat model
export GNP_RAG_ENABLED='true'                # defaults to true
export GNP_RAG_QUERY_EXPANSION='true'        # defaults to true

# Used only after applying the pgvector migration:
export GNP_RAG_EMBEDDING_PATH='/api/embed'
export GNP_RAG_EMBEDDING_MODEL='nomic-embed-text' # 768 dimensions
export GNP_RAG_INDEXING_ENABLED='true'       # defaults to false
export GNP_RAG_INDEXING_KEY='<operator-secret>' # required by the admin batch route
export GNP_RAG_VECTOR_ENABLED='false'        # keep false until backfill is verified
export GNP_RAG_RERANK_ENABLED='false'        # optional extra local-model call
```

Install the selected models in the Ollama environment, for example:

```sh
ollama pull llama3.2
ollama pull nomic-embed-text
```

The embedding model must return **768-dimensional vectors** to match the
migration. Do not mix models in the same index; if changing the model, requeue
and rebuild the chunks before enabling vector search. Keep the Ollama API
reachable from the backend only. If the API runs in Docker and Ollama is in a
different container, set `GNP_RAG_BASE_URL` to the Ollama service hostname (for
example, `http://ollama:11434`) rather than `127.0.0.1`. Browser clients must
call the GNP API, not Ollama.

Environment variables override the matching `custom_config.ArchiveRag`
settings: `Enabled`, `BaseUrl`, `ChatPath`, `Model`, `QueryExpansionEnabled`,
`EmbeddingPath`, `EmbeddingModel`, `IndexingEnabled`, `VectorSearchEnabled`,
and `RerankingEnabled`.

## Retrieval behavior and safe defaults

The lexical path uses PostgreSQL full-text search and the GIN/date indexes in
`sql/newspaper_details_search_index.sql`. Ollama returns bounded structured
query groups (`entities`, `event_terms`, `alternate_queries`). Retrieval first
tries an entity-plus-event query, relaxes to event terms if it needs more
matches, and then tries alternate/original terms. Small deterministic event
synonym groups (including prison, jailed, imprisoned, and detained) preserve
useful fallback behavior when Ollama is unavailable. Search values are bound to
SQL, and all term strings are converted to a restricted `tsquery` locally.

Passage embeddings and hybrid search are **opt-in**. Until the pgvector schema
is installed, the backfill is complete, and the stored model/dimensions are
verified, leave `GNP_RAG_VECTOR_ENABLED=false`. The API then continues using the
page-level FTS path. When enabled, it embeds the question once, retrieves from
passage FTS and cosine-vector lanes, and fuses ranks with reciprocal rank
fusion (RRF). A local Ollama rerank of at most 20 candidates is optional and is
disabled by default. Missing embeddings or vector tables produce an FTS
fallback rather than an unindexed corpus scan.

## Optional passage index and backfill

### 1. Confirm database support and apply the migration

`sql/archive_rag_chunks.sql` creates the `vector` extension, a 768-dimension
passage table, passage GIN/HNSW indexes, and a durable OCR indexing queue. It
requires PostgreSQL with permission to install pgvector and pgvector **0.5 or
newer** for HNSW. Confirm the target deployment supports this before applying
it; the migration is optional and does not replace the existing page FTS index.

```sh
psql "$DATABASE_URL" -f sql/newspaper_details_search_index.sql
psql "$DATABASE_URL" -f sql/archive_rag_chunks.sql
```

### 2. Queue the initial backfill

The one-time/re-runnable script queues pages that do not yet have passage
chunks. It does not generate vectors in the user-question path.

```sh
psql "$DATABASE_URL" -f sql/archive_rag_enqueue_backfill.sql
```

New OCR ingestions are added to the durable queue after the passage migration
exists. If the optional migration is absent, the existing OCR ingestion still
succeeds and logs a warning rather than failing the ingestion request.

### 3. Process bounded batches

The maintenance route requires both the existing JWT filter and a dedicated
operator key (`GNP_RAG_INDEXING_KEY`) because the repository's JWT filter does
not distinguish admin roles. It processes at most 20 pages per request. Enable
it only for an authorized operator, then call it repeatedly until
`result.remaining` is zero:

```sh
export GNP_RAG_INDEXING_ENABLED=true
curl -X POST 'https://<api-host>/api/v1/admin/rag/index-pending' \
  -H 'Authorization: Bearer <operator-jwt>' \
  -H 'X-GNP-RAG-Index-Key: <operator-secret>' \
  -H 'Content-Type: application/json' \
  -d '{"batchSize":10}'
```

The route returns `claimed`, `indexed`, `skipped`, `failed`, `remaining`, the
embedding model, and vector dimensions. Failures stay queued for retry; stale
claims are released after 15 minutes. Indexing is idempotent at the page level
and uses a PostgreSQL MD5 source hash plus model name. The queue keeps OCR
ingestion independent of Ollama availability. Apply API-level rate limits and
restrict access to this maintenance route in deployments where JWTs are not
admin-scoped.

After backfill, inspect queue and chunk counts and sample rows in PostgreSQL.
Only then set `GNP_RAG_VECTOR_ENABLED=true`. Keep the old GIN index in place so
FTS remains a fallback. To rebuild after changing the embedding model, delete
or requeue the affected passage rows, run the enqueue script, and process the
queue again before switching the vector-search model. The migration's fixed
768 dimension must match the selected Ollama embedding model.

## Evaluation and rollout

A standard-library-only evaluation utility is available at
`scripts/evaluate_archive_rag.py`. Prepare a private JSONL gold file with
question IDs, relevant `detailId` values, expected event years (where known),
and then feed it plus captured API results to the utility. It reports
Recall@5/10, MRR, nDCG@5, exact-year accuracy, citation-ID validity, no-evidence
rate, and latency percentiles. The scoring utility is covered by `python3 test/test_archive_rag_eval.py`.
The repository intentionally does not invent archive page IDs or historical
answer labels; a curator must supply the gold set before baseline numbers can
be meaningful. Keep logged evaluation data to questions, source IDs/ranks, and
metrics; avoid storing full OCR or model credentials unnecessarily.

Recommended rollout: measure page-level FTS first; then enable passage
indexing in development, compare hybrid retrieval against the same gold set,
shadow or canary it, and only then enable vector retrieval. Reranking stays
independently disabled unless evaluation shows a net gain at acceptable latency.
For rollback, set `GNP_RAG_VECTOR_ENABLED=false` and
`GNP_RAG_RERANK_ENABLED=false`; page-level PostgreSQL FTS remains available.

## API implementation references

- `services/rag/RagManager.cc` — staged query planning, retrieval fusion,
  optional Ollama embeddings/reranking, date-aware grounded generation, and
  bounded index processing.
- `sql/newspaper_details_search_index.sql` — existing page-level FTS/date
  indexes.
- `sql/archive_rag_chunks.sql` — optional passage/vector/queue schema.
- `controllers/NewsPapersController.cc` — question endpoint.
- `controllers/AdminController.cc` — authenticated index batch route.
