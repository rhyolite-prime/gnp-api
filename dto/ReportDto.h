//
// Created by Emmanuel Addo-Odame on 23/08/2026.
//

#ifndef GNPAPI_REPORTDTO_H
#define GNPAPI_REPORTDTO_H

#include <drogon/HttpRequest.h>
#include <json/json.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <optional>
#include <string>

namespace gnp::dto {

    class ReportDto {

    public:
        ReportDto() = default;
        explicit ReportDto(const Json::Value& json);

        void fromJson(const Json::Value& json);

        // Getters
        [[nodiscard]] const std::string& getPartnerId() const { return partner_id_; }
        [[nodiscard]] const std::string& getStartDate() const { return start_date_; }
        [[nodiscard]] const std::string& getEndDate() const { return end_date_; }

        // Setters
        void setPartnerId(const std::string& value) { partner_id_ = value; }
        void setStartDate(const std::string& value) { start_date_ = value; }
        void setEndDate(const std::string& value) { end_date_ = value; }

    private:
        std::string partner_id_;
        std::string start_date_;
        std::string end_date_;
    };

    inline void ReportDto::fromJson(const Json::Value& json) {

        if (json.isMember("partnerId") && !json["partnerId"].isNull()) {
           setPartnerId(json["partnerId"].asString());
        }

        if (json.isMember("startDate") && !json["startDate"].isNull()) {
            setStartDate(json["startDate"].asString());
        }

        if (json.isMember("endDate") && !json["endDate"].isNull()) {
            setEndDate(json["endDate"].asString());
        }

    }

    /**
     * Unified request parser for every platform report endpoint (see
     * docs/REPORTS_IMPLEMENTATION_PLAN_AND_API_SPEC.md §1.2 / §2.1).
     *
     * Accepts both POST JSON bodies and GET query parameters seamlessly.
     * "all" / "" values for status, plan and channel are normalized to
     * std::nullopt so SQL can use clean parameterized predicates:
     *   WHERE ($3::text IS NULL OR LOWER(status) = LOWER($3))
     *
     * The equal-length prior comparison window (prevStartDate/prevEndDate)
     * is auto-computed for `changePct` deltas on the summary cards.
     */
    struct ReportFilterDto {
        std::string startDate;                        // YYYY-MM-DD (defaults to TODAY - 29 days)
        std::string endDate;                          // YYYY-MM-DD (defaults to TODAY)
        std::string prevStartDate;                    // Auto-computed equal-length prior window start
        std::string prevEndDate;                      // Auto-computed equal-length prior window end
        std::optional<std::string> partnerId;         // UUID or slug of commercial partner
        std::optional<std::string> publicationId;     // UUID of Graphic publication
        std::optional<std::string> affiliateId;       // UUID of affiliate account
        std::optional<std::string> status;            // Normalized: "all"/"" -> std::nullopt
        std::optional<std::string> plan;              // "standard"|"premium"|"corporate" (omits "all")
        std::optional<std::string> channel;           // "email"|"sms"|"push" (omits "all")
        int pageNo = 1;
        int pageSize = 100;
        bool bypassCache = false;

        static ReportFilterDto fromRequest(const drogon::HttpRequestPtr& req);

        // Deterministic normalized JSON used for the Redis report cache key.
        [[nodiscard]] Json::Value toJson() const;

        // Civil-date helpers (Howard Hinnant algorithms — timezone agnostic).
        static bool parseYmd(const std::string& value, int& year, int& month, int& day);
        static long long daysFromCivil(int year, int month, int day);
        static std::string civilFromDays(long long daysSinceEpoch);
        static std::string todayYmd();
    };

    // ------------------------------------------------------------------
    // ReportFilterDto implementation (header-only so every translation
    // unit shares exactly one definition, matching the ReportDto style).
    // ------------------------------------------------------------------

