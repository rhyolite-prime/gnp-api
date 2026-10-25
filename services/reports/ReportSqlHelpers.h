//
// Internal shared helpers for the ReportingService translation units.
// Not part of the public API surface.
//

#ifndef GNPAPI_REPORTSQLHELPERS_H
#define GNPAPI_REPORTSQLHELPERS_H

#include <drogon/orm/Result.h>
#include <json/json.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace gnp::services::reportdetail {

// ---- Safe column extraction (NULL-aware) -------------------------------

inline double asDouble(const drogon::orm::Row &row, const char *col) {
    if (!row[col].isNull()) {
        try {
            return row[col].as<double>();
        } catch (...) {
        }
    }
    return 0.0;
}

inline long long asLong(const drogon::orm::Row &row, const char *col) {
    if (!row[col].isNull()) {
        try {
            return static_cast<long long>(row[col].as<long long>());
        } catch (...) {
            try {
                return static_cast<long long>(row[col].as<double>());
            } catch (...) {
            }
        }
    }
    return 0;
}

inline std::string asString(const drogon::orm::Row &row, const char *col) {
    if (!row[col].isNull()) {
        try {
            return row[col].as<std::string>();
        } catch (...) {
        }
    }
    return "";
}

// ---- Numeric shaping ---------------------------------------------------

inline double round2(double value) {
    if (!std::isfinite(value)) {
        return 0.0;
    }
    return std::round(value * 100.0) / 100.0;
}

inline double pct(double part, double whole) {
    if (whole == 0.0) {
        return 0.0;
    }
    return round2((part / whole) * 100.0);
}

inline double changePct(double current, double previous) {
    if (previous == 0.0) {
        return current == 0.0 ? 0.0 : 100.0;
    }
    return round2(((current - previous) / std::fabs(previous)) * 100.0);
}

// ---- Display formatting ------------------------------------------------

// "2026-10-22" / "2026-10-22 09:42:00" -> "22 Oct"
inline std::string dayLabel(const std::string &dbDate) {
    static const char *kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int y = 0;
    int m = 0;
    int d = 0;
    char extra = 0;
    const int matched = std::sscanf(dbDate.c_str(), "%4d-%2d-%2d%c", &y, &m, &d, &extra);
    if (matched < 3 || m < 1 || m > 12) {
        return dbDate.substr(0, std::min<size_t>(dbDate.size(), 11));
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d %s", d, kMonths[m - 1]);
    return std::string(buf);
}

// "2026-10-01" -> "Oct 2026"
inline std::string monthLabel(const std::string &dbDate) {
    static const char *kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int y = 0;
    int m = 0;
    int d = 0;
    char extra = 0;
    const int matched = std::sscanf(dbDate.c_str(), "%4d-%2d-%2d%c", &y, &m, &d, &extra);
    if (matched < 3 || m < 1 || m > 12) {
        return dbDate.substr(0, std::min<size_t>(dbDate.size(), 7));
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%s %d", kMonths[m - 1], y);
    return std::string(buf);
}

// "2026-10-22 09:42:00+00" -> "2026-10-22 09:42"
inline std::string minuteStamp(const std::string &dbTimestamp) {
    if (dbTimestamp.size() >= 16) {
        return dbTimestamp.substr(0, 16);
    }
    return dbTimestamp;
}

// "2026-10-22 09:42:00+00" -> "2026-10-22"
inline std::string dateOnly(const std::string &dbTimestamp) {
    if (dbTimestamp.size() >= 10) {
        return dbTimestamp.substr(0, 10);
    }
    return dbTimestamp;
}

inline Json::Value jsonLong(long long value) {
    return Json::Value(static_cast<Json::Int64>(value));
}

inline Json::Value jsonDouble(double value) {
    return Json::Value(round2(value));
}

// CASE expression mapping constants::StatusTypes integer columns
// (affiliates.status, affiliate_commissions.status, affiliate_payouts.status,
//  coupons.status, videos.status, audios.status, ...) onto the report badge
// vocabulary.
inline std::string statusLabelCase(const std::string &column) {
    return "CASE COALESCE(" + column + ", 0) "
           "WHEN 0 THEN 'Scheduled' "
           "WHEN 1 THEN 'Published' "
           "WHEN 2 THEN 'Inactive' "
           "WHEN 3 THEN 'Revoked' "
           "WHEN 4 THEN 'Active' "
           "WHEN 5 THEN 'Inactive' "
           "WHEN 6 THEN 'Scheduled' "
           "WHEN 7 THEN 'Active' "
           "WHEN 8 THEN 'Settled' "
           "WHEN 9 THEN 'Pending' "
           "WHEN 10 THEN 'Scheduled' "
           "WHEN 11 THEN 'Active' "
           "WHEN 12 THEN 'Inactive' "
           "WHEN 13 THEN 'Inactive' "
           "WHEN 14 THEN 'Pending' "
           "WHEN 15 THEN 'Settled' "
           "WHEN 16 THEN 'Active' "
           "WHEN 17 THEN 'Failed' "
           "WHEN 18 THEN 'Pending' "
           "WHEN 19 THEN 'Settled' "
           "WHEN 20 THEN 'Pending' "
           "ELSE 'Active' END";
}

// ---- Shared SQL predicate fragments ------------------------------------
//
// All report builders bind the same three timestamp predicates:
//   $1 = filter.startDate  ("YYYY-MM-DD")
//   $2 = filter.endDate    ("YYYY-MM-DD", inclusive)
//   $3 = optional partnerId
//   $4 = optional status   (normalized: NULL when omitted)
//   $5 = optional plan
//   $6 = optional channel
//
// Monetary columns are NUMERIC and surfaced through drogon as strings, so
// every aggregation wraps them in COALESCE(...::NUMERIC, 0).

inline std::string windowPredicate(const std::string &column) {
    return column + " >= $1::date AND " + column + " < ($2::date + INTERVAL '1 day')";
}

} // namespace gnp::services::reportdetail

#endif //GNPAPI_REPORTSQLHELPERS_H
