//
// Created by Emmanuel Addo-Odame on 17/06/2026.
//

#ifndef GNPAPI_PARTNERAPILOGSERVICE_H
#define GNPAPI_PARTNERAPILOGSERVICE_H
#include "dto/BaseApiResponse.h"
#include "dto/PartnerApiLogSearchRequestDto.h"
#include <drogon/drogon.h>
#include "models/PartnerApiRequestLogs.h"

namespace gnp::services {

    class PartnerApiLogService {
    public:
        // Core logging method
        drogon::Task<void> logRequestAsync(drogon_model::Gnp::PartnerApiRequestLogs logEntry);

        // Retrieve paginated logs for a specific partner
        drogon::Task<dto::BaseApiResponse> getLogsByPartnerAsync(const std::string &partnerId, int pageNo, int pageSize, const std::string &endpoint = "");

        // Search/filter request logs with free-text query and field filters (paginated)
        drogon::Task<dto::BaseApiResponse> searchLogsAsync(const dto::PartnerApiLogSearchRequestDto &filter);

        // Retrieve failed requests for a specific partner
        drogon::Task<dto::BaseApiResponse> getFailedRequestsAsync(const std::string &partnerId, int pageNo, int pageSize);

        // Get endpoint usage statistics for a partner
        drogon::Task<dto::BaseApiResponse> getEndpointUsageStatsAsync(const std::string &partnerId);

        // Maintenance: delete old logs
        drogon::Task<dto::BaseApiResponse> pruneOldLogsAsync(int daysOld);
    };

}
#endif //GNPAPI_PARTNERAPILOGSERVICE_H