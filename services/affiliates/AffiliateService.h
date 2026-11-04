//
// Created by Emmanuel Addo-Odame on 11/02/2026.
//

#ifndef GNPAPI_AFFILIATESERVICE_H
#define GNPAPI_AFFILIATESERVICE_H

#include "dto/BaseApiResponse.h"
#include "dto/AffiliateDto.h"
#include <drogon/drogon.h>
#include <drogon/utils/coroutine.h>

#include "dto/AffiliateSettingsDto.h"
#include "dto/AffiliateSignupDto.h"

namespace gnp::services {

class AffiliateService {

public:

  drogon::Task<dto::BaseApiResponse> getAll(int pageNo, int pageSize, const std::string &query, int status, const std::string &sortBy);

  drogon::Task<dto::BaseApiResponse> getAdminDashboardData(const std::string &startDate, const std::string &endDate);

  drogon::Task<dto::BaseApiResponse> getAllApplicants(int pageNo, int pageSize, const std::string &query, int status);

  drogon::Task<::gnp::dto::BaseApiResponse> submitApplication(const ::gnp::dto::AffiliateSignupDto &dto);

  drogon::Task<::gnp::dto::BaseApiResponse> createAsync(const ::gnp::dto::AffiliateDto &dto);

  drogon::Task<::gnp::dto::BaseApiResponse> updateAsync(const ::gnp::dto::AffiliateDto &dto, const std::string &id);

  drogon::Task<::gnp::dto::BaseApiResponse> approveApplication(const std::string &applicationId);

  drogon::Task<::gnp::dto::BaseApiResponse> rejectApplication(const std::string &applicationId, const std::string &reason = "");

  drogon::Task<dto::BaseApiResponse> updateAffiliateAccountStatus(const std::string &id, int status, const std::string &reason = "");

  drogon::Task<dto::BaseApiResponse> deleteAffiliate(const std::string &id);

  drogon::Task<dto::BaseApiResponse> resetAffiliateUserPassword(const std::string &id);

  drogon::Task<::gnp::dto::BaseApiResponse> getAllCommissions(int pageNo, int pageSize, const std::string &affiliateId, const std::string &startDate, const std::string &endDate);

  drogon::Task<::gnp::dto::BaseApiResponse> getAllPayouts(int pageNo, int pageSize, const std::string &affiliateId, const std::string &startDate, const std::string &endDate);

  drogon::Task<::gnp::dto::BaseApiResponse> issueAffiliatePayout(const std::string &affiliateId);

  drogon::Task<::gnp::dto::BaseApiResponse> getAffiliateCommissions(const std::string &affiliateId);

  drogon::Task<::gnp::dto::BaseApiResponse> getAffiliatePayouts(const std::string &affiliateId);

  drogon::Task<::gnp::dto::BaseApiResponse> issueBulkPayout();

  drogon::Task<::gnp::dto::BaseApiResponse> getAffiliateSettings();

  drogon::Task<::gnp::dto::BaseApiResponse> createAffiliateSettings(const ::gnp::dto::AffiliateSettingsDto &dto);

  //   affiliate news paper sales
  //-> retrieve latest news papers by affiliateId
  //-> initialize buy news paper from  an affiliate : complete payment and manage commissions as well
  //->
};

} // namespace gnp::services
#endif // GNPAPI_AFFILIATESERVICE_H