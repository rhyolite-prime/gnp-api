//
// Created by Emmanuel Addo-Odame on 16/12/2025.
//

#include "CampaignService.h"
#include "services/reports/ReportInstrumentation.h"
#include "Campaigns.h" // Defines drogon_model::Gnp::Campaigns
#include "constants/ErrorCodes.h"
#include "controllers/NotificationsHub.h"
#include "dto/SendEmailDto.h"
#include "models/Users.h" // Defines drogon_model::Gnp::Users
#include "plugins/GnpServicePlugin.h"
#include "services/email/EmailService.h"
#include <ctime>
#include <drogon/orm/CoroMapper.h>
#include <drogon/orm/Mapper.h>
#include <sstream>

using namespace drogon::orm;
using drogon_model::Gnp::Campaigns;
using drogon_model::Gnp::Users;

namespace gnp::services {

drogon::Task<dto::BaseApiResponse> CampaignService::getAll(int pageNo, int pageSize, const std::string &query,
                        const std::string &channel) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Campaigns> mp(dbClient);

  // 1. Build the search criteria
  Criteria searchCriteria;
  if (!query.empty()) {
    std::string likeQuery = "%" + query + "%";

    searchCriteria =
        Criteria(Campaigns::Cols::_name, CompareOperator::Like, likeQuery) ||
        Criteria(Campaigns::Cols::_subject, CompareOperator::Like, likeQuery) ||
        Criteria(Campaigns::Cols::_message_body, CompareOperator::Like,
                 likeQuery);
  }

  if (!channel.empty()) {

    searchCriteria = searchCriteria && Criteria(Campaigns::Cols::_channel,
                                                CompareOperator::EQ, channel);
  }

  try {
    // 1. Get total count
    auto totalCount = co_await mp.count(searchCriteria);

    if (totalCount == 0) {
      dto::BaseApiResponse response;
      response.success = true;
      response.result["data"] = Json::arrayValue;
      response.result["totalCount"] = 0;
      co_return response;
    }

    // 3. Asynchronously find the paginated data
    int offset = (pageNo - 1) * pageSize;
    auto campaigns =
        co_await mp.limit(pageSize).offset(offset).findBy(searchCriteria);

    // 4. Build the final response
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

    for (const auto &campaign : campaigns) {
      Json::Value campaignJson = campaign.toJson();

      // Convert snake_case to camelCase
      Json::Value camelCaseCampaign;
      camelCaseCampaign["id"] = campaignJson["id"];
      camelCaseCampaign["name"] = campaignJson["name"];
      camelCaseCampaign["reach"] = campaignJson["reach"];
      camelCaseCampaign["status"] = campaignJson["status"];
      camelCaseCampaign["campaignType"] = campaignJson["campaign_type"];
      camelCaseCampaign["scheduledTime"] = campaignJson["scheduled_time"];
      camelCaseCampaign["clicks"] = campaignJson["clicks"];
      camelCaseCampaign["targetAudience"] = campaignJson["target_audience"];
      camelCaseCampaign["channel"] = campaignJson["channel"];
      camelCaseCampaign["subject"] = campaignJson["subject"];
      camelCaseCampaign["messageBody"] = campaignJson["message_body"];
      camelCaseCampaign["createdAt"] = campaignJson["created_at"];
      camelCaseCampaign["updatedAt"] = campaignJson["updated_at"];

      data.append(camelCaseCampaign);
    }
    response.result["data"] = data;
    co_return response;

  } catch (const DrogonDbException &e) {
    // Handle error
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] = "Database error while fetching campaigns.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<::gnp::dto::BaseApiResponse> CampaignService::createAsync(const ::gnp::dto::CreateCampaignDto &dto) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<::drogon_model::Gnp::Campaigns> mp(dbClient);

  ::drogon_model::Gnp::Campaigns newCampaign;
  newCampaign.setName(dto.getName());
  newCampaign.setTargetAudience(dto.getTargetAudience());
  newCampaign.setChannel(dto.getChannel());
  newCampaign.setDeepLink(dto.getDeepLink());
  newCampaign.setSubject(dto.getSubject());
  newCampaign.setMessageBody(dto.getMessageBody());
  newCampaign.setStatus("Draft");
  newCampaign.setCampaignType(dto.getCampaignType());
  newCampaign.setScheduledTime(dto.getScheduledTime());
  newCampaign.setReach(0);
  newCampaign.setClicks(0);
  newCampaign.setCreatedAt(trantor::Date::now());
  newCampaign.setHtmlTemplate(dto.getHtmlTemplate());

