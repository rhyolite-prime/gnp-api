# Graphic Archives RAG API

The archive question-answering module retrieves evidence from the OCR text in `public.newspaper_details.page_text` (and `supporting_text`), then returns a cited answer. It uses PostgreSQL full-text search, optionally expands the question with a **local Ollama model**, and asks Ollama to write a grounded response. The raw OCR page is not returned wholesale.

## Endpoint

`POST /api/v1/news-papers/ask-archives`

The endpoint requires a GNP bearer JWT (`Authorization: Bearer <token>`). CORS preflight `OPTIONS` requests bypass authentication through the existing JWT filter. Authentication helps limit resource use when local model inference is enabled.

### Request

```json
{
  "question": "In which year did Kwame Nkrumah go to prison?",
  "topK": 5
}
```

- `question` (required; aliases: `query`, `q`): up to 2,000 characters.
- `topK` (optional; aliases: `top_k`, `limit`): 1–12 results; defaults to 5.
- `dateFrom`, `dateTo`, `publicationDate`: valid `YYYY-MM-DD` dates.
- `publicationYear`: optional year from 1 to 9999. Do not combine with `publicationDate`.
- `publicationId` and `publicationName`: optional publication filters.

### Response

The response follows the existing `BaseApiResponse` envelope. With Ollama available, `result.answer` is Markdown with a direct answer followed by a short chronological account. It cites claims as `[S1]`, corresponding to `citations[].sourceId`. The source array includes the excerpt, newspaper issue date, page number, and record IDs. If Ollama is disabled or unavailable, the API still returns retrieved excerpts in extractive form.

`generationStatus` is `completed`, `disabled`, `provider_unavailable`, or `skipped_no_results`. `answerMode` is `generated`, `extractive`, or `retrieval_only`. `retrievalMethod` indicates whether Ollama query expansion was used (`postgres_fts_ollama_expansion`) or retrieval used the question text alone (`postgres_fts`).

## Ollama configuration

The API calls Ollama from the **backend process** through its native `POST /api/chat` API. No OpenAI key or cloud account is required.

```sh
# Default when API and Ollama share a host:
export GNP_RAG_BASE_URL='http://127.0.0.1:11434'
export GNP_RAG_CHAT_PATH='/api/chat'

# Use the name of a model already installed in `ollama list`:
export GNP_RAG_MODEL='llama3.2'  # default; override for your local model, e.g. qwen2.5:7b
export GNP_RAG_ENABLED='true'    # optional; defaults to true
export GNP_RAG_QUERY_EXPANSION='true' # optional; defaults to true
```

The model name must exactly match a locally installed Ollama model. If the API runs in Docker while Ollama runs in another container, use the Ollama service/container hostname for `GNP_RAG_BASE_URL` (for example, `http://ollama:11434`), not `127.0.0.1`. Keep the Ollama API reachable from the backend only; browser code should call the GNP API, not Ollama directly. Alternatively, `custom_config.ArchiveRag` may set `Enabled`, `BaseUrl`, `ChatPath`, `Model`, and `QueryExpansionEnabled`; environment variables take precedence.

When enabled, the local model is used first to suggest alternate search words (such as “jailed” or “imprisoned” for a question about prison), then again to compose the answer from retrieved excerpts. Set `GNP_RAG_QUERY_EXPANSION=false` to skip the first model call. Set `GNP_RAG_ENABLED=false` to disable both Ollama calls while keeping PostgreSQL retrieval and citations available.

## Retrieval and database setup

The system combines the original question with Ollama-suggested search terms, sanitizes them, and performs ranked PostgreSQL full-text search over `page_text` and `supporting_text`. Terms are ORed so a natural-language question does not need to match word-for-word. Date and publication filters are applied before selecting the top results. Request and model-generated terms are bound/sanitized rather than inserted as SQL.

Apply the existing GIN index migration on the target database:

```sh
psql "$DATABASE_URL" -f sql/newspaper_details_search_index.sql
```

The indexed `to_tsvector` expression must stay in sync with the RAG and archive-search SQL. This is lexical retrieval with local-model query expansion, **not vector/embedding search**. Expansion can help find synonyms, but spelling errors or missing OCR text can still cause a miss. The answer prompt requires the model to distinguish a newspaper's issue date from an event date, cite evidence, and state when the pages do not establish a year. Check the cited scan before using an answer as a historical fact.

## Integration notes

- `services/rag/RagManager` owns retrieval, optional Ollama query expansion, and answer generation.
- `GnpServicePlugin` owns the manager; `NewsPapersController` exposes it at the endpoint above.
- The existing archive GIN/date indexes are reused; no new database extension or schema migration is required.
- Add infrastructure-level per-user rate limiting and usage monitoring before production rollout. If Ollama is configured on a separate host, OCR excerpts are sent to that host; ensure its access and data-retention policy are appropriate.
