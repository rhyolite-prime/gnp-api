#pragma once

#include <json/json.h>

namespace gnp::utils::partner_invoice {

// The slices are pre-tax invoice amounts, not subscription counts. They use
// the same partner, period and categories as the invoice report line items.
inline Json::Value subscriptionInvoiceDonut(double newAmount, double renewedAmount) {
    Json::Value chart;
    // Keep the existing chart ID so clients can update without a contract break.
    chart["id"] = "invoice_collection_status";
    chart["title"] = "New vs Renewed Subscription Invoices (GHS, pre-tax)";
    chart["type"] = "donut";
    chart["items"] = Json::arrayValue;

    Json::Value newItem;
    newItem["name"] = "New subscriptions";
    newItem["value"] = newAmount;
    chart["items"].append(newItem);

    Json::Value renewedItem;
    renewedItem["name"] = "Renewed subscriptions";
    renewedItem["value"] = renewedAmount;
    chart["items"].append(renewedItem);
    return chart;
}

} // namespace gnp::utils::partner_invoice
