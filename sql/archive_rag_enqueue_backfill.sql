-- Initial backfill/recovery queue: enqueue OCR pages with no complete chunk set
-- for their current page_text + supporting_text hash. Safe to rerun; pages with
-- a complete, current set of chunks are left alone.
--
-- When changing embedding models, delete the old chunks first so every page is
-- re-embedded with one consistent model, then run this script.

INSERT INTO public.newspaper_archive_rag_index_queue (detail_id)
SELECT nd.id
FROM public.newspaper_details nd
WHERE NULLIF(BTRIM(COALESCE(nd.page_text, '') || ' ' ||
                  COALESCE(nd.supporting_text, '')), '') IS NOT NULL
  AND NOT EXISTS (
      SELECT 1
      FROM public.newspaper_archive_rag_chunks c
      WHERE c.detail_id = nd.id
      GROUP BY c.source_hash, c.embedding_model, c.chunk_count
      HAVING c.source_hash = md5(
                 COALESCE(nd.page_text, '') || chr(10) || chr(10) ||
                 COALESCE(nd.supporting_text, '')
             )
         AND COUNT(*) = c.chunk_count
  )
ON CONFLICT (detail_id) DO NOTHING;
