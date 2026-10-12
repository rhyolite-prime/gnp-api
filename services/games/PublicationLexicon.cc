#include "services/games/PublicationLexicon.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <drogon/drogon.h>
#include <mupdf/fitz.h>

#include "services/games/GameTypes.h"

namespace gnp::services {
namespace {

std::mutex gCacheMutex;
struct CacheEntry {
    Corpus corpus;
    std::chrono::steady_clock::time_point loadedAt;
};
std::unordered_map<std::string, CacheEntry> gCache;
constexpr int kCacheTtlSeconds = 1800;
constexpr int kMaxRawChars = 160000;
constexpr int kMaxPages = 24;
constexpr int kMaxPdfPages = 8;

const std::unordered_set<std::string> kStopwords = {
    "THE", "AND", "FOR", "ARE", "BUT", "NOT", "YOU", "ALL", "CAN", "HER", "WAS", "ONE", "OUR", "OUT",
    "HAS", "HAVE", "HAD", "HIS", "HERS", "ITS", "THEY", "THEM", "THEIR", "THIS", "THAT", "WITH", "FROM",
    "WERE", "BEEN", "WILL", "WOULD", "COULD", "SHOULD", "ABOUT", "AFTER", "BEFORE", "THERE", "WHERE",
    "WHICH", "WHILE", "WHAT", "WHEN", "YOUR", "YOURS", "INTO", "OVER", "UNDER", "ALSO", "THAN", "THEN",
    "THESE", "THOSE", "SAID", "SAYS", "SAY", "JUST", "LIKE", "MORE", "MOST", "SOME", "SUCH", "ONLY",
    "OTHER", "BEING", "EACH", "BOTH", "MANY", "MUCH", "VERY", "EVEN", "BACK", "DOWN", "UPON", "AMONG",
    "PAGE", "PAGES", "EDITION", "COPYRIGHT", "ADVERTISEMENT", "ADVERT", "CONTINUED", "GRAPHIC", "NEWS",
    "PLUS", "HTTP", "HTTPS", "WWW", "COM", "ORG", "EMAIL", "PHOTO", "PHOTOS", "IMAGE", "IMAGES", "CAPTION",
    "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY", "SUNDAY", "JANUARY", "FEBRUARY",
    "MARCH", "APRIL", "JUNE", "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER",
    "TODAY", "YESTERDAY", "TOMORROW", "WEEK", "WEEKS", "YEAR", "YEARS", "MONTH", "MONTHS",
    "SAYS", "TOLD", "ACCORDING", "REPORT", "REPORTS", "REPORTED", "STORY", "STORIES"
};

bool isStopword(const std::string &word) {
    return kStopwords.count(word) > 0;
}

std::string toUpperAscii(std::string value) {
    for (char &c : value) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc >= 'a' && uc <= 'z') c = static_cast<char>(uc - 'a' + 'A');
    }
    return value;
}

std::string trimCopy(const std::string &value) {
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(begin, end - begin);
}

std::string collapseSpace(const std::string &value) {
    std::string out;
    out.reserve(value.size());
    bool pendingSpace = false;
    for (unsigned char c : value) {
        if (c == '\r' || c == '\n' || c == '\t' || std::isspace(c)) {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace) {
            out.push_back(' ');
            pendingSpace = false;
        }
        out.push_back(static_cast<char>(c));
    }
    return out;
}

