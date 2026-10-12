-- =============================================================================
-- Full-text search support for the Graphic Archives search
-- (NewspaperService::searchArchives / POST /api/v1/news-papers/search-archives)
--
-- The search runs against newspaper_details.page_text and
-- newspaper_details.supporting_text:
--   * bare keywords   -> PostgreSQL full-text search ('english' configuration,
--                        AND semantics, stemmed, case-insensitive)
--   * "quoted text"   -> exact, case-insensitive phrase matching
--
-- IMPORTANT: the tsvector expression below MUST stay in sync with the
-- expression used in NewspaperService::searchArchives, otherwise PostgreSQL
-- will not be able to use this index for the search queries.
-- =============================================================================

CREATE INDEX IF NOT EXISTS idx_newspaper_details_archive_fts
    ON public.newspaper_details
    USING gin (to_tsvector('english', COALESCE(page_text, '') || ' ' || COALESCE(supporting_text, '')));

-- Speeds up the dateFrom / dateTo / publicationDate filters of the archive search.
CREATE INDEX IF NOT EXISTS idx_newspaper_details_publication_date
    ON public.newspaper_details (publication_date DESC);

-- -----------------------------------------------------------------------------
-- Optional, heavier optimisation for the exact quoted-phrase matching.
--
-- The phrase predicate currently uses position(lower(...) IN lower(...)) which
-- is exact but evaluated by scanning the candidate rows. If phrase search over
-- a very large archive ever becomes slow, enable pg_trgm and change the
-- predicate in NewspaperService::searchArchives to
-- lower(...) LIKE '%' || lower(phrase) || '%' — that form can use these
-- indexes:
--
-- CREATE EXTENSION IF NOT EXISTS pg_trgm;
-- CREATE INDEX IF NOT EXISTS idx_newspaper_details_page_text_trgm
--     ON public.newspaper_details USING gin ((lower(page_text)) gin_trgm_ops);
-- CREATE INDEX IF NOT EXISTS idx_newspaper_details_supporting_text_trgm
--     ON public.newspaper_details USING gin ((lower(supporting_text)) gin_trgm_ops);
-- -----------------------------------------------------------------------------
