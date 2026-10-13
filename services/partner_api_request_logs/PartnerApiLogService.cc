//
// Created by Emmanuel Addo-Odame on 17/06/2026.
//

#include "PartnerApiLogService.h"
#include "constants/ErrorCodes.h"
#include "utils/StringUtils.h"
#include <algorithm>
#include <cctype>
#include <drogon/orm/CoroMapper.h>

using namespace drogon;
using namespace drogon::orm;

namespace {
    // Light shape check (YYYY-MM-DD); PostgreSQL still enforces real validity.
    bool looksLikeIsoDate(const std::string &value) {
        if (value.size() != 10) {
            return false;
        }
        for (size_t i = 0; i < value.size(); ++i) {
            const char c = value[i];
            if (i == 4 || i == 7) {
                if (c != '-') {
                    return false;
                }
            } else if (c < '0' || c > '9') {
                return false;
            }
        }
        return true;
    }

    std::string toUpperCopy(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return value;
    }

    std::string toLowerCopy(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }
}

namespace gnp::services {

    drogon::Task<void> PartnerApiLogService::logRequestAsync(drogon_model::Gnp::PartnerApiRequestLogs logEntry) {
        auto dbClient = app().getDbClient();
        CoroMapper<drogon_model::Gnp::PartnerApiRequestLogs> mapper(dbClient);
        
        try {
            co_await mapper.insert(logEntry);
        } catch (const DrogonDbException &e) {
            LOG_ERROR << "Failed to insert PartnerApiRequestLog: " << e.base().what();
        }
    }

    drogon::Task<dto::BaseApiResponse> PartnerApiLogService::getLogsByPartnerAsync(const std::string &partnerId, int pageNo, int pageSize, const std::string &endpoint) {
        dto::BaseApiResponse response;
        auto dbClient = app().getDbClient();
        CoroMapper<drogon_model::Gnp::PartnerApiRequestLogs> mapper(dbClient);

        try {
            Criteria c = Criteria(drogon_model::Gnp::PartnerApiRequestLogs::Cols::_partner_id, CompareOperator::EQ, partnerId);
            if (!endpoint.empty()) {
                c = c && Criteria(drogon_model::Gnp::PartnerApiRequestLogs::Cols::_endpoint, CompareOperator::EQ, endpoint);
            }

            auto logs = co_await mapper.orderBy(drogon_model::Gnp::PartnerApiRequestLogs::Cols::_created_at, SortOrder::DESC)
                                       .limit(pageSize)
                                       .offset((pageNo - 1) * pageSize)
                                       .findBy(c);
            
            auto totalCount = co_await mapper.count(c);

            Json::Value data(Json::arrayValue);
            for (const auto &log : logs) {
                data.append(log.toJson());
            }

            response.success = true;
            response.result["logs"] = data;
            response.result["totalCount"] = (Json::UInt64)totalCount;
            response.result["pageNo"] = pageNo;
            response.result["pageSize"] = pageSize;

        } catch (const DrogonDbException &e) {
            LOG_ERROR << "Database error in getLogsByPartnerAsync: " << e.base().what();
            response.success = false;
            response.error["message"] = "Database error occurred";
        }
        
        co_return response;
    }

