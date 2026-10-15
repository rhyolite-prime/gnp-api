# Graphic Archives RAG API

The archive question-answering module retrieves evidence from the OCR text stored in `public.newspaper_details.page_text` (with `supporting_text` as a fallback/searchable companion) and returns a cited answer. Retrieval uses PostgreSQL full-text search; an optional OpenAI-compatible chat-completions endpoint synthesizes an answer from the retrieved excerpts. The raw OCR page is never returned wholesale by this endpoint.

## Endpoint

`POST /api/v1/news-papers/ask-archives`

The endpoint requires a GNP bearer JWT (`Authorization: Bearer <token>`). CORS preflight `OPTIONS` requests bypass authentication through the existing JWT filter. Authentication is important when generation is enabled because a public endpoint could otherwise incur unbounded model costs.

### Request

```json
{
  "question": "What did the Graphic report about Ghana's 1966 coup?",
  "topK": 5,
  "dateFrom": "1966-01-01",
  "dateTo": "1966-12-31",
  "publicationId": "optional-publication-id",
  "publicationName": "optional publication name"
}
```

- `question` (required; aliases: `query`, `q`): up to 2,000 characters.
- `topK` (optional; aliases: `top_k`, `limit`): 1–12 results; defaults to 5.
- `dateFrom`, `dateTo`, `publicationDate`: valid `YYYY-MM-DD` dates.
- `publicationYear`: optional year from 1 to 9999. Do not combine with `publicationDate`.
- `publicationId` and `publicationName`: optional publication filters.

### Response

The response follows the existing `BaseApiResponse` envelope. `result.answer` is an LLM-generated synthesis when configured, or an extractive evidence summary otherwise. `result.citations` always contains the source metadata and OCR excerpt for each retrieved page. Generated answers use citation labels such as `[S1]`, corresponding to `citations[].sourceId`.

```json
{
  "success": true,
  "message": "Archive evidence retrieved successfully.",
  "result": {
    "question": "What did the Graphic report about Ghana's 1966 coup?",
    "answer": "The report described ... [S1]",
    "answerMode": "generated",
    "answerGenerated": true,
    "generationStatus": "completed",
    "retrievedCount": 1,
    "citations": [
      {
        "sourceId": "S1",
        "detailId": "...",
        "newspaperId": "...",
        "publicationName": "Daily Graphic",
        "publicationDate": "1966-02-25",
        "pageNumber": 1,
        "excerpt": "...",
        "relevance": 0.42
      }
    ],
    "disclaimer": "AI-generated responses are research aids ..."
  }
}
```

`generationStatus` is `completed`, `disabled`, `not_configured`, `provider_unavailable`, or `skipped_no_results`. The API still returns retrieved citations if the model provider is unavailable. `answerMode` is `generated`, `extractive`, or `retrieval_only`.

## Configuration

The module uses environment variables so provider credentials do not need to be committed in `config.json`:

```sh
export GNP_RAG_API_KEY='...'
export GNP_RAG_BASE_URL='https://api.openai.com'   # origin only; optional
export GNP_RAG_CHAT_COMPLETIONS_PATH='/v1/chat/completions' # optional
export GNP_RAG_MODEL='gpt-4o-mini'                  # optional; defaults to this
export GNP_RAG_ENABLED='true'                       # optional; defaults to true
```

Any OpenAI-compatible service can be used by setting `GNP_RAG_BASE_URL` to its origin (scheme and host, without a path) and, if needed, `GNP_RAG_CHAT_COMPLETIONS_PATH` to its chat-completions route. Alternatively, the `custom_config.ArchiveRag` object may define `Enabled`, `ApiKey`, `BaseUrl`, `ChatCompletionsPath`, and `Model`; environment variables take precedence. Keep `ApiKey` in a secret manager/environment rather than the checked-in configuration. If `GNP_RAG_API_KEY` and the configured `ApiKey` are both absent, the module makes no external model call and returns retrieved excerpts.

Retrieved OCR excerpts are sent to the configured model provider only when generation is enabled and an API key is configured. Review the provider's retention and data-processing terms before enabling it for archival research.

## Retrieval and database setup

The query is tokenized into a safe PostgreSQL `tsquery`, then matched and ranked against `page_text` plus `supporting_text` using the English text-search configuration. Terms are ORed to suit natural-language questions and imperfect OCR; publication/date filters are applied before the top results are selected. User values are bound parameters, not SQL fragments.

Apply the existing GIN index migration on the target database:

```sh
psql "$DATABASE_URL" -f sql/newspaper_details_search_index.sql
```

The indexed `to_tsvector` expression must remain in sync with the RAG retrieval SQL and the existing archive-search implementation. The RAG endpoint is a lexical first release (not a vector/embedding retriever): rare names, alternate spellings and OCR errors may still require trying a second query. Source excerpts and conservative prompts help users verify answers, but do not replace checking the original scan.

## Integration notes

- `services/rag/RagManager` contains retrieval and optional generation.
- `GnpServicePlugin` owns the manager; `NewsPapersController` exposes it at the endpoint above.
- The existing archive GIN/date indexes are reused; no new database extension or schema migration is required.
- The endpoint is JWT-protected to limit access to model-backed generation. Add infrastructure-level per-user rate limiting and usage monitoring before production rollout.
