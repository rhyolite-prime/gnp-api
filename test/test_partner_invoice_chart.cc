#include "utils/PartnerInvoiceChart.h"

#include <cstdlib>
#include <iostream>
#include <utility>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Failed: " #condition << " (line " << __LINE__ << ")\n"; \
    return EXIT_FAILURE; } } while (false)

int main() {
    using gnp::utils::partner_invoice::subscriptionInvoiceDonut;
    for (const auto &amounts : {std::pair{10.25, 30.50}, std::pair{0.0, 12.0},
                                std::pair{8.0, 0.0}, std::pair{0.0, 0.0}}) {
        const auto chart = subscriptionInvoiceDonut(amounts.first, amounts.second);
        CHECK(chart["id"].asString() == "invoice_collection_status");
        CHECK(chart["type"].asString() == "donut");
        CHECK(chart["items"].isArray() && chart["items"].size() == 2);
        CHECK(chart["items"][0]["name"].asString() == "New subscriptions");
        CHECK(chart["items"][0]["value"].asDouble() == amounts.first);
        CHECK(chart["items"][1]["name"].asString() == "Renewed subscriptions");
        CHECK(chart["items"][1]["value"].asDouble() == amounts.second);
    }
    return EXIT_SUCCESS;
}