    inline bool ReportFilterDto::parseYmd(const std::string& value, int& year, int& month, int& day) {
        if (value.size() < 10) {
            return false;
        }
        int y = 0;
        int m = 0;
        int d = 0;
        char extra = 0;
        // Accept "YYYY-MM-DD" and "YYYY-MM-DDTHH:MM:SS..." / "YYYY-MM-DD HH:MM:SS"
        const int matched = std::sscanf(value.c_str(), "%4d-%2d-%2d%c", &y, &m, &d, &extra);
        if (matched != 3 && matched != 4) {
            return false;
        }
        if (matched == 4 && extra != 'T' && extra != ' ' && extra != 'Z') {
            return false;
        }
        if (m < 1 || m > 12 || d < 1 || d > 31 || y < 1970 || y > 2999) {
            return false;
        }
        year = y;
        month = m;
        day = d;
        return true;
    }

    inline long long ReportFilterDto::daysFromCivil(int y, int m, int d) {
        y -= m <= 2;
        const long long era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(y - era * 400);                       // [0, 399]
        const unsigned doy = static_cast<unsigned>(153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; // [0, 365]
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                      // [0, 146096]
        return era * 146097 + static_cast<long long>(doe) - 719468;
    }

    inline std::string ReportFilterDto::civilFromDays(long long z) {
        z += 719468;
        const long long era = (z >= 0 ? z : z - 146096) / 146097;
        const unsigned doe = static_cast<unsigned>(z - era * 146097);                    // [0, 146096]
        const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;      // [0, 399]
        const long long y = static_cast<long long>(yoe) + era * 400;
        const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);                    // [0, 365]
        const unsigned mp = (5 * doy + 2) / 153;                                         // [0, 11]
        const unsigned d = doy - (153 * mp + 2) / 5 + 1;                                 // [1, 31]
        const unsigned m = mp + (mp < 10 ? 3 : -9);                                      // [1, 12]
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04lld-%02u-%02u", y + (m <= 2), m, d);
        return std::string(buf);
    }

