/**
 * Loads puzzle vocabulary from newspaper publications stored on the server.
 *
 * Preference order:
 *   1. newspaper_details.page_text / supporting_text (OCR or extracted page text)
 *   2. edition title, description, and featured stories
 *   3. MuPDF text extraction from the PDF in G3 storage
 */
#pragma once

#include <json/json.h>
#include <string>
#include <vector>

#include <drogon/utils/coroutine.h>

#include "services/games/Corpus.h"

namespace gnp::services {

class PublicationLexicon {
public:
    static drogon::Task<Corpus> load(const std::string &publicationId,
                                     const std::string &newspaperId,
                                     const std::vector<std::string> &topics);

    static drogon::Task<Json::Value> listSources(const std::string &publicationId, int limit);

    static void clearCache();
};

} // namespace gnp::services
