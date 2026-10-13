//
// Search/filter request for partner API request logs.
//

#ifndef GNPAPI_PARTNERAPILOGSEARCHREQUESTDTO_H
#define GNPAPI_PARTNERAPILOGSEARCHREQUESTDTO_H
#include <json/json.h>
#include <optional>
#include <string>

namespace gnp::dto {

    class PartnerApiLogSearchRequestDto {

    public:
        PartnerApiLogSearchRequestDto() = default;
        explicit PartnerApiLogSearchRequestDto(const Json::Value& json);

        void fromJson(const Json::Value& json);

        // Getters
        [[nodiscard]] int32_t getPageNo() const { return pageNo_; }
        [[nodiscard]] int32_t getPageSize() const { return pageSize_; }
        [[nodiscard]] const std::string& getQuery() const { return query_; }
        [[nodiscard]] const std::string& getPartnerId() const { return partnerId_; }
        [[nodiscard]] const std::string& getApiKeyId() const { return apiKeyId_; }
        [[nodiscard]] const std::string& getClientId() const { return clientId_; }
        [[nodiscard]] const std::string& getMethod() const { return method_; }
        [[nodiscard]] const std::string& getEndpoint() const { return endpoint_; }
        [[nodiscard]] int32_t getStatusCode() const { return statusCode_; }
        [[nodiscard]] const std::optional<bool>& getIsSuccessful() const { return isSuccessful_; }
        [[nodiscard]] const std::string& getDateFrom() const { return dateFrom_; }
        [[nodiscard]] const std::string& getDateTo() const { return dateTo_; }
        [[nodiscard]] const std::string& getSortOrder() const { return sortOrder_; }

        // Setters
        void setPageNo(const int32_t value) { pageNo_ = value; }
        void setPageSize(const int32_t value) { pageSize_ = value; }
        void setQuery(const std::string& value) { query_ = value; }
        void setPartnerId(const std::string& value) { partnerId_ = value; }
        void setApiKeyId(const std::string& value) { apiKeyId_ = value; }
        void setClientId(const std::string& value) { clientId_ = value; }
        void setMethod(const std::string& value) { method_ = value; }
        void setEndpoint(const std::string& value) { endpoint_ = value; }
        void setStatusCode(const int32_t value) { statusCode_ = value; }
        void setIsSuccessful(const std::optional<bool>& value) { isSuccessful_ = value; }
        void setDateFrom(const std::string& value) { dateFrom_ = value; }
        void setDateTo(const std::string& value) { dateTo_ = value; }
        void setSortOrder(const std::string& value) { sortOrder_ = value; }

    private:
        int32_t pageNo_ = 1;
        int32_t pageSize_ = 10;
        std::string query_;                 // free-text: endpoint, request id, client id, ip, user agent, request/response bodies, error
        std::string partnerId_;
        std::string apiKeyId_;
        std::string clientId_;
        std::string method_;                // HTTP method, e.g. GET, POST
        std::string endpoint_;              // partial match on the request path
        int32_t statusCode_ = 0;            // 0 = no status-code filter
        std::optional<bool> isSuccessful_;  // unset = no success filter
        std::string dateFrom_;              // YYYY-MM-DD, inclusive lower bound on created_at
        std::string dateTo_;                // YYYY-MM-DD, inclusive upper bound on created_at
        std::string sortOrder_ = "desc";    // created_at sort direction: "asc" | "desc"

    };

    inline PartnerApiLogSearchRequestDto::PartnerApiLogSearchRequestDto(const Json::Value& json) {
        fromJson(json);
    }

    inline void PartnerApiLogSearchRequestDto::fromJson(const Json::Value& json) {

        if (json.isMember("pageNo") && !json["pageNo"].isNull()) {
            setPageNo(json["pageNo"].asInt());
        }

        if (json.isMember("pageSize") && !json["pageSize"].isNull()) {
            setPageSize(json["pageSize"].asInt());
        }

        if (json.isMember("query") && !json["query"].isNull()) {
            setQuery(json["query"].asString());
        }

        if (json.isMember("partnerId") && !json["partnerId"].isNull()) {
            setPartnerId(json["partnerId"].asString());
        }

        if (json.isMember("apiKeyId") && !json["apiKeyId"].isNull()) {
            setApiKeyId(json["apiKeyId"].asString());
        }

        if (json.isMember("clientId") && !json["clientId"].isNull()) {
            setClientId(json["clientId"].asString());
        }

        if (json.isMember("method") && !json["method"].isNull()) {
            setMethod(json["method"].asString());
        }

        if (json.isMember("endpoint") && !json["endpoint"].isNull()) {
            setEndpoint(json["endpoint"].asString());
        }

        if (json.isMember("statusCode") && !json["statusCode"].isNull()) {
            setStatusCode(json["statusCode"].asInt());
        }

        if (json.isMember("isSuccessful") && !json["isSuccessful"].isNull()) {
            const Json::Value& value = json["isSuccessful"];
            if (value.isBool()) {
                setIsSuccessful(value.asBool());
            } else if (value.isString()) {
                const std::string text = value.asString();
                if (text == "true" || text == "1") {
                    setIsSuccessful(true);
                } else if (text == "false" || text == "0") {
                    setIsSuccessful(false);
                }
            }
        }

        if (json.isMember("dateFrom") && !json["dateFrom"].isNull()) {
            setDateFrom(json["dateFrom"].asString());
        }

        if (json.isMember("dateTo") && !json["dateTo"].isNull()) {
            setDateTo(json["dateTo"].asString());
        }

        if (json.isMember("sortOrder") && !json["sortOrder"].isNull()) {
            setSortOrder(json["sortOrder"].asString());
        }

    }

}
#endif //GNPAPI_PARTNERAPILOGSEARCHREQUESTDTO_H
