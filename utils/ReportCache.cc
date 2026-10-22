//
// Short-TTL Redis report cache
// (see docs/REPORTS_IMPLEMENTATION_PLAN_AND_API_SPEC.md §1.2 / §1.5 Phase 4).
//

#include "ReportCache.h"

#include <openssl/sha.h>

#include <cstdio>
#include <cstring>
#include <memory>

namespace gnp::utils {

std::string ReportCache::serialize(const Json::Value& value) {
    static thread_local Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "";
    return Json::writeString(writerBuilder, value);
}

std::string ReportCache::sha256Hex(const std::string& payload) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(payload.data()), payload.size(), digest);
    char hex[SHA256_DIGEST_LENGTH * 2 + 1];
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        std::snprintf(hex + (i * 2), 3, "%02x", digest[i]);
    }
    hex[SHA256_DIGEST_LENGTH * 2] = '\0';
    return std::string(hex);
}

std::string ReportCache::makeKey(const std::string& slug, const Json::Value& normalizedFilterJson) {
    return "gnp:report:" + slug + ":" + sha256Hex(serialize(normalizedFilterJson));
}

drogon::Task<std::optional<Json::Value>> ReportCache::get(const std::string& key) {
    auto redisClient = drogon::app().getRedisClient();
    if (!redisClient) {
        co_return std::nullopt;
    }

    try {
        auto result = co_await redisClient->execCommandCoro("GET %s", key.c_str());
        if (result.type() == drogon::nosql::RedisResultType::kNil) {
            co_return std::nullopt;
        }
        if (result.type() != drogon::nosql::RedisResultType::kString) {
            co_return std::nullopt;
        }
        const std::string raw = result.asString();
        if (raw.empty()) {
            co_return std::nullopt;
        }

        Json::Value parsed;
        Json::CharReaderBuilder readerBuilder;
        std::string errors;
        const std::unique_ptr<Json::CharReader> reader(readerBuilder.newCharReader());
        if (!reader->parse(raw.data(), raw.data() + raw.size(), &parsed, &errors)) {
            LOG_WARN << "ReportCache: ignoring unparseable cache entry " << key
                     << " (" << errors << ")";
            co_return std::nullopt;
        }
        co_return parsed;
    } catch (const std::exception& e) {
        LOG_WARN << "ReportCache GET error for " << key << ": " << e.what();
        co_return std::nullopt;
    }
}

drogon::Task<void> ReportCache::set(const std::string& key, const Json::Value& value, int ttlSeconds) {
    auto redisClient = drogon::app().getRedisClient();
    if (!redisClient || ttlSeconds <= 0) {
        co_return;
    }

    try {
        const std::string payload = serialize(value);
        co_await redisClient->execCommandCoro(
            "SETEX %s %d %s", key.c_str(), ttlSeconds, payload.c_str());
    } catch (const std::exception& e) {
        LOG_WARN << "ReportCache SETEX error for " << key << ": " << e.what();
    }
    co_return;
}

} // namespace gnp::utils