bool isAsciiLetter(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool hasVowel(const std::string &word) {
    for (char c : word) {
        if (c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U' || c == 'Y') return true;
    }
    return false;
}

int scoreWord(const std::string &word, int frequency, bool hasSentence) {
    int score = 0;
    const int n = static_cast<int>(word.size());
    if (n >= 5 && n <= 8) score += 6;
    else if (n >= 4 && n <= 11) score += 3;
    else score += 1;
    if (frequency >= 2 && frequency <= 5) score += 4;
    else if (frequency == 1) score += 2;
    else score += 1;
    if (hasSentence) score += 3;
    return score;
}

std::string snippetAround(const std::string &text, const std::string &word) {
    const std::string upper = toUpperAscii(text);
    const auto pos = upper.find(word);
    if (pos == std::string::npos) {
        if (text.size() <= 160) return text;
        return text.substr(0, 157) + "...";
    }
    const size_t begin = pos > 70 ? pos - 70 : 0;
    size_t end = std::min(text.size(), pos + word.size() + 70);
    std::string snippet = trimCopy(text.substr(begin, end - begin));
    if (begin > 0) snippet = "..." + snippet;
    if (end < text.size()) snippet += "...";
    if (snippet.size() > 180) snippet = snippet.substr(0, 177) + "...";
    return snippet;
}

void collectStrings(const Json::Value &node, std::string &out) {
    if (node.isString()) {
        out.push_back(' ');
        out += node.asString();
        return;
    }
    if (node.isArray()) {
        for (const auto &item : node) collectStrings(item, out);
        return;
    }
    if (node.isObject()) {
        for (const auto &name : node.getMemberNames()) collectStrings(node[name], out);
    }
}

std::string extractPdfText(const std::string &path, int maxPages) {
    if (!std::filesystem::exists(path)) return "";

    fz_context *ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
    if (!ctx) return "";
    fz_register_document_handlers(ctx);

    fz_document *doc = nullptr;
    char *text = nullptr;
    size_t textLen = 0;
    fz_var(doc);
    fz_var(text);

    fz_try(ctx) {
        doc = fz_open_document(ctx, path.c_str());
        const int pages = fz_count_pages(ctx, doc);
        const int limit = std::min(pages, maxPages);
        fz_buffer *joined = fz_new_buffer(ctx, 1024);
        for (int i = 0; i < limit; ++i) {
            fz_stext_page *stext = fz_new_stext_page_from_page_number(ctx, doc, i, nullptr);
            fz_buffer *pageBuf = fz_new_buffer_from_stext_page(ctx, stext);
            fz_append_buffer(ctx, joined, pageBuf);
            fz_append_byte(ctx, joined, '\n');
            fz_drop_buffer(ctx, pageBuf);
            fz_drop_stext_page(ctx, stext);
            if (fz_buffer_storage(ctx, joined, nullptr) > static_cast<size_t>(kMaxRawChars)) break;
        }
        unsigned char *data = nullptr;
        const size_t len = fz_buffer_storage(ctx, joined, &data);
        text = static_cast<char *>(malloc(len + 1));
        if (text && data) {
            memcpy(text, data, len);
            text[len] = '\0';
            textLen = len;
        }
        fz_drop_buffer(ctx, joined);
    }
    fz_always(ctx) {
        fz_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        LOG_ERROR << "[games] MuPDF text extraction failed for " << path << ": " << fz_caught_message(ctx);
        free(text);
        text = nullptr;
        textLen = 0;
    }
    fz_drop_context(ctx);

    std::string result;
    if (text) {
        result.assign(text, textLen);
        free(text);
    }
    return result;
}

std::vector<std::string> candidatePdfPaths(const std::string &documentId, const std::string &storageService) {
    std::vector<std::string> paths;
    if (documentId.empty()) return paths;
    if (documentId.find("..") != std::string::npos) return paths;

    auto custom = drogon::app().getCustomConfig();
    std::string base = "./g3-storage";
    std::vector<std::string> buckets;
    if (custom.isMember("G3Bucket")) {
        const auto &bucket = custom["G3Bucket"];
        if (bucket.isMember("BaseStoragePath") && !bucket["BaseStoragePath"].asString().empty()) {
            base = bucket["BaseStoragePath"].asString();
        }
        if (bucket.isMember("MasterDocumentBucketName")) buckets.push_back(bucket["MasterDocumentBucketName"].asString());
        if (bucket.isMember("SplitDocumentBucketName")) buckets.push_back(bucket["SplitDocumentBucketName"].asString());
    }
    const std::string service = lowerCopy(storageService);
    if (!storageService.empty() && service.find("google") == std::string::npos &&
        service.find("http") == std::string::npos && service.find("drive") == std::string::npos &&
        storageService.find('/') == std::string::npos && storageService.find("..") == std::string::npos) {
        buckets.push_back(storageService);
    }
    buckets.emplace_back("gnp-master-documents");
    buckets.emplace_back("newspapers");
    buckets.emplace_back("publications");

    if (std::filesystem::exists(documentId)) paths.push_back(documentId);

    auto addFile = [&](const std::string &bucket, const std::string &fileName) {
        if (bucket.empty() || fileName.empty()) return;
        if (fileName.find("..") != std::string::npos || fileName.find('/') != std::string::npos) return;
        paths.push_back((std::filesystem::path(base) / bucket / fileName).string());
    };

    std::vector<std::string> names{documentId};
    if (documentId.size() < 4 || lowerCopy(documentId.substr(documentId.size() - 4)) != ".pdf") {
        names.push_back(documentId + ".pdf");
        names.push_back(documentId + ".PDF");
    }

    for (const auto &bucket : buckets) {
        for (const auto &name : names) addFile(bucket, name);
    }
    return paths;
}

struct PageBlob {
    std::string text;
    SourceRef source;
    bool fromPdf = false;
};

void absorbText(std::unordered_map<std::string, Lexeme> &words,
                 std::string &letterBag,
                 const std::string &raw,
                 const SourceRef &source) {
    const std::string text = collapseSpace(raw);
    if (text.size() < 20) return;

    std::string current;
    auto flush = [&](size_t endIndex) {
        if (current.size() < 4 || current.size() > 12) {
            current.clear();
            return;
        }
        if (!hasVowel(current) || isStopword(current)) {
            current.clear();
            return;
        }
        bool same = true;
        for (char c : current) {
            if (c != current.front()) {
                same = false;
                break;
            }
        }
        if (same) {
            current.clear();
            return;
        }

        const size_t wordStart = endIndex >= current.size() ? endIndex - current.size() : 0;
        std::string sentence = text;
        size_t left = wordStart;
        while (left > 0 && text[left] != '.' && text[left] != '!' && text[left] != '?' && text[left] != '\n') --left;
        if (left > 0 && left < text.size()) ++left;
        size_t right = wordStart;
        while (right < text.size() && text[right] != '.' && text[right] != '!' && text[right] != '?') ++right;
        if (right < text.size()) ++right;
        sentence = trimCopy(text.substr(left, right - left));
        if (sentence.size() > 220) sentence = snippetAround(text, current);
        if (sentence.size() < 20) sentence = snippetAround(text, current);

        auto it = words.find(current);
        if (it == words.end()) {
            Lexeme lexeme;
            lexeme.word = current;
            lexeme.frequency = 1;
            lexeme.sentence = sentence;
            lexeme.snippet = snippetAround(sentence.empty() ? text : sentence, current);
            lexeme.source = source;
            lexeme.score = scoreWord(current, 1, !sentence.empty());
            words.emplace(current, std::move(lexeme));
        } else {
            it->second.frequency += 1;
            if (it->second.sentence.size() < sentence.size()) {
                it->second.sentence = sentence;
                it->second.snippet = snippetAround(sentence, current);
                it->second.source = source;
            }
            it->second.score = scoreWord(it->second.word, it->second.frequency, !it->second.sentence.empty());
        }
        current.clear();
    };

    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (isAsciiLetter(c)) {
            char upper = static_cast<char>(std::toupper(c));
            current.push_back(upper);
            if (letterBag.size() < 4000) letterBag.push_back(upper);
            continue;
        }
        flush(i);
    }
    flush(text.size());
}

Corpus buildCorpus(const std::vector<PageBlob> &pages, const std::vector<std::string> &topics) {
    std::unordered_map<std::string, Lexeme> words;
    std::string letterBag;
    std::vector<SourceRef> sources;
    int rawChars = 0;
    bool fromPdf = false;
    bool fromPageText = false;

    for (const auto &page : pages) {
        if (rawChars > kMaxRawChars) break;
        rawChars += static_cast<int>(page.text.size());
        fromPdf = fromPdf || page.fromPdf;
        fromPageText = fromPageText || !page.fromPdf;
        absorbText(words, letterBag, page.text, page.source);
        if (!page.source.newspaperId.empty() || !page.source.newspaperTitle.empty()) {
            bool seen = false;
            for (const auto &existing : sources) {
                if (existing.newspaperId == page.source.newspaperId && existing.pageNumber == page.source.pageNumber) {
                    seen = true;
                    break;
                }
            }
            if (!seen) sources.push_back(page.source);
        }
    }

    std::vector<std::string> topicNeedles;
    for (const auto &topic : topics) {
        std::string needle = toUpperAscii(trimCopy(topic));
        if (needle.size() >= 3) topicNeedles.push_back(needle);
    }

    Corpus corpus;
    corpus.letterBag = letterBag;
    corpus.sources = sources;
    corpus.pageCount = static_cast<int>(pages.size());
    corpus.rawChars = rawChars;
    corpus.fromPdf = fromPdf;
    corpus.fromPageText = fromPageText;
    corpus.topicsApplied = !topicNeedles.empty();

    for (auto &entry : words) {
        if (!topicNeedles.empty()) {
            const std::string haystack = toUpperAscii(entry.second.sentence + " " + entry.second.snippet + " " + entry.second.word);
            bool matched = false;
            for (const auto &needle : topicNeedles) {
                if (haystack.find(needle) != std::string::npos) {
                    matched = true;
                    break;
                }
            }
            if (!matched) continue;
        }
        corpus.lexemes.push_back(std::move(entry.second));
    }

    std::sort(corpus.lexemes.begin(), corpus.lexemes.end(), [](const Lexeme &a, const Lexeme &b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.frequency != b.frequency) return a.frequency > b.frequency;
        return a.word < b.word;
    });
    if (corpus.lexemes.size() > 400) corpus.lexemes.resize(400);
    return corpus;
}

