#pragma once

#include <cmath>
#include <cstdint>
#include <string>

namespace gnp::utils::partner_invoice {

inline constexpr const char *newPrefix = "Invoice for new subscriptions as of ";
inline constexpr const char *renewedPrefix = "Invoice for renewed subscriptions as of ";
inline constexpr const char *legacyPrefix = "Invoice for new and renewed subscriptions as of ";

inline std::string description(bool renewed, const std::string &date) {
    return std::string(renewed ? renewedPrefix : newPrefix) + date;
}

// These are the existing invoice report policy rates, not a determination of
// the tax rules applicable to every partner. Calculate in pesewas so that the
// displayed components always add up to the displayed total.
struct Totals {
    std::int64_t subtotal;
    std::int64_t vat;
    std::int64_t getflNhil;
    std::int64_t discount;
    std::int64_t totalDue;
};

inline std::int64_t toPesewas(double amount) {
    return static_cast<std::int64_t>(std::llround(amount * 100.0));
}

inline double toCurrency(std::int64_t amount) {
    return static_cast<double>(amount) / 100.0;
}

inline Totals calculate(std::int64_t subtotal) {
    // Integer arithmetic, rounded to the nearest pesewa (half away from zero).
    const auto percentage = [subtotal](std::int64_t rate) {
        const auto whole = (subtotal / 100) * rate;
        const auto remainder = (subtotal % 100) * rate;
        return whole + (remainder >= 0 ? (remainder + 50) / 100
                                       : (remainder - 50) / 100);
    };
    const auto vat = percentage(15);
    const auto levy = percentage(5);
    const std::int64_t discount = 0;
    return {subtotal, vat, levy, discount, subtotal + vat + levy - discount};
}

inline long long durationMultiplier(long long microseconds) {
    const long long days = microseconds > 0 ? microseconds / (1000000LL * 86400) : 0;
    if (days <= 31) return 1;
    if (days <= 60) return 2;
    if (days <= 90) return 3;
    return (days + 29) / 30;
}
} // namespace gnp::utils::partner_invoice
