#pragma once

#ifndef SUBSCRIPTIONSERVICE_H
#define SUBSCRIPTIONSERVICE_H

#include "dto/BaseApiResponse.h"
#include <drogon/drogon.h>
#include <drogon/utils/coroutine.h>
#include <string>

#include "UserSubscriptions.h"
#include "dto/GrantNewsPaperAccessDto.h"
#include "dto/GuestOnetimeBuyDto.h"
#include "dto/GuestSubscriptionDto.h"

#include "dto/BuyNewspaperCopyDto.h"

namespace gnp::services {

class SubscriptionService {

public:
  /**
   * @brief Retrieve a paginated list of users with optional filtering.
   * @param pageNo The page number (0-based).
   * @param pageSize The number of users per page.
   * @param query Optional search query to filter users.
   * @param callback The callback function to handle the response.
   */

  void getAllSubscriptions(int pageNo, int pageSize, const std::string &query,
      const std::function<void(const gnp::dto::BaseApiResponse &)> &callback);

  void manageGuestSubscription(const dto::GuestSubscriptionDto &dto, const std::function<void(const gnp::dto::BaseApiResponse &)> &callback);

  drogon::Task<dto::BaseApiResponse> manageGuestOneTimeBuyAsync(const dto::GuestOnetimeBuyDto &dto);

  drogon::Task<dto::BaseApiResponse> completeGuestOneTimeBuyAsync(const std::string &reference);

  drogon::Task<dto::BaseApiResponse> completeUserOneTimeBuyAsync(const std::string &userId, const std::string &reference);

  drogon::Task<dto::BaseApiResponse> fulfillSubscriptionPaymentAsync(const std::string &userId, const std::string &reference);

  drogon::Task<dto::BaseApiResponse> initializeUserOneTimeBuyAsync(const std::string &newsPaperId, const std::string &userId);

  drogon::Task<dto::BaseApiResponse> validateNewsPaperEntitlementAsync(const std::string &newsPaperId, const std::string &userId);

  drogon::Task<dto::BaseApiResponse> buyCopy(const std::string &userId, const dto::BuyNewspaperCopyDto &dto);

  drogon::Task<dto::BaseApiResponse> fulFillBuyCopy(const std::string &reference);

  void grantNewsPaperAccessToRequester(const dto::GrantNewsPaperAccessDto &dto, const std::function<void(const gnp::dto::BaseApiResponse &)> &callback);

  drogon::Task<dto::BaseApiResponse> getNewsPaperRedactedDetailsWithUniqueIdAsync(const std::string &uniqueId, const std::string &userId, const std::string &email);

  drogon::Task<dto::BaseApiResponse> readNewsPaperByDateAndPublicationAsync(const std::string &publicationId, const std::string &publicationDate, const std::string &userId, const std::string &email);

  // renewal history

  //misc
  drogon::Task<void> dispatchSubscriptionRenewalReminder();

  drogon::Task<void> unsubscribeNotifications(const std::string &userId, int notificationType);


};

} // namespace gnp::services

#endif // SUBSCRIPTIONSERVICE_H
