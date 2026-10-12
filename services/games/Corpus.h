/**
 * A publication corpus: words and sentences lifted from stored newspaper pages.
 * Puzzle generators may only use lexemes in this structure.
 */
#pragma once

#include <string>
#include <vector>

#include <json/json.h>

namespace gnp::services {

struct SourceRef {
    std::string newspaperId;
    std::string publicationId;
    std::string publicationName;
    std::string newspaperTitle;
    std::string publicationDate;
    int pageNumber = 0;

    [[nodiscard]] Json::Value toJson() const {
        Json::Value json;
        json["newspaperId"] = newspaperId;
        json["publicationId"] = publicationId;
        json["publicationName"] = publicationName;
        json["newspaperTitle"] = newspaperTitle;
        json["publicationDate"] = publicationDate;
        json["pageNumber"] = pageNumber;
        return json;
    }
};

struct Lexeme {
    std::string word;
    std::string snippet;
    std::string sentence;
    int frequency = 1;
    int score = 0;
    SourceRef source;
};

struct Corpus {
    std::vector<Lexeme> lexemes;
    std::string letterBag;
    std::vector<SourceRef> sources;
    int pageCount = 0;
    int rawChars = 0;
    bool topicsApplied = false;
    bool fromPdf = false;
    bool fromPageText = false;
};

} // namespace gnp::services
