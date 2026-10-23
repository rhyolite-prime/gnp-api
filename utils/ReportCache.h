//
// Short-TTL Redis report cache
// (see docs/REPORTS_IMPLEMENTATION_PLAN_AND_API_SPEC.md §1.2 / §1.5 Phase 4).
//
// Cache key: gnp:report:<slug>:sha256(<normalizedFilterJson>)
// TTL      : 120 seconds (partner_invoices email dispatch & real-time audit
//            logs bypass the cache entirely).
//

#ifndef GNPAPI_REPORTCACHE_H
#define GNPAPI_REPORTCACHE_H

#include <drogon/drogon.h>
#include <drogon/utils/coroutine.h>
#include <json/json.h>

#include <optional>
#include <string>

namespace gnp::utils {

class ReportCache {
public:
    // Default TTL for analytical report payloads.
    static constexpr int kTtlSeconds = 120;

    // Deterministic cache key: "gnp:report:<slug>:sha256(normalizedFilterJson)"
    static std::string makeKey(const std::string& slug, const Json::Value& normalizedFilterJson);

    // sha256 (lowercase hex) of the UTF-8 serialized filter JSON.
    static std::string sha256Hex(const std::string& payload);

    // Returns the cached result payload, or std::nullopt on miss/error.
    static drogon::Task<std::optional<Json::Value>> get(const std::string& key);

    // Fire-and-forget write with the standard 120 second TTL.
    static drogon::Task<void> set(const std::string& key, const Json::Value& value, int ttlSeconds = kTtlSeconds);

private:
    // JSON serialization shared by makeKey/get/set.
    static std::string serialize(const Json::Value& value);
};

} // namespace gnp::utils

#endif //GNPAPI_REPORTCACHE_H
