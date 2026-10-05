//
// Created by Emmanuel Addo-Odame on 03/09/2025.
//

#ifndef NEWSPAPERSERVICE_H
#define NEWSPAPERSERVICE_H

#include "dto/BaseApiResponse.h"
#include "dto/IngestNewsPaperDto.h"
#include <drogon/drogon.h>

#include "Newspapers.h"
#include "UserNotificationSubscriptions.h"
#include "Users.h"
#include "dto/NewsPaperDto.h"
#include "dto/OcrIngestionDto.h"
#include "dto/PublicationSearchRequestDto.h"
#include "dto/ReportDto.h"
#include "dto/UserEngagementDto.h"

namespace gnp::services {

class NewspaperService {

public:
  drogon::Task<dto::BaseApiResponse> getAllAsync(int pageNo, int pageSize, const std::string &publicationId, const std::string &startDate, const std::string &endDate, const std::string &query);

    drogon::Task<dto::BaseApiResponse> listAllAsync(int pageNo, int pageSize, const std::string &publicationId,
    const std::string &startDate, const std::string &endDate,
    const std::string &query, const std::string &status);

    drogon::Task<dto::BaseApiResponse> listAllArchivedAsync(int pageNo, int pageSize, const std::string &publicationId,
    const std::string &startDate, const std::string &endDate,
    const std::string &query, const std::string &status);

  //drogon::Task<gnp::dto::BaseApiResponse> getRelatedContent(int pageNo, int pageSize);

  drogon::Task<gnp::dto::BaseApiResponse> getLatestNewsPapers(int pageNo, int pageSize);

  drogon::Task<gnp::dto::BaseApiResponse> getRedactedDetailsAsync(const std::string &id);

  drogon::Task<gnp::dto::BaseApiResponse> getDetails(const std::string &id);

  drogon::Task<gnp::dto::BaseApiResponse> getFreeNewsPaperDetailsByPublicationAsync(const std::string &publicationId, const std::string &date);

  drogon::Task<gnp::dto::BaseApiResponse> getPaidNewsPaperDetailsByPublicationAsync(const std::string &publicationId, const std::string &date);

  drogon::Task<gnp::dto::BaseApiResponse> ingestAsync(const dto::IngestNewsPaperDto &dto);

  drogon::Task<gnp::dto::BaseApiResponse> update(const dto::NewsPaperDto &dto, const std::string &id);

  drogon::Task<gnp::dto::BaseApiResponse> publishAsync(const std::string &id);

  drogon::Task<gnp::dto::BaseApiResponse> unPublishAsync(const std::string &id);

  drogon::Task<gnp::dto::BaseApiResponse> deleteNewspaperAsync(const std::string &id);

  drogon::Task<gnp::dto::BaseApiResponse> regenerateNewspaperEntitlement(const std::string &startDate);

  void incrementViewCount(const std::string &id, const std::function<void(const dto::BaseApiResponse &)> &callback);

   //ocr ingestion ...
   drogon::Task<gnp::dto::BaseApiResponse> handleOcrIngestion(const dto::OcrIngestionDto &dto);

    drogon::Task<void> dispatchDailyNewsUpdate(const std::string &deliveryChannel);

    drogon::Task<gnp::dto::BaseApiResponse> trackUserEngagement(const dto::UserEngagementDto &dto, const std::string &userId);

    drogon::Task<gnp::dto::BaseApiResponse> updateUserEngagement(const dto::UserEngagementDto &dto, const std::string &userId);

    //Graphic Archive
    //drogon::Task<gnp::dto::BaseApiResponse> searchArchives(const dto::PublicationSearchRequestDto &dto, const std::string &userId);

    //reports
    drogon::Task<::gnp::dto::BaseApiResponse> getNewspaperEngagementReport(const gnp::dto::ReportDto &dto);

    drogon::Task<gnp::dto::BaseApiResponse> getRecentNewspapersForAffiliate(int pageNo, int pageSize, const std::string &affiliateId);

private:
    enum class DeliveryChannel { Email, Sms, Both };

    static DeliveryChannel parseDeliveryChannel(const std::string &channel);

    static bool isSystemGeneratedEmail(const std::string &email);

    // Build the shared pieces of the daily update.
    drogon::Task<std::string> buildNewspaperCards(
        drogon::orm::CoroMapper<drogon_model::Gnp::Newspapers> &newsMapper,
        const std::string &todayIso);

    static std::string buildDailyNewsEmailBody(
        const std::string &firstName,
        const std::string &todayFormatted,
        const std::string &newspaperCards,
        const std::string &userId);

    static std::string buildDailyNewsSmsBody();

    // Per-user senders
    drogon::Task<void> sendDailyNewsEmail(
        const drogon_model::Gnp::Users &user,
        const std::string &newspaperCards,
        const std::string &todayFormatted);

    drogon::Task<void> sendDailyNewsSms(const drogon_model::Gnp::Users &user);

    // Opt-out check
    drogon::Task<bool> hasOptedOutOfDailyNews(
        drogon::orm::CoroMapper<drogon_model::Gnp::UserNotificationSubscriptions> &notifMapper,
        const std::string &userId);
};

} // namespace gnp::services

#endif // NEWSPAPERSERVICE_H