    inline std::string ReportFilterDto::todayYmd() {
        const auto now = std::chrono::system_clock::now();
        const std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm localTm{};
#if defined(_WIN32)
        localtime_s(&localTm, &tt);
#else
        localtime_r(&tt, &localTm);
#endif
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                      localTm.tm_year + 1900, localTm.tm_mon + 1, localTm.tm_mday);
        return std::string(buf);
    }

    namespace detail {

        inline std::string reportTrim(const std::string& value) {
            const auto begin = value.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) {
                return "";
            }
            const auto end = value.find_last_not_of(" \t\r\n");
            return value.substr(begin, end - begin + 1);
        }

        inline std::string reportLower(const std::string& value) {
            std::string out = value;
            for (auto& c : out) {
                const unsigned char uc = static_cast<unsigned char>(c);
                if (uc >= 'A' && uc <= 'Z') {
                    c = static_cast<char>(uc + 32);
                }
            }
            return out;
        }

        // Normalizes filter-ish values: ""/"all"/"any" -> std::nullopt
        inline std::optional<std::string> reportNormalizeOptional(const std::string& raw) {
            const std::string trimmed = reportTrim(raw);
            if (trimmed.empty()) {
                return std::nullopt;
            }
            const std::string lowered = reportLower(trimmed);
            if (lowered == "all" || lowered == "any") {
                return std::nullopt;
            }
            return lowered;
        }

        // Reads a field from the JSON body first, falling back to the query string.
        inline std::string reportPick(const Json::Value& body,
                                      const drogon::HttpRequestPtr& req,
                                      const std::string& field) {
            if (body.isMember(field) && !body[field].isNull()) {
                if (body[field].isString()) {
                    return body[field].asString();
                }
                if (body[field].isIntegral()) {
                    return std::to_string(body[field].asInt64());
                }
                if (body[field].isDouble()) {
                    return std::to_string(body[field].asDouble());
                }
            }
            if (req) {
                const std::string param = req->getParameter(field);
                if (!param.empty()) {
                    return param;
                }
            }
            return "";
        }

    } // namespace detail

    inline ReportFilterDto ReportFilterDto::fromRequest(const drogon::HttpRequestPtr& req) {
        using detail::reportNormalizeOptional;
        using detail::reportPick;
        using detail::reportTrim;

        ReportFilterDto filter;

        Json::Value body;
        if (req) {
            const auto jsonPtr = req->getJsonObject();
            if (jsonPtr) {
                body = *jsonPtr;
            }
        }

        // ---- Optional identifier / enum filters ------------------------------
        const std::string partnerId = reportTrim(reportPick(body, req, "partnerId"));
        if (!partnerId.empty()) {
            filter.partnerId = partnerId;
        }
        const std::string publicationId = reportTrim(reportPick(body, req, "publicationId"));
        if (!publicationId.empty()) {
            filter.publicationId = publicationId;
        }
        const std::string affiliateId = reportTrim(reportPick(body, req, "affiliateId"));
        if (!affiliateId.empty()) {
            filter.affiliateId = affiliateId;
        }
        filter.status = reportNormalizeOptional(reportPick(body, req, "status"));
        filter.plan = reportNormalizeOptional(reportPick(body, req, "plan"));
        filter.channel = reportNormalizeOptional(reportPick(body, req, "channel"));

        // ---- Pagination -------------------------------------------------------
        try {
            const std::string pageNoStr = reportPick(body, req, "pageNo");
            if (!pageNoStr.empty()) {
                filter.pageNo = std::max(1, std::stoi(pageNoStr));
            }
        } catch (...) {
            filter.pageNo = 1;
        }
        try {
            const std::string pageSizeStr = reportPick(body, req, "pageSize");
            if (!pageSizeStr.empty()) {
                filter.pageSize = std::max(1, std::min(1000, std::stoi(pageSizeStr)));
            }
        } catch (...) {
            filter.pageSize = 100;
        }

        // ---- bypassCache ------------------------------------------------------
        const std::string bypass = detail::reportLower(reportTrim(reportPick(body, req, "bypassCache")));
        filter.bypassCache = (bypass == "true" || bypass == "1" || bypass == "yes");

        // ---- Date window (defaults: last 30 days inclusive) -------------------
        const std::string today = todayYmd();

        std::string startDate = reportTrim(reportPick(body, req, "startDate"));
        std::string endDate = reportTrim(reportPick(body, req, "endDate"));

        int sy = 0;
        int sm = 0;
        int sd = 0;
        if (!parseYmd(startDate, sy, sm, sd)) {
            startDate.clear();
        } else {
            startDate = civilFromDays(daysFromCivil(sy, sm, sd));
        }

        int ey = 0;
        int em = 0;
        int ed = 0;
        if (!parseYmd(endDate, ey, em, ed)) {
            endDate.clear();
        } else {
            endDate = civilFromDays(daysFromCivil(ey, em, ed));
        }

        if (endDate.empty()) {
            endDate = today;
        }
        if (startDate.empty()) {
            int ty = 0;
            int tm = 0;
            int td = 0;
            parseYmd(endDate, ty, tm, td);
            startDate = civilFromDays(daysFromCivil(ty, tm, td) - 29);
        }

        // Guard against inverted windows.
        if (startDate > endDate) {
            std::swap(startDate, endDate);
        }

        filter.startDate = startDate;
        filter.endDate = endDate;

        // ---- Auto-computed equal-length prior comparison window ---------------
        int s2y = 0;
        int s2m = 0;
        int s2d = 0;
        int e2y = 0;
        int e2m = 0;
        int e2d = 0;
        parseYmd(filter.startDate, s2y, s2m, s2d);
        parseYmd(filter.endDate, e2y, e2m, e2d);
        const long long startDays = daysFromCivil(s2y, s2m, s2d);
        const long long endDays = daysFromCivil(e2y, e2m, e2d);
        const long long windowDays = endDays - startDays + 1;   // inclusive length
        filter.prevEndDate = civilFromDays(startDays - 1);
        filter.prevStartDate = civilFromDays(startDays - windowDays);

        return filter;
    }

    inline Json::Value ReportFilterDto::toJson() const {
        Json::Value json(Json::objectValue);
        json["startDate"] = startDate;
        json["endDate"] = endDate;
        json["prevStartDate"] = prevStartDate;
        json["prevEndDate"] = prevEndDate;
        json["partnerId"] = partnerId.value_or("");
        json["publicationId"] = publicationId.value_or("");
        json["affiliateId"] = affiliateId.value_or("");
        json["status"] = status.value_or("");
        json["plan"] = plan.value_or("");
        json["channel"] = channel.value_or("");
        json["pageNo"] = pageNo;
        json["pageSize"] = pageSize;
        return json;
    }

}

#endif //GNPAPI_REPORTDTO_H
