//
// Created by Emmanuel Addo-Odame on 29/09/2026.
//

#ifndef GNPAPI_PUBLICATIONSEARCHREQUESTDTO_H
#define GNPAPI_PUBLICATIONSEARCHREQUESTDTO_H
#include <json/json.h>

namespace gnp::dto {

    class PublicationSearchRequestDto {

    public:
        PublicationSearchRequestDto() = default;
        explicit PublicationSearchRequestDto(const Json::Value& json);

        void fromJson(const Json::Value& json);

        // Getters
        [[nodiscard]] const std::string& getPageNo() const { return newspaperId_; }
        [[nodiscard]] const std::string& getPageSize() const { return newspaperId_; }
        [[nodiscard]] const std::string& getNewspaperId() const { return newspaperId_; }
        [[nodiscard]] const std::string& getDeviceType() const { return deviceType_; }
        [[nodiscard]] const std::string& getUserAgent() const { return userAgent_; }

        // Setters
        void setNewspaperId(const std::string& value) { newspaperId_ = value; }
        void setDeviceType(const std::string& value) { deviceType_ = value; }
        void setUserAgent(const std::string& value) { userAgent_ = value; }

    private:
        int32_t pageNo_;
        int32_t pageSize_;
        std::string query_;
        std::string dateFrom_;
        std::string dateTo_;
        std::string publicationDate_;

    };

    inline void PublicationSearchRequestDto::fromJson(const Json::Value& json) {

        if (json.isMember("newspaperId") && !json["newspaperId"].isNull()) {
            setNewspaperId(json["newspaperId"].asString());
        }

        if (json.isMember("userAgent") && !json["userAgent"].isNull()) {
            setDeviceType(json["userAgent"].asString());
        }

        if (json.isMember("deviceType") && !json["deviceType"].isNull()) {
            setDeviceType(json["deviceType"].asString());
        }

        if (json.isMember("timeSpentSeconds") && !json["timeSpentSeconds"].isNull()) {
            setTimeSpentInSeconds(json["timeSpentSeconds"].asInt());
        }


    }

}
#endif //GNPAPI_PUBLICATIONSEARCHREQUESTDTO_H
