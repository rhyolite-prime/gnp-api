// Affiliate program settings submitted to the admin API.
#ifndef GNPAPI_AFFILIATESETTINGSDTO_H
#define GNPAPI_AFFILIATESETTINGSDTO_H

#include <json/json.h>

namespace gnp::dto {

class AffiliateSettingsDto {
public:
    // Preserve field presence so updates can change only the supplied columns.
    void fromJson(const Json::Value &json) { values_ = json; }
    [[nodiscard]] const Json::Value &values() const { return values_; }

private:
    Json::Value values_;
};

} // namespace gnp::dto
#endif // GNPAPI_AFFILIATESETTINGSDTO_H