std::string cacheKey(const std::string &publicationId,
                     const std::string &newspaperId,
                     const std::vector<std::string> &topics) {
    std::ostringstream key;
    key << publicationId << "|" << newspaperId;
    for (const auto &topic : topics) key << "|" << lowerCopy(topic);
    return key.str();
}

} // namespace

void PublicationLexicon::clearCache() {
    std::lock_guard<std::mutex> lock(gCacheMutex);
    gCache.clear();
}

drogon::Task<Corpus> PublicationLexicon::load(const std::string &publicationId,
                                              const std::string &newspaperId,
                                              const std::vector<std::string> &topics) {
    const std::string key = cacheKey(publicationId, newspaperId, topics);
    {
        std::lock_guard<std::mutex> lock(gCacheMutex);
        auto it = gCache.find(key);
        if (it != gCache.end()) {
            const auto age = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::steady_clock::now() - it->second.loadedAt)
                                 .count();
            if (age < kCacheTtlSeconds && it->second.corpus.lexemes.size() >= 8) {
                co_return it->second.corpus;
            }
        }
    }

    auto db = drogon::app().getDbClient();
    if (!db) {
        throw std::runtime_error("LEXICON: Database client is not configured.");
    }

    std::string sql =
        "SELECT nd.page_text, nd.supporting_text, nd.page_number, "
        "nd.publication_name, nd.publication_id::text AS publication_id, nd.newspaper_id::text AS newspaper_id, "
        "n.title, n.document_id, n.storage_service, n.full_description, "
        "COALESCE(n.publication_date::text, nd.publication_date::text, '') AS publication_date, "
        "n.featured_stories::text AS featured_stories "
        "FROM newspaper_details nd "
        "JOIN newspapers n ON n.id = nd.newspaper_id "
        "WHERE COALESCE(n.is_published, FALSE) = TRUE "
        "AND COALESCE(n.is_archived, FALSE) = FALSE "
        "AND nd.page_text IS NOT NULL AND length(nd.page_text) > 40 ";
    if (!publicationId.empty()) sql += "AND nd.publication_id = $1::uuid ";
    if (!newspaperId.empty()) sql += publicationId.empty() ? "AND nd.newspaper_id = $1::uuid " : "AND nd.newspaper_id = $2::uuid ";
    sql += "ORDER BY n.publication_date DESC NULLS LAST, nd.page_number ASC LIMIT " + std::to_string(kMaxPages);

    drogon::orm::Result pageRows(nullptr);
    try {
        if (!publicationId.empty() && !newspaperId.empty()) {
            pageRows = co_await db->execSqlCoro(sql, publicationId, newspaperId);
        } else if (!publicationId.empty()) {
            pageRows = co_await db->execSqlCoro(sql, publicationId);
        } else if (!newspaperId.empty()) {
            pageRows = co_await db->execSqlCoro(sql, newspaperId);
        } else {
            pageRows = co_await db->execSqlCoro(sql);
        }
    } catch (const std::exception &e) {
        throw std::runtime_error(std::string("LEXICON: Failed to read newspaper page text: ") + e.what());
    }

    std::vector<PageBlob> pages;
    std::unordered_map<std::string, SourceRef> pdfCandidates;

    for (size_t i = 0; i < pageRows.size(); ++i) {
        const auto &row = pageRows[i];
        SourceRef source;
        if (!row["newspaper_id"].isNull()) source.newspaperId = row["newspaper_id"].as<std::string>();
        if (!row["publication_id"].isNull()) source.publicationId = row["publication_id"].as<std::string>();
        if (!row["publication_name"].isNull()) source.publicationName = row["publication_name"].as<std::string>();
        if (!row["title"].isNull()) source.newspaperTitle = row["title"].as<std::string>();
        if (!row["publication_date"].isNull()) source.publicationDate = row["publication_date"].as<std::string>();
        if (!row["page_number"].isNull()) source.pageNumber = row["page_number"].as<int>();

        std::string text;
        if (!row["page_text"].isNull()) text += row["page_text"].as<std::string>();
        if (!row["supporting_text"].isNull()) {
            text.push_back('\n');
            text += row["supporting_text"].as<std::string>();
        }
        if (!row["full_description"].isNull()) {
            text.push_back('\n');
            text += row["full_description"].as<std::string>();
        }
        if (!row["featured_stories"].isNull()) {
            const std::string featured = row["featured_stories"].as<std::string>();
            Json::Value parsed;
            Json::CharReaderBuilder builder;
            std::string errs;
            std::istringstream stream(featured);
            if (Json::parseFromStream(builder, stream, &parsed, &errs)) {
                std::string extra;
                collectStrings(parsed, extra);
                text += extra;
            } else {
                text.push_back('\n');
                text += featured;
            }
        }
        if (!text.empty()) {
            pages.push_back(PageBlob{text, source, false});
        }
        if (!row["document_id"].isNull()) {
            SourceRef pdfSource = source;
            pdfSource.pageNumber = 0;
            const std::string documentId = row["document_id"].as<std::string>();
            const std::string storage = row["storage_service"].isNull() ? "" : row["storage_service"].as<std::string>();
            if (!documentId.empty() && pdfCandidates.find(documentId) == pdfCandidates.end()) {
                pdfSource.newspaperTitle = source.newspaperTitle;
                pdfCandidates.emplace(documentId + "\n" + storage, pdfSource);
            }
        }
    }

    Corpus corpus = buildCorpus(pages, topics);
    if (corpus.lexemes.size() < 8) {
        LOG_INFO << "[games] Page text yielded " << corpus.lexemes.size()
                 << " words. Falling back to PDF extraction.";
        int extracted = 0;
        for (const auto &entry : pdfCandidates) {
            if (extracted >= 3 || corpus.lexemes.size() >= 40) break;
            const auto split = entry.first.find('\n');
            const std::string documentId = entry.first.substr(0, split);
            const std::string storage = entry.first.substr(split + 1);
            for (const auto &path : candidatePdfPaths(documentId, storage)) {
                std::string pdfText = extractPdfText(path, kMaxPdfPages);
                if (pdfText.size() < 40) continue;
                SourceRef source = entry.second;
                pages.push_back(PageBlob{pdfText, source, true});
                ++extracted;
                LOG_INFO << "[games] Extracted " << pdfText.size() << " chars from " << path;
                break;
            }
        }

        if (pages.empty() || extracted > 0) {
            // Also pull recent edition metadata when page rows were missing entirely.
            if (pageRows.empty()) {
                std::string editionSql =
                    "SELECT id::text AS newspaper_id, title, document_id, storage_service, full_description, "
                    "publication_name, publication_id::text AS publication_id, "
                    "COALESCE(publication_date::text, '') AS publication_date, featured_stories::text AS featured_stories "
                    "FROM newspapers "
                    "WHERE COALESCE(is_published, FALSE) = TRUE AND COALESCE(is_archived, FALSE) = FALSE ";
                if (!publicationId.empty()) editionSql += "AND publication_id = $1::uuid ";
                if (!newspaperId.empty()) editionSql += publicationId.empty() ? "AND id = $1::uuid " : "AND id = $2::uuid ";
                editionSql += "ORDER BY publication_date DESC NULLS LAST LIMIT 5";
                drogon::orm::Result editions(nullptr);
                if (!publicationId.empty() && !newspaperId.empty()) editions = co_await db->execSqlCoro(editionSql, publicationId, newspaperId);
                else if (!publicationId.empty()) editions = co_await db->execSqlCoro(editionSql, publicationId);
                else if (!newspaperId.empty()) editions = co_await db->execSqlCoro(editionSql, newspaperId);
                else editions = co_await db->execSqlCoro(editionSql);

                for (size_t i = 0; i < editions.size() && extracted < 3; ++i) {
                    const auto &row = editions[i];
                    SourceRef source;
                    source.newspaperId = row["newspaper_id"].isNull() ? "" : row["newspaper_id"].as<std::string>();
                    source.publicationId = row["publication_id"].isNull() ? "" : row["publication_id"].as<std::string>();
                    source.publicationName = row["publication_name"].isNull() ? "" : row["publication_name"].as<std::string>();
                    source.newspaperTitle = row["title"].isNull() ? "" : row["title"].as<std::string>();
                    source.publicationDate = row["publication_date"].isNull() ? "" : row["publication_date"].as<std::string>();
                    std::string text = source.newspaperTitle + "\n" + source.publicationName;
                    if (!row["full_description"].isNull()) text += "\n" + row["full_description"].as<std::string>();
                    if (!row["featured_stories"].isNull()) text += "\n" + row["featured_stories"].as<std::string>();
                    if (!row["document_id"].isNull()) {
                        const std::string documentId = row["document_id"].as<std::string>();
                        const std::string storage = row["storage_service"].isNull() ? "" : row["storage_service"].as<std::string>();
                        for (const auto &path : candidatePdfPaths(documentId, storage)) {
                            std::string pdfText = extractPdfText(path, kMaxPdfPages);
                            if (pdfText.size() < 40) continue;
                            text += "\n" + pdfText;
                            ++extracted;
                            break;
                        }
                    }
                    pages.push_back(PageBlob{text, source, true});
                }
            }
            corpus = buildCorpus(pages, topics);
        }
    }

    if (corpus.lexemes.size() < 8 && !topics.empty()) {
        throw std::runtime_error("LEXICON: Not enough publication words matched the requested topics.");
    }
    if (corpus.lexemes.size() < 8) {
        throw std::runtime_error(
            "LEXICON: Not enough text in stored newspaper publications to build a puzzle. "
            "Publish an edition with page text or a PDF in G3 storage.");
    }

    {
        std::lock_guard<std::mutex> lock(gCacheMutex);
        gCache[key] = CacheEntry{corpus, std::chrono::steady_clock::now()};
    }
    LOG_INFO << "[games] Lexicon ready: " << corpus.lexemes.size() << " words from " << corpus.pageCount
             << " pages (pdf=" << corpus.fromPdf << ", pageText=" << corpus.fromPageText << ")";
    co_return corpus;
}