    drogon::Task<dto::BaseApiResponse> PartnerApiLogService::searchLogsAsync(const dto::PartnerApiLogSearchRequestDto &filter) {
        using Cols = drogon_model::Gnp::PartnerApiRequestLogs::Cols;

        // ---- normalize pagination --------------------------------------------
        const int pageNo = filter.getPageNo() < 1 ? 1 : filter.getPageNo();
        const int pageSize = std::min(filter.getPageSize() < 1 ? 10 : filter.getPageSize(), 100);
        const int offset = (pageNo - 1) * pageSize;

        // ---- normalize + validate filters -------------------------------------
        const std::string query = gnp::utils::StringUtils::trim(filter.getQuery());
        const std::string partnerId = gnp::utils::StringUtils::trim(filter.getPartnerId());
        const std::string apiKeyId = gnp::utils::StringUtils::trim(filter.getApiKeyId());
        const std::string clientId = gnp::utils::StringUtils::trim(filter.getClientId());
        const std::string endpoint = gnp::utils::StringUtils::trim(filter.getEndpoint());
        const std::string method = toUpperCopy(gnp::utils::StringUtils::trim(filter.getMethod()));
        const std::string dateFrom = gnp::utils::StringUtils::trim(filter.getDateFrom());
        const std::string dateTo = gnp::utils::StringUtils::trim(filter.getDateTo());

        if ((!dateFrom.empty() && !looksLikeIsoDate(dateFrom)) ||
            (!dateTo.empty() && !looksLikeIsoDate(dateTo))) {
            dto::BaseApiResponse errorResponse;
            errorResponse.success = false;
            errorResponse.message = "Invalid date filter.";
            errorResponse.error["code"] = constants::ERR_VALIDATION;
            errorResponse.error["message"] = "dateFrom and dateTo must be in YYYY-MM-DD format.";
            co_return errorResponse;
        }

        std::string sortOrder = toLowerCopy(gnp::utils::StringUtils::trim(filter.getSortOrder()));
        if (sortOrder.empty()) {
            sortOrder = "desc";
        }
        if (sortOrder != "asc" && sortOrder != "desc") {
            dto::BaseApiResponse errorResponse;
            errorResponse.success = false;
            errorResponse.message = "Invalid sort order.";
            errorResponse.error["code"] = constants::ERR_VALIDATION;
            errorResponse.error["message"] = "sortOrder must be 'asc' or 'desc'.";
            co_return errorResponse;
        }

        const int32_t statusCode = filter.getStatusCode();
        if (statusCode != 0 && (statusCode < 100 || statusCode > 599)) {
            dto::BaseApiResponse errorResponse;
            errorResponse.success = false;
            errorResponse.message = "Invalid status code filter.";
            errorResponse.error["code"] = constants::ERR_VALIDATION;
            errorResponse.error["message"] = "statusCode must be an HTTP status code (100-599), or 0 for no filter.";
            co_return errorResponse;
        }

        // ---- build the search criteria ---------------------------------------
        // Every active filter is ANDed in; an empty criteria matches all rows
        // (drogon omits the WHERE clause entirely).
        Criteria criteria;
        if (!partnerId.empty()) {
            criteria = criteria && Criteria(Cols::_partner_id, CompareOperator::EQ, partnerId);
        }
        if (!apiKeyId.empty()) {
            criteria = criteria && Criteria(Cols::_api_key_id, CompareOperator::EQ, apiKeyId);
        }
        if (!clientId.empty()) {
            criteria = criteria && Criteria(Cols::_client_id, CompareOperator::EQ, clientId);
        }
        if (!method.empty()) {
            criteria = criteria && Criteria(Cols::_method, CompareOperator::EQ, method);
        }
        if (!endpoint.empty()) {
            criteria = criteria && Criteria(Cols::_endpoint, CompareOperator::Like, "%" + endpoint + "%");
        }
        if (statusCode != 0) {
            criteria = criteria && Criteria(Cols::_response_status_code, CompareOperator::EQ, statusCode);
        }
        if (filter.getIsSuccessful().has_value()) {
            criteria = criteria && Criteria(Cols::_is_successful, CompareOperator::EQ, *filter.getIsSuccessful());
        }
        if (!dateFrom.empty()) {
            criteria = criteria && Criteria(Cols::_created_at, CompareOperator::GE,
                                            trantor::Date::fromDbStringLocal(dateFrom + " 00:00:00"));
        }
        if (!dateTo.empty()) {
            criteria = criteria && Criteria(Cols::_created_at, CompareOperator::LE,
                                            trantor::Date::fromDbStringLocal(dateTo + " 23:59:59"));
        }
        if (!query.empty()) {
            const std::string likeQuery = "%" + query + "%";
            criteria = criteria &&
                (Criteria(Cols::_endpoint, CompareOperator::Like, likeQuery) ||
                 Criteria(Cols::_request_id, CompareOperator::Like, likeQuery) ||
                 Criteria(Cols::_client_id, CompareOperator::Like, likeQuery) ||
                 Criteria(Cols::_request_ip, CompareOperator::Like, likeQuery) ||
                 Criteria(Cols::_user_agent, CompareOperator::Like, likeQuery) ||
                 Criteria(Cols::_error_message, CompareOperator::Like, likeQuery));
        }

        auto dbClient = app().getDbClient();
        CoroMapper<drogon_model::Gnp::PartnerApiRequestLogs> mapper(dbClient);

        try {
            size_t totalCount = co_await mapper.count(criteria);
            if (totalCount == 0) {
                dto::BaseApiResponse response;
                response.success = true;
                response.result["data"] = Json::arrayValue;
                response.result["totalCount"] = 0;
                co_return response;
            }

            auto logs = co_await mapper.orderBy(Cols::_created_at,
                                                sortOrder == "asc" ? SortOrder::ASC : SortOrder::DESC)
                                       .limit(pageSize)
                                       .offset(offset)
                                       .findBy(criteria);

            dto::BaseApiResponse response;
            auto totalPages = (totalCount + pageSize - 1) / pageSize;

            response.success = true;
            response.result["totalCount"] = (Json::UInt64)totalCount;
            response.result["pageNo"] = pageNo;
            response.result["pageSize"] = pageSize;
            response.result["lowerBound"] = pageSize * (pageNo - 1) + 1;
            response.result["upperBound"] = Json::Value(
                (int)totalPages == pageNo ? (Json::UInt64)totalCount
                                          : (Json::UInt64)(pageNo * pageSize));
            response.result["totalPages"] = (int)totalPages;

            Json::Value data = Json::arrayValue;
            for (const auto &log : logs) {
                Json::Value logJson = log.toJson();

                // snake_case -> camelCase; the heavy request/response body and
                // header payloads are omitted from list results.
                Json::Value item;
                item["id"] = logJson["id"];
                item["partnerId"] = logJson["partner_id"];
                item["apiKeyId"] = logJson["api_key_id"];
                item["clientId"] = logJson["client_id"];
                item["endpoint"] = logJson["endpoint"];
                item["method"] = logJson["method"];
                item["requestIp"] = logJson["request_ip"];
                item["userAgent"] = logJson["user_agent"];
                item["requestParams"] = logJson["request_params"];
                item["responseStatusCode"] = logJson["response_status_code"];
                item["responseTimeMs"] = logJson["response_time_ms"];
                item["errorMessage"] = logJson["error_message"];
                item["isSuccessful"] = logJson["is_successful"];
                item["requestId"] = logJson["request_id"];
                item["createdAt"] = logJson["created_at"];
                item["completedAt"] = logJson["completed_at"];
                data.append(item);
            }

            response.result["data"] = data;
            co_return response;

        } catch (const DrogonDbException &e) {
            LOG_ERROR << "Database error in searchLogsAsync: " << e.base().what();
            dto::BaseApiResponse errorResponse;
            errorResponse.success = false;
            errorResponse.error["code"] = constants::ERR_DB_QUERY;
            errorResponse.error["message"] = "Database error while fetching partner API request logs.";
            errorResponse.error["detail"] = e.base().what();
            co_return errorResponse;
        }
    }

