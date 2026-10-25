//
// Unified JSON response builder for all 26 standard platform reports
// (see docs/REPORTS_IMPLEMENTATION_PLAN_AND_API_SPEC.md §1.2 / §2.1).
//

#ifndef GNPAPI_REPORTRESPONSEBUILDER_H
#define GNPAPI_REPORTRESPONSEBUILDER_H

#include <json/json.h>
#include <string>
#include <utility>
#include <vector>

namespace gnp::utils {

class ReportResponseBuilder {
public:
    ReportResponseBuilder& setEndpoint(const std::string& path);

    ReportResponseBuilder& addSummary(
        const std::string& key,
        const std::string& label,
        double value,
        const std::string& format,   // "currency" | "number" | "percent"
        double changePct);

    // Convenience overload for metrics without a prior-period comparison.
    ReportResponseBuilder& addSummary(
        const std::string& key,
        const std::string& label,
        double value,
        const std::string& format);

    ReportResponseBuilder& addChart(const Json::Value& chartObj);

    ReportResponseBuilder& addColumn(
        const std::string& key,
        const std::string& label,
        const std::string& format = "text",   // "text" | "number" | "currency" | "percent" | "badge"
        const std::string& align = "left");   // "left" | "right"

    ReportResponseBuilder& addRow(const Json::Value& rowObj);

    // Used on newspaper_engagement (partner quota progress bars).
    ReportResponseBuilder& setPartners(const Json::Value& partnersArray);

    // Final result payload: { endpoint, summary[], charts[], columns[], rows[], partners? }
    [[nodiscard]] Json::Value buildResult() const;

    // Full API envelope with the standard report result payload.
    [[nodiscard]] Json::Value buildEnvelope(
        const std::string& message = "Report generated successfully.") const;

    // ---------------------------------------------------------------
    // Small shared chart/row factories used by the report builders.
    // ---------------------------------------------------------------

    // { "id", "title", "type", "span"?, "labels"?, "yAxisLabel"?, "series" }
    static Json::Value makeCartesianChart(const std::string& id,
                                          const std::string& title,
                                          const std::string& type,
                                          const Json::Value& labels,
                                          const Json::Value& series,
                                          const std::string& yAxisLabel = "",
                                          const std::string& secondaryYAxisLabel = "",
                                          bool spanFull = true);

    // { "id", "title", "type": "donut", "items": [{ "name", "value" }] }
    static Json::Value makeDonutChart(const std::string& id,
                                      const std::string& title,
                                      const Json::Value& items);

    // { "id", "title", "type": "funnel", "items": [{ "name", "value" }] }
    static Json::Value makeFunnelChart(const std::string& id,
                                       const std::string& title,
                                       const Json::Value& items);

    // { "id", "title", "type": "heatmap", "xLabels", "yLabels", "matrix": [[x,y,pct], ...] }
    static Json::Value makeHeatmapChart(const std::string& id,
                                        const std::string& title,
                                        const Json::Value& xLabels,
                                        const Json::Value& yLabels,
                                        const Json::Value& matrix);

    // Single-series helper: [{ "name", "type", "data", "yAxisIndex"? }]
    static Json::Value makeSeries(const std::string& name,
                                  const std::string& type,
                                  const Json::Value& data,
                                  int yAxisIndex = -1);

    // Round to 2 decimals (front-end formats the raw numbers).
    static double round2(double value);

    // Signed % delta of `current` vs `previous` (0 when previous == 0).
    static double changePct(double current, double previous);

    // Formats a raw DB status string into the badge vocabulary the report
    // viewer expects (Active/Settled/Published/Paid, Pending/Scheduled,
    // Failed/Inactive/Expired/Revoked).
    static std::string normalizeBadgeStatus(const std::string& rawStatus);

private:
    std::string endpoint_;
    Json::Value summary_ = Json::arrayValue;
    Json::Value charts_ = Json::arrayValue;
    Json::Value columns_ = Json::arrayValue;
    Json::Value rows_ = Json::arrayValue;
    Json::Value partners_;
    bool hasPartners_ = false;
};

} // namespace gnp::utils

#endif //GNPAPI_REPORTRESPONSEBUILDER_H
