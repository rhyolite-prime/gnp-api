//
// Unified JSON response builder for all 26 standard platform reports
// (see docs/REPORTS_IMPLEMENTATION_PLAN_AND_API_SPEC.md §1.2 / §2.1).
//

#include "ReportResponseBuilder.h"

#include <cmath>
#include <utility>

namespace gnp::utils {

ReportResponseBuilder& ReportResponseBuilder::setEndpoint(const std::string& path) {
    endpoint_ = path;
    return *this;
}

ReportResponseBuilder& ReportResponseBuilder::addSummary(
    const std::string& key,
    const std::string& label,
    double value,
    const std::string& format,
    double changePct) {
    Json::Value metric(Json::objectValue);
    metric["key"] = key;
    metric["label"] = label;
    metric["value"] = round2(value);
    metric["format"] = format;
    metric["changePct"] = round2(changePct);
    summary_.append(metric);
    return *this;
}

ReportResponseBuilder& ReportResponseBuilder::addSummary(
    const std::string& key,
    const std::string& label,
    double value,
    const std::string& format) {
    Json::Value metric(Json::objectValue);
    metric["key"] = key;
    metric["label"] = label;
    metric["value"] = round2(value);
    metric["format"] = format;
    metric["changePct"] = 0.0;
    summary_.append(metric);
    return *this;
}

ReportResponseBuilder& ReportResponseBuilder::addChart(const Json::Value& chartObj) {
    charts_.append(chartObj);
    return *this;
}

ReportResponseBuilder& ReportResponseBuilder::addColumn(
    const std::string& key,
    const std::string& label,
    const std::string& format,
    const std::string& align) {
    Json::Value column(Json::objectValue);
    column["key"] = key;
    column["label"] = label;
    column["format"] = format;
    column["align"] = align;
    columns_.append(column);
    return *this;
}

ReportResponseBuilder& ReportResponseBuilder::addRow(const Json::Value& rowObj) {
    rows_.append(rowObj);
    return *this;
}

ReportResponseBuilder& ReportResponseBuilder::setPartners(const Json::Value& partnersArray) {
    partners_ = partnersArray;
    hasPartners_ = true;
    return *this;
}

Json::Value ReportResponseBuilder::buildResult() const {
    Json::Value result(Json::objectValue);
    result["endpoint"] = endpoint_;
    result["summary"] = summary_;
    result["charts"] = charts_;
    result["columns"] = columns_;
    result["rows"] = rows_;
    if (hasPartners_) {
        result["partners"] = partners_;
    }
    return result;
}

Json::Value ReportResponseBuilder::buildEnvelope(const std::string& message) const {
    Json::Value envelope(Json::objectValue);
    envelope["result"] = buildResult();
    envelope["targetUrl"] = "";
    envelope["message"] = message;
    envelope["success"] = true;
    envelope["error"] = Json::nullValue;
    return envelope;
}

Json::Value ReportResponseBuilder::makeCartesianChart(const std::string& id,
                                                      const std::string& title,
                                                      const std::string& type,
                                                      const Json::Value& labels,
                                                      const Json::Value& series,
                                                      const std::string& yAxisLabel,
                                                      const std::string& secondaryYAxisLabel,
                                                      bool spanFull) {
    Json::Value chart(Json::objectValue);
    chart["id"] = id;
    chart["title"] = title;
    chart["type"] = type;
    if (spanFull) {
        chart["span"] = "full";
    }
    chart["labels"] = labels;
    chart["series"] = series;
    if (!yAxisLabel.empty()) {
        chart["yAxisLabel"] = yAxisLabel;
    }
    if (!secondaryYAxisLabel.empty()) {
        chart["secondaryYAxisLabel"] = secondaryYAxisLabel;
    }
    return chart;
}

Json::Value ReportResponseBuilder::makeDonutChart(const std::string& id,
                                                  const std::string& title,
                                                  const Json::Value& items) {
    Json::Value chart(Json::objectValue);
    chart["id"] = id;
    chart["title"] = title;
    chart["type"] = "donut";
    chart["items"] = items;
    return chart;
}

Json::Value ReportResponseBuilder::makeFunnelChart(const std::string& id,
                                                   const std::string& title,
                                                   const Json::Value& items) {
    Json::Value chart(Json::objectValue);
    chart["id"] = id;
    chart["title"] = title;
    chart["type"] = "funnel";
    chart["items"] = items;
    return chart;
}

Json::Value ReportResponseBuilder::makeHeatmapChart(const std::string& id,
                                                    const std::string& title,
                                                    const Json::Value& xLabels,
                                                    const Json::Value& yLabels,
                                                    const Json::Value& matrix) {
    Json::Value chart(Json::objectValue);
    chart["id"] = id;
    chart["title"] = title;
    chart["type"] = "heatmap";
    chart["span"] = "full";
    chart["xLabels"] = xLabels;
    chart["yLabels"] = yLabels;
    chart["matrix"] = matrix;
    return chart;
}

Json::Value ReportResponseBuilder::makeSeries(const std::string& name,
                                              const std::string& type,
                                              const Json::Value& data,
                                              int yAxisIndex) {
    Json::Value series(Json::objectValue);
    series["name"] = name;
    series["type"] = type;
    series["data"] = data;
    if (yAxisIndex >= 0) {
        series["yAxisIndex"] = yAxisIndex;
    }
    return series;
}

double ReportResponseBuilder::round2(double value) {
    if (!std::isfinite(value)) {
        return 0.0;
    }
    return std::round(value * 100.0) / 100.0;
}

double ReportResponseBuilder::changePct(double current, double previous) {
    if (previous == 0.0) {
        return current == 0.0 ? 0.0 : 100.0;
    }
    return round2(((current - previous) / std::fabs(previous)) * 100.0);
}

std::string ReportResponseBuilder::normalizeBadgeStatus(const std::string& rawStatus) {
    std::string lowered;
    lowered.reserve(rawStatus.size());
    for (const char c : rawStatus) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (uc >= 'A' && uc <= 'Z') {
            lowered.push_back(static_cast<char>(uc + 32));
        } else {
            lowered.push_back(c);
        }
    }