    drogon::Task<dto::BaseApiResponse> PartnerApiLogService::getFailedRequestsAsync(const std::string &partnerId, int pageNo, int pageSize) {
        dto::BaseApiResponse response;
        auto dbClient = app().getDbClient();
        CoroMapper<drogon_model::Gnp::PartnerApiRequestLogs> mapper(dbClient);

        try {
            Criteria c = Criteria(drogon_model::Gnp::PartnerApiRequestLogs::Cols::_partner_id, CompareOperator::EQ, partnerId)
                      && Criteria(drogon_model::Gnp::PartnerApiRequestLogs::Cols::_is_successful, CompareOperator::EQ, false);

            auto logs = co_await mapper.orderBy(drogon_model::Gnp::PartnerApiRequestLogs::Cols::_created_at, SortOrder::DESC)
                                       .limit(pageSize)
                                       .offset((pageNo - 1) * pageSize)
                                       .findBy(c);
            
            auto totalCount = co_await mapper.count(c);

            Json::Value data(Json::arrayValue);
            for (const auto &log : logs) {
                data.append(log.toJson());
            }

            response.success = true;
            response.result["logs"] = data;
            response.result["totalCount"] = (Json::UInt64)totalCount;
            response.result["pageNo"] = pageNo;
            response.result["pageSize"] = pageSize;

        } catch (const DrogonDbException &e) {
            LOG_ERROR << "Database error in getFailedRequestsAsync: " << e.base().what();
            response.success = false;
            response.error["message"] = "Database error occurred";
        }
        
        co_return response;
    }

    drogon::Task<dto::BaseApiResponse> PartnerApiLogService::getEndpointUsageStatsAsync(const std::string &partnerId) {
        dto::BaseApiResponse response;
        auto dbClient = app().getDbClient();

        try {
            auto result = co_await dbClient->execSqlCoro(
                "SELECT endpoint, COUNT(*) as total_requests, "
                "SUM(CASE WHEN is_successful = true THEN 1 ELSE 0 END) as successful_requests, "
                "AVG(response_time_ms) as avg_response_time_ms "
                "FROM partner_api_request_logs "
                "WHERE partner_id = $1 "
                "GROUP BY endpoint",
                partnerId
            );

            Json::Value data(Json::arrayValue);
            for (auto const &row : result) {
                Json::Value stat;
                stat["endpoint"] = row["endpoint"].as<std::string>();
                stat["totalRequests"] = (Json::UInt64)row["total_requests"].as<uint64_t>();
                stat["successfulRequests"] = (Json::UInt64)row["successful_requests"].as<uint64_t>();
                stat["avgResponseTimeMs"] = row["avg_response_time_ms"].as<double>();
                data.append(stat);
            }

            response.success = true;
            response.result["stats"] = data;

        } catch (const DrogonDbException &e) {
            LOG_ERROR << "Database error in getEndpointUsageStatsAsync: " << e.base().what();
            response.success = false;
            response.error["message"] = "Database error occurred";
        }
        
        co_return response;
    }

    drogon::Task<dto::BaseApiResponse> PartnerApiLogService::pruneOldLogsAsync(int daysOld) {
        dto::BaseApiResponse response;
        auto dbClient = app().getDbClient();

        try {
            auto result = co_await dbClient->execSqlCoro(
                "DELETE FROM partner_api_request_logs "
                "WHERE created_at < NOW() - INTERVAL '1 day' * $1",
                daysOld
            );

            response.success = true;
            response.result["deletedRows"] = (Json::UInt64)result.affectedRows();
            response.result["message"] = "Old logs pruned successfully";

        } catch (const DrogonDbException &e) {
            LOG_ERROR << "Database error in pruneOldLogsAsync: " << e.base().what();
            response.success = false;
            response.error["message"] = "Database error occurred";
        }
        
        co_return response;
    }

}