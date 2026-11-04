#include "utils/PartnerInvoiceCalculation.h"
#include <cstdlib>
#include <iostream>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Failed: " #condition << " (line " << __LINE__ << ")\n"; \
    return EXIT_FAILURE; } } while (false)

int main() {
    using namespace gnp::utils::partner_invoice;
    CHECK(description(false, "2026-01-31") == "Invoice for new subscriptions as of 2026-01-31");
    CHECK(description(true, "2026-01-31") == "Invoice for renewed subscriptions as of 2026-01-31");
    const auto day = 86400LL * 1000000;
    CHECK(durationMultiplier(-day) == 1);
    CHECK(durationMultiplier(31 * day) == 1);
    CHECK(durationMultiplier(32 * day) == 2);
    CHECK(durationMultiplier(60 * day) == 2);
    CHECK(durationMultiplier(61 * day) == 3);
    CHECK(durationMultiplier(90 * day) == 3);
    CHECK(durationMultiplier(91 * day) == 4);
    CHECK(toPesewas(12.34) == 1234);
    auto zero = calculate(0);
    CHECK(zero.subtotal == 0 && zero.discount == 0 && zero.totalDue == 0);
    auto normal = calculate(10000);
    CHECK(normal.vat == 1500 && normal.getflNhil == 500 && normal.discount == 0);
    CHECK(normal.totalDue == 12000);
    auto fractions = calculate(10); // half-pesewa components round up
    CHECK(fractions.vat == 2 && fractions.getflNhil == 1 && fractions.discount == 0);
    CHECK(fractions.totalDue == 13);
    for (long long subtotal = 0; subtotal < 10000; ++subtotal) {
        const auto t = calculate(subtotal);
        CHECK(t.discount == 0);
        CHECK(t.totalDue == t.subtotal + t.vat + t.getflNhil);
    }
    return EXIT_SUCCESS;
}