    if (lowered.find("fail") != std::string::npos ||
        lowered.find("declin") != std::string::npos ||
        lowered.find("reject") != std::string::npos) {
        return "Failed";
    }
    if (lowered.find("expir") != std::string::npos) {
        return "Expired";
    }
    if (lowered.find("revoke") != std::string::npos ||
        lowered.find("revoked") != std::string::npos ||
        lowered.find("cancel") != std::string::npos) {
        return "Revoked";
    }
    if (lowered.find("inactive") != std::string::npos ||
        lowered.find("disabled") != std::string::npos ||
        lowered.find("suspend") != std::string::npos ||
        lowered.find("locked") != std::string::npos ||
        lowered.find("closed") != std::string::npos ||
        lowered.find("churn") != std::string::npos) {
        return "Inactive";
    }
    if (lowered.find("pending") != std::string::npos ||
        lowered.find("initiat") != std::string::npos ||
        lowered.find("processing") != std::string::npos ||
        lowered.find("open") != std::string::npos ||
        lowered.find("await") != std::string::npos) {
        return "Pending";
    }
    if (lowered.find("schedul") != std::string::npos ||
        lowered.find("queue") != std::string::npos ||
        lowered.find("draft") != std::string::npos ||
        lowered.find("upcoming") != std::string::npos) {
        return "Scheduled";
    }
    if (lowered.find("success") != std::string::npos ||
        lowered.find("settled") != std::string::npos ||
        lowered.find("paid") != std::string::npos ||
        lowered.find("complete") != std::string::npos ||
        lowered.find("resolve") != std::string::npos) {
        return "Settled";
    }
    if (lowered.find("publish") != std::string::npos ||
        lowered.find("sent") != std::string::npos ||
        lowered.find("active") != std::string::npos ||
        lowered.find("enabled") != std::string::npos) {
        return "Active";
    }
    return rawStatus.empty() ? std::string("Active") : rawStatus;
}

} // namespace gnp::utils