  try {
    auto campaign = co_await mp.insert(newCampaign);

    // create a job on quartz

    auto &app = drogon::app();
    auto customConfig = app.getCustomConfig();
    std::string campaignCallBackUrl = customConfig["QuartzSchedulerApi"]["CampaignCallBackUrl"].asString();

    auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
    auto &quartzApi = plugin->getQuartzApi();

    ::gnp::dto::QuartzJobDto jobDto;
    jobDto.name = campaign.getValueOfName(); // Using Name as the unique identifier name
    jobDto.description = campaign.getValueOfSubject();
    jobDto.customData.uniqueId = campaign.getValueOfId();
    // Default or empty callbackUrl as not specified by user context
    jobDto.customData.callbackUrl = campaignCallBackUrl;

    auto scheduledTime = dto.getScheduledTime();
    time_t rawTime = scheduledTime.secondsSinceEpoch();
    struct tm *timeinfo = localtime(&rawTime);

    const char *months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                            "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

    // Cron format: 0 Minute Hour Day Month ? Year
    std::stringstream cron;
    cron << "0 " << timeinfo->tm_min << " " << timeinfo->tm_hour << " "
         << timeinfo->tm_mday << " " << months[timeinfo->tm_mon] << " ? "
         << (timeinfo->tm_year + 1900);
    jobDto.schedule = cron.str();

    // StartDate format: YYYY-MM-DDTHH:MM:SS
    std::string startDateStr = scheduledTime.toDbStringLocal();
    std::replace(startDateStr.begin(), startDateStr.end(), ' ', 'T');
    jobDto.startDate = startDateStr;
    jobDto.endDate = startDateStr;

    bool isScheduleSuccessful = co_await quartzApi.scheduleJob(jobDto);

    if (isScheduleSuccessful) {
      ::gnp::dto::BaseApiResponse successResponse;
      successResponse.success = true;
      successResponse.message = "Campaign created and scheduled successfully";
      successResponse.result["id"] = campaign.getValueOfId();
      co_return successResponse;
    } else {
      ::gnp::dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "Campaign created but failed to schedule.";
      errorResponse.result["id"] = campaign.getValueOfId();
      co_return errorResponse;
    }
  } catch (const drogon::orm::DrogonDbException &e) {
    ::gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Database error while creating Campaign";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<dto::BaseApiResponse>
CampaignService::publishCampaign(const std::string &campaignId) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Campaigns> mp(dbClient);

  try {
    auto campaign = co_await mp.findByPrimaryKey(campaignId);
    campaign.setStatus("Sent");

    auto count = co_await mp.update(campaign);

    dto::BaseApiResponse response;
    if (count > 0) {
      if (campaign.getValueOfChannel() == "app-notification" ||
          campaign.getValueOfChannel() == "all") {

        Json::Value payload;
        payload["deepLink"] = campaign.getValueOfDeepLink();
        payload["notificationId"] = campaign.getValueOfId();
        payload["subject"] = campaign.getValueOfSubject();
        payload["messageBody"] = campaign.getValueOfMessageBody();
        payload["timestamp"] =
            (Json::Int64)trantor::Date::now().microSecondsSinceEpoch();

        Json::StreamWriterBuilder w;
        std::string jsonPayload = Json::writeString(w, payload);

        gnp::signalr::NotificationsHub::broadcastMessage(jsonPayload);
      } else {
        // use email service to send messages to target users
      }
      response.success = true;
      response.message = "Campaign published successfully";
    } else {
      response.success = false;
      response.message = "Failed to update campaign status";
    }
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse response;
    response.success = false;
    response.message = "Campaign not found or database error";
    response.error["detail"] = e.base().what();
    co_return response;
  }
}

drogon::Task<dto::BaseApiResponse>
CampaignService::updateCampaignStats(const std::string &campaignId,
                                     const std::string &metricsType) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Campaigns> mp(dbClient);

  try {
    // Find the campaign by ID
    auto campaign = co_await mp.findByPrimaryKey(campaignId);

    // Determine which metrics field to update
    bool shouldUpdate = false;

    if (metricsType == "reach") {
      // Assume Campaigns model has a 'deliveredCount' field
      int delivered = campaign.getValueOfReach();
      campaign.setReach(delivered + 1);
      shouldUpdate = true;
    } else if (metricsType == "click") {
      // Assume Campaigns model has a 'clickedCount' field
      int clicked = campaign.getValueOfClicks();
      campaign.setClicks(clicked + 1);
      shouldUpdate = true;
    }
    // Add additional metricsType handling as required

    if (!shouldUpdate) {
      dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "Invalid metrics type";
      co_return errorResponse;
    }

    // Update the campaign record in the database
    auto count = co_await mp.update(campaign);

    // Report telemetry: per-recipient campaign event (campaign_recipient_events)
    co_await reporting::recordCampaignRecipientEvent(
        campaignId, "", campaign.getValueOfChannel(),
        metricsType == "click" ? "clicked" : "delivered", 0.0);

    dto::BaseApiResponse response;
    if (count > 0) {
      response.success = true;
      response.message = "Campaign stats updated successfully";
    } else {
      response.success = false;
      response.message = "Failed to update campaign stats";
    }
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse response;
    response.success = false;
    response.message = "Campaign not found or database error";
    response.error["detail"] = e.base().what();
    co_return response;
  }
}

