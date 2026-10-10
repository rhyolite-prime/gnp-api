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
        [[nodiscard]] int32_t getPageNo() const { return pageNo_; }
        [[nodiscard]] int32_t getPageSize() const { return pageSize_; }
        [[nodiscard]] const std::string& getQuery() const { return query_; }
        [[nodiscard]] const std::string& getDateFrom() const { return dateFrom_; }
        [[nodiscard]] const std::string& getUserAgent() const { return dateTo_; }
        [[nodiscard]] const std::string& getPublicationDate() const { return publicationDate_; }

        // Setters
        void setPageNo(const int32_t value) { pageNo_ = value; }
        void setPageSize(const int32_t value) { pageSize_ = value; }
        void setQuery(const std::string& value) { query_ = value; }
        void setDateFrom(const std::string& value) { dateFrom_ = value; }
        void setDateTo(const std::string& value) { dateTo_ = value; }
        void setPublicationDate(const std::string& value) { publicationDate_ = value; }

    private:
        int32_t pageNo_;
        int32_t pageSize_;
        std::string query_;
        std::string dateFrom_;
        std::string dateTo_;
        std::string publicationDate_;

    };

    inline void PublicationSearchRequestDto::fromJson(const Json::Value& json) {

        if (json.isMember("pageNo") && !json["pageNo"].isNull()) {
            setPageNo(json["pageNo"].asInt());
        }

        if (json.isMember("pageSize") && !json["pageSize"].isNull()) {
            setPageSize(json["pageSize"].asInt());
        }

        if (json.isMember("query") && !json["query"].isNull()) {
            setQuery(json["query"].asString());
        }

        if (json.isMember("dateFrom") && !json["dateFrom"].isNull()) {
            setDateFrom(json["dateFrom"].asString());
        }

        if (json.isMember("dateTo") && !json["dateTo"].isNull()) {
            setDateTo(json["dateTo"].asString());
        }

        if (json.isMember("publicationDate") && !json["publicationDate"].isNull()) {
            setPublicationDate(json["publicationDate"].asString());
        }




    }

}
#endif //GNPAPI_PUBLICATIONSEARCHREQUESTDTO_H