drogon::Task<Json::Value> PublicationLexicon::listSources(const std::string &publicationId, int limit) {
    auto db = drogon::app().getDbClient();
    Json::Value data(Json::arrayValue);
    if (!db) co_return data;
    if (limit < 1) limit = 1;
    if (limit > 30) limit = 30;

    std::string sql =
        "SELECT n.id::text AS newspaper_id, n.title, n.publication_name, n.publication_id::text AS publication_id, "
        "COALESCE(n.publication_date::text, '') AS publication_date, "
        "COUNT(nd.id)::int AS pages_with_text, "
        "COALESCE(SUM(length(nd.page_text)), 0)::int AS text_chars, "
        "CASE WHEN n.document_id IS NULL OR n.document_id = '' THEN FALSE ELSE TRUE END AS has_document "
        "FROM newspapers n "
        "LEFT JOIN newspaper_details nd ON nd.newspaper_id = n.id AND nd.page_text IS NOT NULL AND length(nd.page_text) > 40 "
        "WHERE COALESCE(n.is_published, FALSE) = TRUE AND COALESCE(n.is_archived, FALSE) = FALSE ";
    if (!publicationId.empty()) sql += "AND n.publication_id = $1::uuid ";
    sql += "GROUP BY n.id, n.title, n.publication_name, n.publication_id, n.publication_date, n.document_id "
           "ORDER BY n.publication_date DESC NULLS LAST LIMIT " +
           std::to_string(limit);

    drogon::orm::Result rows(nullptr);
    if (!publicationId.empty()) rows = co_await db->execSqlCoro(sql, publicationId);
    else rows = co_await db->execSqlCoro(sql);

    for (size_t i = 0; i < rows.size(); ++i) {
        const auto &row = rows[i];
        Json::Value item;
        item["newspaperId"] = row["newspaper_id"].isNull() ? "" : row["newspaper_id"].as<std::string>();
        item["title"] = row["title"].isNull() ? "" : row["title"].as<std::string>();
        item["publicationName"] = row["publication_name"].isNull() ? "" : row["publication_name"].as<std::string>();
        item["publicationId"] = row["publication_id"].isNull() ? "" : row["publication_id"].as<std::string>();
        item["publicationDate"] = row["publication_date"].isNull() ? "" : row["publication_date"].as<std::string>();
        item["pagesWithText"] = row["pages_with_text"].isNull() ? 0 : row["pages_with_text"].as<int>();
        item["textChars"] = row["text_chars"].isNull() ? 0 : row["text_chars"].as<int>();
        item["hasDocument"] = !row["has_document"].isNull() && row["has_document"].as<bool>();
        item["ready"] = item["pagesWithText"].asInt() > 0 || item["hasDocument"].asBool();
        data.append(item);
    }
    co_return data;
}

} // namespace gnp::services
