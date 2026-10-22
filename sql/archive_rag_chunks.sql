-- Optional passage-level hybrid retrieval for Graphic Archives.
-- Requires pgvector >= 0.5 for the HNSW index and Ollama embeddings with 768
-- dimensions (the default model is nomic-embed-text). The API stays on the
-- existing page-level FTS path until GNP_RAG_VECTOR_ENABLED=true.

CREATE EXTENSION IF NOT EXISTS vector;

CREATE TABLE IF NOT EXISTS public.newspaper_archive_rag_chunks (
    detail_id UUID NOT NULL
        REFERENCES public.newspaper_details(id) ON DELETE CASCADE,
    chunk_index INTEGER NOT NULL CHECK (chunk_index >= 0),
    chunk_text TEXT NOT NULL,
    source_hash CHAR(32) NOT NULL,
    embedding_model VARCHAR(120) NOT NULL,
    embedding VECTOR(768) NOT NULL,
    chunk_count INTEGER NOT NULL CHECK (chunk_count > 0),
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (detail_id, chunk_index),
    CHECK (chunk_index < chunk_count)
);

CREATE INDEX IF NOT EXISTS idx_archive_rag_chunks_fts
    ON public.newspaper_archive_rag_chunks
    USING GIN (to_tsvector('english', chunk_text));

CREATE INDEX IF NOT EXISTS idx_archive_rag_chunks_embedding_hnsw
    ON public.newspaper_archive_rag_chunks
    USING HNSW (embedding vector_cosine_ops);

CREATE TABLE IF NOT EXISTS public.newspaper_archive_rag_index_queue (
    detail_id UUID PRIMARY KEY
        REFERENCES public.newspaper_details(id) ON DELETE CASCADE,
    queued_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    attempts INTEGER NOT NULL DEFAULT 0 CHECK (attempts >= 0),
    claimed_at TIMESTAMPTZ,
    last_error TEXT
);

CREATE INDEX IF NOT EXISTS idx_archive_rag_index_queue_pending
    ON public.newspaper_archive_rag_index_queue (queued_at)
    WHERE claimed_at IS NULL;