drogon::Task<dto::BaseApiResponse>
CampaignService::deleteCampaign(const std::string &campaignId) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Campaigns> mp(dbClient);

  try {
    auto count = co_await mp.deleteByPrimaryKey(campaignId);

    dto::BaseApiResponse response;
    if (count > 0) {
      response.success = true;
      response.message = "Campaign deleted successfully";
    } else {
      response.success = false;
      response.message = "Campaign not found or could not be deleted";
    }
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse response;
    response.success = false;
    response.message = "Database error deleting campaign";
    response.error["detail"] = e.base().what();
    co_return response;
  }
}

drogon::Task<::gnp::dto::BaseApiResponse>
CampaignService::runScheduledCampaign(const std::string &campaignId) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Campaigns> mp(dbClient);

  try {
    auto campaign = co_await mp.findByPrimaryKey(campaignId);

    // Fetch target users

    std::vector<Users> users;

    if (campaign.getValueOfTargetAudience() == "all") {
      CoroMapper<Users> userMp(dbClient);
      users = co_await userMp.findBy(
          Criteria(Users::Cols::_is_active, CompareOperator::EQ, true));
    } else if (campaign.getValueOfTargetAudience() == "subscribers") {
      CoroMapper<Users> userMp(dbClient);
      users = co_await userMp.findBy(
          Criteria(Users::Cols::_is_active, CompareOperator::EQ, true) &&
          Criteria(Users::Cols::_is_admin_user, CompareOperator::EQ, false));
    } else if (campaign.getValueOfTargetAudience() == "inactive-subscribers" ||
               campaign.getValueOfTargetAudience() == "churned") {
      std::string sql = "SELECT u.* FROM users u "
                        "INNER JOIN user_subscriptions us ON u.id = us.user_id "
                        "WHERE us.is_active = false AND u.is_active = true";
      auto result = co_await dbClient->execSqlCoro(sql);
      for (const auto &row : result) {
        users.emplace_back(Users(row, -1));
      }
    } else if (campaign.getValueOfTargetAudience() == "new-users") {
      // Users created in the last 30 days
      std::string sql = "SELECT * FROM users WHERE created_at >= NOW() - "
                        "INTERVAL '30 days' AND is_active = true";
      auto result = co_await dbClient->execSqlCoro(sql);
      for (const auto &row : result) {
        users.emplace_back(Users(row, -1));
      }
    } else {

      CoroMapper<Users> userMp(dbClient);
      users = co_await userMp.findBy(
          Criteria(Users::Cols::_is_active, CompareOperator::EQ, true));
    }

    auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
    auto &emailService = plugin->getEmailService();

    int sentCount = 0;
    for (const auto &user : users) {

      gnp::dto::SendEmailDto emailDto;
      emailDto.setTo(user.getValueOfEmail());
      emailDto.setSubject(campaign.getValueOfSubject());

      if (campaign.getValueOfCampaignType() == "email" &&
          !campaign.getValueOfHtmlTemplate().empty()) {

        std::map<std::string, std::string> tokens;
        tokens["email"] = user.getValueOfEmail();
        tokens["phone"] = user.getValueOfPhoneNumber();
        tokens["username"] = user.getValueOfUsername();

        if (tokens["username"].empty()) {
          tokens["username"] =
              user.getValueOfFirstName() + " " + user.getValueOfLastName();
        }

        emailDto.setBody(
            replaceTokens(campaign.getValueOfHtmlTemplate(), tokens));
      } else {
        emailDto.setBody(campaign.getValueOfMessageBody());
      }

      // check if user has email
      if (user.getValueOfEmail().empty()) {
        continue;
      }

      LOG_DEBUG << "Sending email to: " << user.getValueOfEmail()
                << " for campaign: " << campaignId;

      co_await emailService.sendEmailAsync(emailDto);

      sentCount++;
    }

    campaign.setStatus("Sent");
    co_await mp.update(campaign);

    ::gnp::dto::BaseApiResponse response;
    response.success = true;
    response.message = "Campaign execution started. Emails dispatched: " +
                       std::to_string(sentCount);
    co_return response;

  } catch (const std::exception &e) {
    LOG_ERROR << "Error running campaign " << campaignId << ": " << e.what();
    ::gnp::dto::BaseApiResponse response;
    response.success = false;
    response.message = "Failed to run campaign: " + std::string(e.what());
    co_return response;
  }
}

std::string CampaignService::replaceTokens(
    const std::string &templateStr,
    const std::map<std::string, std::string> &tokens) {

  std::string result = templateStr;

  for (const auto &pair : tokens) {
    std::string placeholder = "{{" + pair.first + "}}";
    std::string value = pair.second;

    size_t pos = 0;
    while ((pos = result.find(placeholder, pos)) != std::string::npos) {
      result.replace(pos, placeholder.length(), value);
      pos += value.length();
    }
  }

  return result;
}

} // namespace gnp::services
