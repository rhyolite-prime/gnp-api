//
// Created by Emmanuel Addo-Odame on 03/09/2025.
//

#include "UserService.h"
#include "Payments.h"
#include "UserSubscriptions.h"

#include <algorithm>
#include <cctype>
#include <random>

#include "Users.h"
#include "bcrypt.h"
#include "constants/ErrorCodes.h"
#include "dto/SendEmailDto.h"
#include "dto/SigninDto.h"
#include "services/email/EmailService.h"
#include <jwt-cpp/jwt.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

#include "Affiliates.h"
#include "CommercialPartners.h"
#include "SubscriptionRenewalHistory.h"
#include "UserGroups.h"
#include "plugins/GnpServicePlugin.h"
#include "utils/PasswordUtils.h"
#include "utils/StringUtils.h"

using namespace drogon::orm;
using drogon_model::Gnp::UserGroups;
using drogon_model::Gnp::Users;
using drogon_model::Gnp::UserSubscriptions;

namespace gnp::services {

drogon::Task<dto::BaseApiResponse>
UserService::getAll(int pageNo, int pageSize, const std::string &query) {
  auto dbClient = drogon::app().getDbClient();
  auto mp = CoroMapper<Users>(dbClient);

  // 1. Build the search criteria
  Criteria searchCriteria;
  if (!query.empty()) {
    std::string likeQuery = "%" + query + "%";
    searchCriteria =
        Criteria(Users::Cols::_first_name, CompareOperator::Like, likeQuery) ||
        Criteria(Users::Cols::_email, CompareOperator::Like, likeQuery) ||
        Criteria(Users::Cols::_last_name, CompareOperator::Like, likeQuery);
  }

  dto::BaseApiResponse response;
  try {
    // 2. Get the total count matching the criteria
    size_t totalCount = co_await mp.count(searchCriteria);

    if (totalCount == 0) {
      response.success = true;
      response.result["data"] = Json::arrayValue;
      response.result["totalCount"] = 0;
      response.result["pageNo"] = pageNo;
      response.result["pageSize"] = pageSize;
      response.result["totalPages"] = 0;
      response.result["lowerBound"] = 0;
      response.result["upperBound"] = 0;

      co_return response;
    }

    // 3. Find the paginated data
    int offset = (pageNo - 1) * pageSize;
    auto users =
        co_await mp.limit(pageSize).offset(offset).findBy(searchCriteria);

    auto totalPages = (totalCount + pageSize - 1) / pageSize;

    // 4. Build the final response
    response.success = true;
    response.result["totalCount"] = (Json::UInt64)totalCount;
    response.result["pageNo"] = pageNo;
    response.result["pageSize"] = pageSize;
    response.result["totalPages"] =
        (int)((totalCount + pageSize - 1) / pageSize);
    response.result["lowerBound"] = pageSize * (pageNo - 1) + 1;
    response.result["upperBound"] = (int)totalPages == pageNo
                                        ? (Json::UInt64)totalCount
                                        : (Json::UInt64)(pageNo * pageSize);

    Json::Value data = Json::arrayValue;

    for (const auto &role : users) {
      Json::Value roleJson = role.toJson();
      Json::Value camelCaseRole;
      camelCaseRole["id"] = roleJson["id"];
      camelCaseRole["firstName"] = roleJson["first_name"];
      camelCaseRole["lastName"] = roleJson["last_name"];
      camelCaseRole["email"] = roleJson["email"];
      camelCaseRole["phoneNumber"] = roleJson["phone_number"];
      camelCaseRole["country"] = roleJson["country"];
      camelCaseRole["profileImageUrl"] = roleJson["profile_image_url"];
      camelCaseRole["isLockedOut"] = roleJson["is_locked_out"];
      camelCaseRole["isActive"] = roleJson["is_active"];
      camelCaseRole["createdAt"] = roleJson["created_at"];
      camelCaseRole["updatedAt"] = roleJson["updated_at"];
      data.append(camelCaseRole);
    }
    response.result["data"] = data;

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.error["message"] = "Database error while fetching users.";
    response.error["detail"] = e.base().what();
  }
  co_return response;
}

drogon::Task<dto::BaseApiResponse> UserService::getAllSubscribers(
    int pageNo, int pageSize, const std::string &query,
    const std::string &dateJoinedStartDate,
    const std::string &dateJoinedEndDate,
    const std::string &lastActiveStartDate,
    const std::string &lastActiveEndDate, const std::string &accountType) {

  auto dbClient = drogon::app().getDbClient();
  auto mp = CoroMapper<Users>(dbClient);

  // accountType: "partner", "affiliate", "regular", or "" / "all" for all types

  // 1. Build base criteria: only non-admin users
  Criteria criteria(Users::Cols::_is_admin_user, CompareOperator::EQ, false);

  // 2. Search query (name / email)
  if (!query.empty()) {
    std::string likeQuery = "%" + query + "%";
    Criteria searchCriteria =
        Criteria(Users::Cols::_first_name, CompareOperator::Like, likeQuery) ||
        Criteria(Users::Cols::_email, CompareOperator::Like, likeQuery) ||
        Criteria(Users::Cols::_last_name, CompareOperator::Like, likeQuery);
    criteria = criteria && searchCriteria;
  }

  // 3. Account type filter
  if (!accountType.empty() && accountType != "all") {
    if (accountType == "partner") {
      criteria = criteria &&
                 Criteria(Users::Cols::_partner_id, CompareOperator::IsNotNull);
    } else if (accountType == "affiliate") {
      criteria = criteria && Criteria(Users::Cols::_affiliate_id,
                                      CompareOperator::IsNotNull);
    } else if (accountType == "regular") {
      // Regular = no partner_id AND no affiliate_id
      criteria = criteria &&
                 Criteria(Users::Cols::_partner_id, CompareOperator::IsNull) &&
                 Criteria(Users::Cols::_affiliate_id, CompareOperator::IsNull);
    }
  }

  // 4. Date Joined range filter (created_at)
  if (!dateJoinedStartDate.empty()) {
    criteria =
        criteria && Criteria(Users::Cols::_created_at, CompareOperator::GE,
                             dateJoinedStartDate + " 00:00:00");
  }
  if (!dateJoinedEndDate.empty()) {
    criteria =
        criteria && Criteria(Users::Cols::_created_at, CompareOperator::LE,
                             dateJoinedEndDate + " 23:59:59");
  }

  // 5. Last Active range filter
  if (!lastActiveStartDate.empty()) {
    criteria =
        criteria && Criteria(Users::Cols::_last_active, CompareOperator::GE,
                             lastActiveStartDate + " 00:00:00");
  }
  if (!lastActiveEndDate.empty()) {
    criteria =
        criteria && Criteria(Users::Cols::_last_active, CompareOperator::LE,
                             lastActiveEndDate + " 23:59:59");
  }

  dto::BaseApiResponse response;
  try {
    // 6. Total count
    size_t totalCount = co_await mp.count(criteria);

    if (totalCount == 0) {
      response.success = true;
      response.result["data"] = Json::arrayValue;
      response.result["totalCount"] = 0;
      response.result["pageNo"] = pageNo;
      response.result["pageSize"] = pageSize;
      response.result["totalPages"] = 0;
      response.result["lowerBound"] = 0;
      response.result["upperBound"] = 0;
      co_return response;
    }

    // 7. Paginated fetch (newest first is usually nice for subscribers)
    int offset = (pageNo - 1) * pageSize;
    auto users = co_await mp.orderBy(Users::Cols::_created_at, SortOrder::DESC)
                     .limit(pageSize)
                     .offset(offset)
                     .findBy(criteria);

    auto totalPages = (totalCount + pageSize - 1) / pageSize;

    // 8. Build response
    response.success = true;
    response.result["totalCount"] = (Json::UInt64)totalCount;
    response.result["pageNo"] = pageNo;
    response.result["pageSize"] = pageSize;
    response.result["totalPages"] = (int)totalPages;
    response.result["lowerBound"] = pageSize * (pageNo - 1) + 1;
    response.result["upperBound"] = (int)totalPages == pageNo
                                        ? (Json::UInt64)totalCount
                                        : (Json::UInt64)(pageNo * pageSize);

    Json::Value data = Json::arrayValue;
    for (const auto &user : users) {
      Json::Value roleJson = user.toJson();
      Json::Value camelCaseRole;
      camelCaseRole["id"] = roleJson["id"];
      camelCaseRole["firstName"] = roleJson["first_name"];
      camelCaseRole["lastName"] = roleJson["last_name"];
      camelCaseRole["email"] = roleJson["email"];
      camelCaseRole["phoneNumber"] = roleJson["phone_number"];
      camelCaseRole["country"] = roleJson["country"];
      camelCaseRole["profileImageUrl"] = roleJson["profile_image_url"];
      camelCaseRole["isLockedOut"] = roleJson["is_locked_out"];
      camelCaseRole["isActive"] = roleJson["is_active"];
      camelCaseRole["createdAt"] = roleJson["created_at"];
      camelCaseRole["lastActive"] = roleJson["last_active"];
      camelCaseRole["affiliateId"] = roleJson["affiliate_id"];
      camelCaseRole["partnerId"] = roleJson["partner_id"];
      data.append(camelCaseRole);
    }
    response.result["data"] = data;

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.message = "Database error while fetching subscribers.";
    response.error["code"] = constants::ERR_DB_QUERY;
    response.error["detail"] = e.base().what();
  }
  co_return response;
}

  drogon::Task<::gnp::dto::BaseApiResponse> UserService::getSubscriberSubscriptionSummary(std::string userId) {
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<UserSubscriptions> subMapper(dbClient);
  CoroMapper<drogon_model::Gnp::SubscriptionRenewalHistory> renewalMapper(dbClient);
  CoroMapper<drogon_model::Gnp::CommercialPartners> partnerMapper(dbClient);

  try {
    // 1. Fetch the user's active UserSubscription
    auto subscriptions = co_await subMapper.findBy(
        Criteria(UserSubscriptions::Cols::_user_id, CompareOperator::EQ,
                 userId) &&
        Criteria(UserSubscriptions::Cols::_is_active, CompareOperator::EQ,
                 true));

    if (subscriptions.empty()) {
      dto::BaseApiResponse response;
      response.success = true;
      response.result["subscriptionSummary"] = Json::nullValue;
      response.result["renewalHistory"] = Json::arrayValue;
      co_return response;
    }

    auto subscription = subscriptions.front();

    // 2. Resolve the partner from the subscription itself (no partnerId
    //    parameter needed). UserSubscriptions carries the partner linkage.
    std::string partnerName = "Unknown Partner";
    bool hasPartner =
        subscription.getPartnerId() != nullptr; // column may be nullable

    if (hasPartner) {
      try {
        auto partner = co_await partnerMapper.findByPrimaryKey(
            subscription.getValueOfPartnerId());
        partnerName = partner.getValueOfName();
      } catch (const DrogonDbException &) {
        // Partner row missing — fall back to a safe label instead of failing
        // the whole request.
        partnerName = "Unknown Partner";
      }
    }

    // 3. Fetch renewal history for this user only (drop the partner filter).
    //    Renewals are already scoped to the user by user_id.
    auto renewals =
        co_await renewalMapper
            .orderBy(drogon_model::Gnp::SubscriptionRenewalHistory::Cols::
                         _created_at,
                     SortOrder::DESC)
            .findBy(
                Criteria(drogon_model::Gnp::SubscriptionRenewalHistory::Cols::
                             _user_id,
                         CompareOperator::EQ, userId));

    // 4. Build JSON Response
    Json::Value result;

    Json::Value summary;
    summary["package"] =  subscription.getValueOfSubscriptionPlanDescription() + " (Assigned)";
    summary["dueDate"] = subscription.getValueOfEndDate().toCustomFormattedString("%b %d, %Y");
    summary["billingCycle"] = subscription.getValueOfBillingCycle();
    summary["subscriptionId"] = subscription.getValueOfId();

    const auto &startDateObj = subscription.getValueOfStartDate();
    const auto &endDateObj = subscription.getValueOfEndDate();
    summary["activatedOn"] = startDateObj.toCustomFormattedString("%Y-%m-%d");
    summary["validUntil"] = endDateObj.toCustomFormattedString("%Y-%m-%d");

    auto now = trantor::Date::now();
    int64_t diffDays =
        (endDateObj.microSecondsSinceEpoch() - now.microSecondsSinceEpoch()) /
        (1000000LL * 3600 * 24);
    summary["daysRemaining"] = (Json::Int64)std::max((int64_t)0, diffDays);

    result["subscriptionSummary"] = summary;

    Json::Value historyArray = Json::arrayValue;
    for (const auto &renewal : renewals) {
      Json::Value historyItem;
      historyItem["date"] =
          renewal.getValueOfCreatedAt().toCustomFormattedString("%b %d, %Y");
      historyItem["package"] = renewal.getValueOfSubscriptionPlanName();

      // Amount format: GHS 0.00 (Covered by {Partner Name})
      std::string amountStr = renewal.getValueOfAmountPaid();
      historyItem["amount"] =
          "GHS " + amountStr + " (Covered by " + partnerName + ")";

      historyItem["status"] = renewal.getValueOfTransactionStatus();

      // Reference from created_at yyyymmddhhmmss
      historyItem["reference"] =
          renewal.getValueOfCreatedAt().toCustomFormattedString("%Y%m%d%H%M%S");

      historyArray.append(historyItem);
    }

    result["renewalHistory"] = historyArray;

    ::gnp::dto::BaseApiResponse response;
    response.success = true;
    response.result = result;
    co_return response;

  } catch (const DrogonDbException &e) {
    ::gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to fetch subscriber subscription details";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<dto::BaseApiResponse> UserService::getDetails(const std::string &userId) {
  auto dbClient = drogon::app().getDbClient();
  auto mp = drogon::orm::CoroMapper<Users>(dbClient);

  dto::BaseApiResponse response;
  try {
    Users user = co_await mp.findOne(
        Criteria(Users::Cols::_id, CompareOperator::EQ, userId));

    Json::Value userJson = user.toJson();
    Json::Value data;
    data["id"] = userJson["id"];
    data["firstName"] = userJson["first_name"];
    data["lastName"] = userJson["last_name"];
    data["email"] = userJson["email"];
    data["username"] = userJson["username"];
    data["phoneNumber"] = userJson["phone_number"];
    data["country"] = userJson["country"];
    data["profileImageUrl"] = userJson["profile_image_url"];
    data["isLockedOut"] = userJson["is_locked_out"];
    data["isActive"] = userJson["is_active"];
    data["isAdminUser"] = userJson["is_admin_user"];
    data["isPartnerAdminUser"] = userJson["is_partner_admin_user"];
    data["partnerId"] = userJson["partner_id"];
    data["roles"] = userJson["roles"];
    data["lastActive"] = userJson["last_active"];
    data["createdAt"] = userJson["created_at"];
    data["lastActive"] = userJson["last_active"];
    data["affiliateId"] = userJson["affiliate_id"];
    data["partnerId"] = userJson["partner_id"];

    response.success = true;
    response.result["data"] = data;

  } catch (const DrogonDbException &e) {
    response.success = false;
    if (e.base().what() == std::string("Unexpected row number")) {
      response.message = "User not found";
      response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    } else {
      response.message = "Database error while fetching user details.";
      response.error["code"] = constants::ERR_DB_QUERY;
      response.error["detail"] = e.base().what();
    }
  }
  co_return response;
}




  drogon::Task<dto::BaseApiResponse> UserService::getAdminUsers(int pageNo, int pageSize, const std::string &query,
                           const std::string &roleId,
                           const std::string &userGroupId) {
  auto dbClient = drogon::app().getDbClient();
  auto mp = CoroMapper<Users>(dbClient);

  // 1. Build the search criteria
  Criteria criteria(Users::Cols::_is_admin_user, CompareOperator::EQ, true);
  if (!query.empty()) {
    std::string likeQuery = "%" + query + "%";
    Criteria searchCriteria =
        Criteria(Users::Cols::_first_name, CompareOperator::Like, likeQuery) ||
        Criteria(Users::Cols::_email, CompareOperator::Like, likeQuery) ||
        Criteria(Users::Cols::_last_name, CompareOperator::Like, likeQuery);
    criteria = criteria && searchCriteria;
  }

  if (!roleId.empty()) {
    criteria = criteria && Criteria(drogon::orm::CustomSql("\"roles\"::text LIKE $?"), "%\"" + roleId + "\"%");
  }

  if (!userGroupId.empty()) {
    criteria = criteria && Criteria(drogon::orm::CustomSql("\"user_groups\"::text LIKE $?"), "%\"" + userGroupId + "\"%");
  }

  dto::BaseApiResponse response;
  try {
    size_t totalCount = co_await mp.count(criteria);
    if (totalCount == 0) {
      response.success = true;
      response.result["data"] = Json::arrayValue;
      response.result["totalCount"] = 0;
      response.result["pageNo"] = pageNo;
      response.result["pageSize"] = pageSize;
      response.result["totalPages"] = 0;
      response.result["lowerBound"] = 0;
      response.result["upperBound"] = 0;
      co_return response;
    }

    int offset = (pageNo - 1) * pageSize;
    auto users = co_await mp.orderBy(Users::Cols::_created_at, drogon::orm::SortOrder::DESC).limit(pageSize).offset(offset).findBy(criteria);

    auto totalPages = (totalCount + pageSize - 1) / pageSize;

    response.success = true;
    response.result["totalCount"] = (Json::UInt64)totalCount;
    response.result["pageNo"] = pageNo;
    response.result["pageSize"] = pageSize;
    response.result["totalPages"] = (int)totalPages;
    response.result["lowerBound"] = pageSize * (pageNo - 1) + 1;
    response.result["upperBound"] = (int)totalPages == pageNo
                                        ? (Json::UInt64)totalCount
                                        : (Json::UInt64)(pageNo * pageSize);

    Json::Value data = Json::arrayValue;
    Json::Reader reader;

    for (const auto &user : users) {
      Json::Value userJson = user.toJson();
      Json::Value camelCaseUser;
      camelCaseUser["id"] = userJson["id"];
      camelCaseUser["firstName"] = userJson["first_name"];
      camelCaseUser["lastName"] = userJson["last_name"];
      camelCaseUser["email"] = userJson["email"];
      camelCaseUser["phoneNumber"] = userJson["phone_number"];
      camelCaseUser["country"] = userJson["country"];
      camelCaseUser["username"] = userJson["username"];
      camelCaseUser["profileImageUrl"] = userJson["profile_image_url"];
      camelCaseUser["isActive"] = userJson["is_active"];
      camelCaseUser["createdAt"] = userJson["created_at"];
      camelCaseUser["updatedAt"] = userJson["updated_at"];

      Json::Value userGroupsArray;
      Json::Value rolesArray;

      Json::Reader reader;
      if (userJson.isMember("user_groups") &&
          userJson["user_groups"].isString()) {
        reader.parse(userJson["user_groups"].asString(), userGroupsArray);
      }

      camelCaseUser["userGroups"] =
          userGroupsArray.isArray() ? userGroupsArray : Json::arrayValue;

      if (userJson.isMember("roles") && userJson["roles"].isString()) {
        reader.parse(userJson["roles"].asString(), rolesArray);
      }

      camelCaseUser["roles"] =
          rolesArray.isArray() ? rolesArray : Json::arrayValue;

      data.append(camelCaseUser);
    }
    response.result["data"] = data;
  } catch (const DrogonDbException &e) {
    response.success = false;
    response.error["code"] = constants::ERR_DB_QUERY;
    response.error["message"] = "Database error while fetching users.";
    response.error["detail"] = e.base().what();
  }
  co_return response;
}




drogon::Task<dto::BaseApiResponse> UserService::getPartnerAdminUsers(const std::string &partnerId, int pageNo,
                                  int pageSize, const std::string &query) {
  auto dbClient = drogon::app().getDbClient();
  auto mp = CoroMapper<Users>(dbClient);

  // 1. Build the search criteria
  Criteria criteria =
      Criteria(Users::Cols::_partner_id, CompareOperator::EQ, partnerId) &&
      Criteria(Users::Cols::_is_partner_admin_user, CompareOperator::EQ, true);

  if (!query.empty()) {
    std::string likeQuery = "%" + query + "%";
    Criteria searchCriteria =
        Criteria(Users::Cols::_first_name, CompareOperator::Like, likeQuery) ||
        Criteria(Users::Cols::_email, CompareOperator::Like, likeQuery) ||
        Criteria(Users::Cols::_last_name, CompareOperator::Like, likeQuery);
    criteria = criteria && searchCriteria;
  }

  dto::BaseApiResponse response;
  try {
    // 2. Get the total count matching the criteria
    size_t totalCount = co_await mp.count(criteria);
    if (totalCount == 0) {
      response.success = true;
      response.result["data"] = Json::arrayValue;
      response.result["totalCount"] = 0;
      co_return response;
    }

    // 3. Find the paginated data
    int offset = (pageNo - 1) * pageSize;
    auto users = co_await mp.limit(pageSize).offset(offset).findBy(criteria);

    // 4. Build the final response
    response.success = true;
    response.result["totalCount"] = (Json::UInt64)totalCount;
    response.result["pageNo"] = pageNo;
    response.result["pageSize"] = pageSize;
    response.result["totalPages"] =
        (int)((totalCount + pageSize - 1) / pageSize);

    Json::Value data = Json::arrayValue;
    for (const auto &user : users) {
      Json::Value userJson = user.toJson();
      Json::Value camelCaseUser;
      camelCaseUser["id"] = userJson["id"];
      camelCaseUser["firstName"] = userJson["first_name"];
      camelCaseUser["lastName"] = userJson["last_name"];
      camelCaseUser["email"] = userJson["email"];
      camelCaseUser["phoneNumber"] = userJson["phone_number"];
      camelCaseUser["country"] = userJson["country"];
      camelCaseUser["username"] = userJson["username"];
      camelCaseUser["profileImageUrl"] = userJson["profile_image_url"];
      camelCaseUser["isLockedOut"] = userJson["is_locked_out"];
      camelCaseUser["isActive"] = userJson["is_active"];
      camelCaseUser["createdAt"] = userJson["created_at"];
      camelCaseUser["updatedAt"] = userJson["updated_at"];
      camelCaseUser["roles"] = userJson["roles"];
      data.append(camelCaseUser);
    }
    response.result["data"] = data;
  } catch (const DrogonDbException &e) {
    response.success = false;
    response.error["code"] = constants::ERR_DB_QUERY;
    response.error["message"] = "Database error while fetching users.";
    response.error["detail"] = e.base().what();
  }
  co_return response;
}

drogon::Task<dto::BaseApiResponse> UserService::getPartnerSubscribers(const std::string &partnerId, int pageNo,
                                   int pageSize, const std::string &query) {
  auto dbClient = drogon::app().getDbClient();
  auto mp = CoroMapper<Users>(dbClient);

  // 1. Build the search criteria
  Criteria criteria(Users::Cols::_partner_id, CompareOperator::EQ, partnerId);
  if (!query.empty()) {
    std::string likeQuery = "%" + query + "%";
    Criteria searchCriteria =
        Criteria(Users::Cols::_first_name, CompareOperator::Like, likeQuery) ||
        Criteria(Users::Cols::_email, CompareOperator::Like, likeQuery) ||
        Criteria(Users::Cols::_last_name, CompareOperator::Like, likeQuery);
    criteria = criteria && searchCriteria;
  }

  dto::BaseApiResponse response;
  try {
    // 2. Get the total count matching the criteria
    size_t totalCount = co_await mp.count(criteria);
    if (totalCount == 0) {
      response.success = true;
      response.result["data"] = Json::arrayValue;
      response.result["totalCount"] = 0;
      co_return response;
    }

    // 3. Find the paginated data
    int offset = (pageNo - 1) * pageSize;
    auto users = co_await mp.limit(pageSize).offset(offset).findBy(criteria);

    auto totalPages = (totalCount + pageSize - 1) / pageSize;

    // 4. Build the final response
    response.success = true;
    response.result["totalCount"] = (Json::UInt64)totalCount;
    response.result["pageNo"] = pageNo;
    response.result["pageSize"] = pageSize;
    response.result["totalPages"] =
        (int)((totalCount + pageSize - 1) / pageSize);
    response.result["lowerBound"] = pageSize * (pageNo - 1) + 1;
    response.result["upperBound"] = (int)totalPages == pageNo
                                        ? (Json::UInt64)totalCount
                                        : (Json::UInt64)(pageNo * pageSize);

    if (users.empty()) {
      response.result["data"] = Json::arrayValue;
      co_return response;
    }

    // Extract user IDs to fetch subscription descriptions
    std::string userIdsCondition = "(";
    for (size_t i = 0; i < users.size(); ++i) {
      userIdsCondition += "'" + users[i].getValueOfId() + "'";
      if (i < users.size() - 1) {
        userIdsCondition += ",";
      }
    }
    userIdsCondition += ")";

    std::string sql = "SELECT user_id, subscription_plan_description "
                      "FROM user_subscriptions "
                      "WHERE is_active = true AND user_id IN " +
                      userIdsCondition;

    try {
      auto res = co_await dbClient->execSqlCoro(sql);
      std::map<std::string, std::string> subMap;
      for (const auto &row : res) {
        subMap[row["user_id"].as<std::string>()] =
            row["subscription_plan_description"].isNull()
                ? "No Description"
                : row["subscription_plan_description"].as<std::string>();
      }

      Json::Value data = Json::arrayValue;
      for (const auto &role : users) {
        Json::Value roleJson = role.toJson();
        Json::Value camelCaseRole;
        camelCaseRole["id"] = roleJson["id"];
        camelCaseRole["firstName"] = roleJson["first_name"];
        camelCaseRole["lastName"] = roleJson["last_name"];
        camelCaseRole["email"] = roleJson["email"];
        camelCaseRole["phoneNumber"] = roleJson["phone_number"];
        camelCaseRole["country"] = roleJson["country"];
        camelCaseRole["profileImageUrl"] = roleJson["profile_image_url"];
        camelCaseRole["partnerId"] = roleJson["partner_id"];
        camelCaseRole["isActive"] = roleJson["is_active"];
        camelCaseRole["createdAt"] = roleJson["created_at"];
        camelCaseRole["updatedAt"] = roleJson["updated_at"];

        std::string userId = roleJson["id"].asString();
        if (subMap.find(userId) != subMap.end()) {
          camelCaseRole["subscriptionPlanDescription"] = subMap[userId];
        } else {
          camelCaseRole["subscriptionPlanDescription"] =
              "No Active Subscription";
        }
        data.append(camelCaseRole);
      }
      response.result["data"] = data;
    } catch (const DrogonDbException &e) {
      response.success = false;
      response.error["message"] =
          "Database error while fetching subscription descriptions.";
      response.error["detail"] = e.base().what();
    }
  } catch (const DrogonDbException &e) {
    response.success = false;
    response.error["code"] = constants::ERR_DB_QUERY;
    response.error["message"] = "Database error while fetching users.";
    response.error["detail"] = e.base().what();
  }
  co_return response;
}

drogon::Task<dto::BaseApiResponse> UserService::create(const dto::UserDto &userDto) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mp(dbClient);

  Users newUser;
  newUser.setFirstName(userDto.getFirstName());
  newUser.setLastName(userDto.getLastName());
  std::string standardEmail = userDto.getEmail();
  std::transform(standardEmail.begin(), standardEmail.end(),
                 standardEmail.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  newUser.setEmail(standardEmail);
  newUser.setUsername(userDto.getUsername());
  newUser.setPhoneNumber(userDto.getPhoneNumber());
  newUser.setCountry(userDto.getCountry());
  newUser.setPasswordHash(bcrypt::generateHash(userDto.getPassword()));
  newUser.setIsActive(true);
  newUser.setIsLockedOut(false);
  newUser.setCreatedAt(trantor::Date::now());

  gnp::dto::BaseApiResponse response;
  try {
    Users publication = co_await mp.insert(newUser);
    response.success = true;
    response.message = "User created successfully";
    response.result["id"] = publication.getValueOfId();
  } catch (const drogon::orm::DrogonDbException &e) {
    response.success = false;
    response.message = "Database error while creating user";
    response.error["code"] = constants::ERR_DB_QUERY;
  }
  co_return response;
}

drogon::Task<dto::BaseApiResponse> UserService::createAdminUser(const dto::AdminUserDto &userDto) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mp(dbClient);

  Users newUser;
  newUser.setFirstName(userDto.getFirstName());
  newUser.setLastName(userDto.getLastName());
  std::string standardEmail = userDto.getEmail();
  std::transform(standardEmail.begin(), standardEmail.end(),
                 standardEmail.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  newUser.setEmail(standardEmail);
  newUser.setUsername(userDto.getUsername());
  newUser.setPhoneNumber(userDto.getPhoneNumber());
  newUser.setCountry("GH");
  newUser.setPasswordHash(bcrypt::generateHash(userDto.getPassword()));
  newUser.setIsActive(true);
  newUser.setIsLockedOut(false);
  newUser.setIsAdminUser(true);

  // Convert roles vector to JSON string if provided
  Json::Value userGroupsArray = Json::arrayValue;
  Json::Value rolesArray = Json::arrayValue;
  for (const auto &perm : userDto.getUserGroups()) {
    userGroupsArray.append(perm);
  }

  Json::FastWriter writer;
  std::string userGroupsStr = writer.write(userGroupsArray);
  if (!userGroupsStr.empty() && userGroupsStr.back() == '\n') {
    userGroupsStr.pop_back();
  }

  newUser.setUserGroups(userGroupsStr);

  for (const auto &perm : userDto.getRoles()) {
    rolesArray.append(perm);
  }

  std::string rolesStr = writer.write(rolesArray);
  if (!rolesStr.empty() && rolesStr.back() == '\n') {
    rolesStr.pop_back();
  }

  newUser.setRoles(rolesStr);

  newUser.setCreatedAt(trantor::Date::now());

  dto::BaseApiResponse response;
  try {
    Users insertedUser = co_await mp.insert(newUser);
    response.success = true;
    response.message = "Admin user created successfully";
    response.result["id"] = insertedUser.getValueOfId();
  } catch (const drogon::orm::DrogonDbException &e) {
    response.success = false;
    response.message = "Database error while creating admin user";
    response.error["code"] = constants::ERR_DB_QUERY;
    response.error["detail"] = e.base().what();
  }
  co_return response;
}

drogon::Task<dto::BaseApiResponse> UserService::update(const dto::UserDto &userDto, const std::string &userId) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mp(dbClient);

  dto::BaseApiResponse response;

  try {

    Criteria criteria = Criteria(Users::Cols::_id, CompareOperator::EQ, userId);

    Users user = co_await mp.findOne(criteria);

    if (!userDto.getFirstName().empty()) {
      user.setFirstName(userDto.getFirstName());
    }

    if (!userDto.getLastName().empty()) {
      user.setLastName(userDto.getLastName());
    }

    if (!userDto.getEmail().empty()) {
      std::string standardEmail = userDto.getEmail();
      std::transform(standardEmail.begin(), standardEmail.end(),
                     standardEmail.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      user.setEmail(standardEmail);
    }

    if (!userDto.getPhoneNumber().empty()) {
      user.setPhoneNumber(userDto.getPhoneNumber());
    }

    if (!userDto.getCountry().empty()) {
      user.setCountry(userDto.getCountry());
    }

    if (!userDto.getUsername().empty()) {
      user.setUsername(userDto.getUsername());
    }

    if (!userDto.getPassword().empty()) {
      user.setPasswordHash(bcrypt::generateHash(userDto.getPassword()));
    }

    user.setUpdatedAt(trantor::Date::now());

    co_await mp.update(user);

    response.success = true;
    response.message = "User updated successfully";
  } catch (const DrogonDbException &e) {
    response.success = false;
    response.message = "User not found or database error";
    response.error["code"] = constants::ERR_DB_QUERY;
    response.error["detail"] = e.base().what();
  }

  co_return response;
}

drogon::Task<dto::BaseApiResponse> UserService::updateAdminUser(const dto::AdminUserDto &userDto,
                             const std::string &userId) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mp(dbClient);

  dto::BaseApiResponse response;

  try {

    Criteria criteria =
        Criteria(Users::Cols::_id, CompareOperator::EQ, userId) &&
        Criteria(Users::Cols::_is_admin_user, CompareOperator::EQ, true);

    Users user = co_await mp.findOne(criteria);

    if (!userDto.getFirstName().empty()) {
      user.setFirstName(userDto.getFirstName());
    }

    if (!userDto.getLastName().empty()) {
      user.setLastName(userDto.getLastName());
    }

    if (!userDto.getEmail().empty()) {
      std::string standardEmail = userDto.getEmail();
      std::transform(standardEmail.begin(), standardEmail.end(),
                     standardEmail.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      user.setEmail(standardEmail);
    }

    if (!userDto.getPhoneNumber().empty()) {
      user.setPhoneNumber(userDto.getPhoneNumber());
    }

    if (!userDto.getCountry().empty()) {
      user.setCountry(userDto.getCountry());
    }

    if (!userDto.getUsername().empty()) {
      user.setUsername(userDto.getUsername());
    }

    if (!userDto.getPassword().empty()) {
      user.setPasswordHash(bcrypt::generateHash(userDto.getPassword()));
    }

    // Update roles if provided
    Json::Value userGroupsArray = Json::arrayValue;
    Json::Value rolesArray = Json::arrayValue;
    for (const auto &perm : userDto.getUserGroups()) {
      userGroupsArray.append(perm);
    }

    Json::FastWriter writer;
    std::string userGroupsStr = writer.write(userGroupsArray);
    if (!userGroupsStr.empty() && userGroupsStr.back() == '\n') {
      userGroupsStr.pop_back();
    }

    user.setUserGroups(userGroupsStr);

    for (const auto &perm : userDto.getRoles()) {
      rolesArray.append(perm);
    }

    std::string rolesStr = writer.write(rolesArray);
    if (!rolesStr.empty() && rolesStr.back() == '\n') {
      rolesStr.pop_back();
    }

    user.setRoles(rolesStr);

    user.setUpdatedAt(trantor::Date::now());

    co_await mp.update(user);

    response.success = true;
    response.message = "Admin user updated successfully";
  } catch (const DrogonDbException &e) {
    response.success = false;
    response.message = "Admin user not found or database error";
    response.error["code"] = constants::ERR_DB_QUERY;
    response.error["detail"] = e.base().what();
  }

  co_return response;
}

drogon::Task<dto::BaseApiResponse> UserService::updateProfileImage(const std::string &userId,
                                const std::string &logoContent) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mp(dbClient);

  try {
    auto user = co_await mp.findOne(
        Criteria(Users::Cols::_id, CompareOperator::EQ, userId));

    if (!logoContent.empty()) {
      user.setProfileImage(logoContent);
      user.setUpdatedAt(trantor::Date::now());
      co_await mp.update(user);
    }

    dto::BaseApiResponse response;
    response.success = true;
    response.message = "User profile image updated successfully";
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    if (e.base().what() == std::string("Unexpected row number")) {
      errorResponse.message = "User not found";
      errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    } else {
      errorResponse.message = "Failed to update user profile image";
      errorResponse.error["code"] = constants::ERR_DB_QUERY;
      errorResponse.error["detail"] = e.base().what();
    }
    co_return errorResponse;
  }
}

drogon::Task<dto::BaseApiResponse> UserService::invitePartnerAdminUser(const dto::AdminUserDto &userDto,
                                    const std::string &partnerId) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mp(dbClient);
  CoroMapper<drogon_model::Gnp::CommercialPartners> partnerMapper(dbClient);
  CoroMapper<drogon_model::Gnp::CommercialPartners> userInvitationsMapper(
      dbClient);
  auto plugin = drogon::app().getPlugin<plugins::GnpServicePlugin>();

  Users newUser;
  newUser.setFirstName(userDto.getFirstName());
  newUser.setLastName(userDto.getLastName());
  std::string standardEmail = userDto.getEmail();
  std::transform(standardEmail.begin(), standardEmail.end(),
                 standardEmail.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  newUser.setEmail(standardEmail);
  newUser.setUsername(userDto.getUsername());
  newUser.setPhoneNumber(userDto.getPhoneNumber());
  newUser.setCountry(userDto.getCountry());
  newUser.setPasswordHash(bcrypt::generateHash(userDto.getPassword()));
  newUser.setIsActive(true);
  newUser.setIsPartnerAdminUser(true);
  newUser.setPartnerId(partnerId);

  auto partnerInfo = co_await partnerMapper.findByPrimaryKey(partnerId);

  // Convert permissions vector to JSON string
  Json::Value permissionsJson = Json::arrayValue;
  for (const auto &perm : userDto.getRoles()) {
    permissionsJson.append(perm);
  }
  Json::StreamWriterBuilder writer;
  newUser.setRoles(Json::writeString(writer, permissionsJson));

  newUser.setIsLockedOut(false);
  newUser.setCreatedAt(trantor::Date::now());

  dto::BaseApiResponse response;

  try {
    Users user = co_await mp.insert(newUser);
    response.success = true;
    response.message = "User created successfully";
    response.result["id"] = user.getValueOfId();

    // create a record in the invitations table
    auto &adminUserInvitationService = plugin->getAdminUserInvitationService();
    dto::AdminUserInvitationDto adminUserInvitationDto;
    adminUserInvitationDto.setUserId(user.getValueOfId());
    adminUserInvitationDto.setPartnerId(partnerId);
    adminUserInvitationDto.setEmail(user.getValueOfEmail());
    std::string tokenHash = bcrypt::generateHash(
        user.getValueOfId() + trantor::Date::now().toDbStringLocal());
    adminUserInvitationDto.setTokenHash(tokenHash);

    co_await adminUserInvitationService.create(adminUserInvitationDto);

    // send invitation email to new partner admin user

    auto &emailService = plugin->getEmailService();

    dto::SendEmailDto emailDto;
    emailDto.setTo(userDto.getEmail());
    std::string emailSubject =
        "Invitation to join" + partnerInfo.getValueOfName() + " on GNP";
    emailDto.setSubject(emailSubject);

    auto invitationLink =
        "https://dev.graphicnewsplus.com/admin/accept-invitation?id=" +
        user.getValueOfId();

    std::string emailBody =
        R"(
        <!DOCTYPE html>
        <html>
        <head>
        <style>
          body { font-family: Arial, sans-serif; background-color: #f4f4f4; margin: 0; padding: 0; }
          .container { max-width: 600px; margin: 20px auto; background-color: #ffffff; border-radius: 8px; overflow: hidden; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
          .header { background-color: #80004d; color: #ffffff; padding: 20px; text-align: center; }
          .content { padding: 30px; color: #333333; }
          .cta-container { text-align: center; margin: 25px 0; }
          .cta-button { display: inline-block; padding: 12px 25px; background-color: #80004d; color: #ffffff !important; text-decoration: none; border-radius: 5px; font-weight: bold; }
          .link-box { background-color: #f9f9f9; padding: 12px; border-radius: 5px; margin-top: 15px; font-size: 13px; word-break: break-all; color: #80004d; }
          .footer { background-color: #f4f4f4; color: #666666; padding: 10px; text-align: center; font-size: 12px; }
        </style>
        </head>
        <body>
        <div class="container">
          <div class="header">
            <h1>GNP Administrative Invitation</h1>
          </div>

          <div class="content">
            <p>Hello )" +
        userDto.getFirstName() + R"(,</p>

            <p>You have been invited to join )" +
        partnerInfo.getValueOfName() +
        R"( on <strong>Graphic News Plus</strong> as an Administrative user.</p>

            <p>This role gives you access to manage users, configurations, and platform operations.</p>

            <p>Click the button below to accept your invitation and complete your account setup:</p>

            <div class="cta-container">
              <a href=")" +
        invitationLink + R"(" class="cta-button">Accept Invitation</a>
            </div>

            <p>If the button above does not work, copy and paste the link below into your browser:</p>

            <div class="link-box">)" +
        invitationLink + R"(</div>

            <p>If you did not expect this invitation, you can safely ignore this email.</p>
          </div>

          <div class="footer">
            &copy; )" +
        trantor::Date::now().toCustomFormattedString("%Y") +
        R"( GNP. All rights reserved.
          </div>
        </div>
        </body>
        </html>
    )";

    emailDto.setBody(emailBody);

    co_await emailService.sendEmailAsync(emailDto);

  } catch (const drogon::orm::DrogonDbException &e) {
    response.success = false;
    response.message = "Database error while creating user";
    response.error["code"] = constants::ERR_DB_QUERY;
  }
  co_return response;
}

drogon::Task<dto::BaseApiResponse> UserService::updatePartnerAdminUser(const dto::AdminUserDto &userDto,
                                    const std::string &adminUserId,
                                    const std::string &partnerId) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mp(dbClient);

  dto::BaseApiResponse response;

  try {

    Criteria criteria =
        Criteria(Users::Cols::_id, CompareOperator::EQ, adminUserId) &&
        Criteria(Users::Cols::_partner_id, CompareOperator::EQ, partnerId);

    Users user = co_await mp.findOne(criteria);

    user.setFirstName(userDto.getFirstName());
    user.setLastName(userDto.getLastName());
    user.setEmail(userDto.getEmail());
    user.setPhoneNumber(userDto.getPhoneNumber());
    user.setCountry(userDto.getCountry());
    user.setUsername(userDto.getUsername());

    // Convert roles vector to JSON string
    Json::Value rolesJson = Json::arrayValue;
    for (const auto &role : userDto.getRoles()) {
      rolesJson.append(role);
    }
    Json::StreamWriterBuilder writer;
    user.setRoles(Json::writeString(writer, rolesJson));

    user.setUpdatedAt(trantor::Date::now());

    co_await mp.update(user);

    response.success = true;
    response.message = "Admin user updated successfully";
  } catch (const DrogonDbException &e) {
    response.success = false;
    response.message = "User not found or database error";
    response.error["code"] = constants::ERR_DB_QUERY;
    response.error["detail"] = e.base().what();
  }

  co_return response;
}

drogon::Task<dto::BaseApiResponse> UserService::registerUserPasskeys(
    const dto::RegisterUserPasskeysDto &passKeysDto) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mp(dbClient);

  gnp::dto::BaseApiResponse response;
  try {
    Users foundUser = co_await mp.findOne(Criteria(
        Users::Cols::_id, CompareOperator::EQ, passKeysDto.getUserId()));

    Users user = foundUser;

    // Helper to ensure Base64 is standard (url-safe replacement)
    auto toStandardBase64 = [](std::string s) {
      for (char &c : s) {
        if (c == '-')
          c = '+';
        else if (c == '_')
          c = '/';
      }
      while (s.length() % 4 != 0)
        s += '=';
      return s;
    };

    std::string credIdStr = toStandardBase64(passKeysDto.getCredentialId());
    std::string pubKeyStr = toStandardBase64(passKeysDto.getPublicKey());

    user.setCredentialId(drogon::utils::base64DecodeToVector(credIdStr));
    user.setPublicKey(drogon::utils::base64DecodeToVector(pubKeyStr));
    user.setPublicKeyAlgorithm(passKeysDto.getPublicKeyAlgorithm());

    // Passkey fields
    // Extract signCount from attestationObject (CBOR map) if available
    int64_t initialSignCount = 0;

    try {
      std::string attObjStr =
          toStandardBase64(passKeysDto.getAttestationObject());
      std::vector<char> attObjBytes =
          drogon::utils::base64DecodeToVector(attObjStr);

      // Simple CBOR 'authData' finder
      const std::string authDataKey = "authData";
      const char authDataKeyHeader = 0x60 | (char)authDataKey.length(); // 0x68

      auto it = std::search(attObjBytes.begin(), attObjBytes.end(),
                            authDataKey.begin(), authDataKey.end());

      if (it != attObjBytes.end() && it != attObjBytes.begin()) {
        if (*(it - 1) == authDataKeyHeader) {
          auto valueStart = it + authDataKey.length();
          if (valueStart < attObjBytes.end()) {
            size_t dataLen = 0;
            auto dataIt = valueStart;
            uint8_t head = (uint8_t)*dataIt;
            dataIt++;

            if (head >= 0x40 && head <= 0x57) {
              dataLen = head - 0x40;
            } else if (head == 0x58) {
              if (dataIt < attObjBytes.end()) {
                dataLen = (uint8_t)*dataIt;
                dataIt++;
              }
            } else if (head == 0x59) {
              if (dataIt + 1 < attObjBytes.end()) {
                dataLen = ((uint8_t)*dataIt << 8) | (uint8_t)*(dataIt + 1);
                dataIt += 2;
              }
            }

            if (dataLen >= 37 &&
                (size_t)std::distance(dataIt, attObjBytes.end()) >= dataLen) {
              auto scIt = dataIt + 32 + 1; // start of signCount
              uint32_t sc = ((uint8_t)*scIt << 24) |
                            ((uint8_t)*(scIt + 1) << 16) |
                            ((uint8_t)*(scIt + 2) << 8) | (uint8_t)*(scIt + 3);
              initialSignCount = sc;
            }
          }
        }
      }
    } catch (...) {
      // Log or handle parsing error if necessary, but don't fail registration
    }

    user.setSignCount(initialSignCount);
    user.setCredentialType(passKeysDto.getCredentialType());
    user.setTransports(passKeysDto.getTransports());
    user.setUpdatedAt(trantor::Date::now());

    std::string uId = user.getValueOfId();
    std::vector<char> handleVec(uId.begin(), uId.end());
    user.setUserHandle(handleVec);

    co_await mp.update(user);

    response.success = true;
    response.message = "Passkeys registered successfully";

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.message = "Database error or user not found";
    response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
  }
  co_return response;
}

drogon::Task<dto::BaseApiResponse>
UserService::validateUserPasskeys(const dto::LoginUserPasskeyDto &passkeyDto) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mp(dbClient);

  auto toStandardBase64 = [](std::string s) {
    for (char &c : s) {
      if (c == '-')
        c = '+';
      else if (c == '_')
        c = '/';
    }
    while (s.length() % 4 != 0)
      s += '=';
    return s;
  };

  std::string credIdStr = toStandardBase64(passkeyDto.getCredentialId());
  std::vector<char> credIdBytes =
      drogon::utils::base64DecodeToVector(credIdStr);

  std::string userId = passkeyDto.getUserHandle();

  Criteria userCriteria;
  bool hasUserHandle = !userId.empty();

  dto::BaseApiResponse response;
  if (hasUserHandle) {
    if (userId.length() > 36) { // rudimentary check
      std::string handleStr = toStandardBase64(userId);
      auto handleBytes = drogon::utils::base64DecodeToVector(handleStr);
      userId = std::string(handleBytes.begin(), handleBytes.end());
    }
    userCriteria = Criteria(Users::Cols::_id, CompareOperator::EQ, userId);
  } else {
    response.success = false;
    response.message = "User Handle is required for passkey login.";
    co_return response;
  }

  try {
    Users user = co_await mp.findOne(userCriteria);
    // Found user. Now verify passkey.

    // 1. Verify Credential ID matches (if we found by userHandle)
    auto storedCredId = user.getValueOfCredentialId();
    if (credIdBytes != storedCredId) {
      // Passkey doesn't belong to this user or changed
      response.success = false;
      response.message = "Invalid credential ID.";
      co_return response;
    }

    // 2. Cryptographic Verification
    // SignedData = authenticatorData + sha256(clientDataJSON)

    std::string authDataStr =
        toStandardBase64(passkeyDto.getAuthenticatorData());
    std::vector<char> authData =
        drogon::utils::base64DecodeToVector(authDataStr);

    std::string clientDataStr =
        toStandardBase64(passkeyDto.getClientDataJSON());
    std::vector<char> clientData =
        drogon::utils::base64DecodeToVector(clientDataStr);

    unsigned char clientDataHash[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char *)clientData.data(), clientData.size(),
           clientDataHash);

    std::vector<unsigned char> signedData;
    signedData.reserve(authData.size() + SHA256_DIGEST_LENGTH);
    signedData.insert(signedData.end(), authData.begin(), authData.end());
    signedData.insert(signedData.end(), clientDataHash,
                      clientDataHash + SHA256_DIGEST_LENGTH);

    // Public Key from DB
    auto pubKeyBytes = user.getValueOfPublicKey();

    const unsigned char *p = (const unsigned char *)pubKeyBytes.data();
    EVP_PKEY *pkey = d2i_PUBKEY(NULL, &p, pubKeyBytes.size());

    if (!pkey) {
      response.success = false;
      response.message = "Failed to load stored public key.";
      co_return response;
    }

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, pkey);

    // Verification
    // signature from DTO
    std::string sigStr = toStandardBase64(passkeyDto.getSignature());
    std::vector<char> sigBytes = drogon::utils::base64DecodeToVector(sigStr);

    int verifyResult =
        EVP_DigestVerify(ctx, (const unsigned char *)sigBytes.data(),
                         sigBytes.size(), signedData.data(), signedData.size());

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);

    if (verifyResult != 1) {
      response.success = false;
      response.message = "Signature verification failed.";
      co_return response;
    }

    // 3. Clone Check (Sign Count)
    // Extract signCount from authData (bytes 33-36)
    if (authData.size() < 37) {
      response.success = false;
      response.message = "Authenticator data too short.";
      co_return response;
    }

    uint32_t newSignCount =
        ((uint8_t)authData[33] << 24) | ((uint8_t)authData[34] << 16) |
        ((uint8_t)authData[35] << 8) | (uint8_t)authData[36];

    int64_t storedCount = user.getValueOfSignCount();

    if (newSignCount > 0 && newSignCount <= storedCount) {
      // Potential clone attack!
      response.success = false;
      response.message = "Invalid sign count (possible clone detected).";
      co_return response;
    }

    // 4. Update Sign Count & Issue Token
    Users userToUpdate = user;
    userToUpdate.setSignCount(newSignCount);

    co_await mp.update(userToUpdate);

    // Issue Token
    auto &app = drogon::app();
    auto customConfig = app.getCustomConfig();
    std::string jwtSecurityKey =
        customConfig["JwtBearer"]["JwtSecurityKey"].asString();
    std::string jwtIssuer = customConfig["JwtBearer"]["JwtIssuer"].asString();

    auto token =
        jwt::create()
            .set_issuer(jwtIssuer)
            .set_type("JWT")
            .set_issued_at(std::chrono::system_clock::now())
            .set_expires_at(std::chrono::system_clock::now() +
                            std::chrono::hours(24 * 366))
            .set_payload_claim("userId", jwt::claim(user.getValueOfId()))
            .set_payload_claim("username",
                               jwt::claim(user.getValueOfUsername()))
            .set_payload_claim("email", jwt::claim(user.getValueOfEmail()))
            .sign(jwt::algorithm::hs256{jwtSecurityKey});

    response.success = true;
    response.message = "Authentication successful";
    response.result["token"] = token;
    response.result["userId"] = user.getValueOfId();
    response.result["username"] = user.getValueOfUsername();
    response.result["fullName"] =
        user.getValueOfFirstName() + " " + user.getValueOfLastName();
    response.result["email"] = user.getValueOfEmail();

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.message = "User not found or invalid credentials.";
  }
  co_return response;
}

drogon::Task<dto::BaseApiResponse>
UserService::validateUserCredentials(const dto::SigninDto &signin_dto) {
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mapper(dbClient);

  Criteria criteria =
      (Criteria(Users::Cols::_username, CompareOperator::EQ,
                signin_dto.getUsernameOrEmail()) ||
       Criteria(Users::Cols::_phone_number, CompareOperator::EQ,
                signin_dto.getUsernameOrEmail()) ||
       Criteria(Users::Cols::_email, CompareOperator::EQ,
                signin_dto.getUsernameOrEmail())) &&
      Criteria(Users::Cols::_is_active, CompareOperator::EQ, true) &&
      Criteria(Users::Cols::_is_locked_out, CompareOperator::EQ, false);

  dto::BaseApiResponse response;

  try {
    Users user = co_await mapper.findOne(criteria);

    std::string storedHash = gnp::utils::PasswordUtils::normalizeBcryptHash(
        user.getValueOfPasswordHash());

    bool passwordMatches =
        bcrypt::validatePassword(signin_dto.getPassword(), storedHash);

    if (passwordMatches) {

      Users userToUpdate = user;
      userToUpdate.setLastActive(trantor::Date::now());
      co_await mapper.update(userToUpdate);

      // Password is correct, generate JWT token
      auto &app = drogon::app();
      auto customConfig = app.getCustomConfig();
      std::string jwtSecurityKey =
          customConfig["JwtBearer"]["JwtSecurityKey"].asString();
      std::string jwtIssuer = customConfig["JwtBearer"]["JwtIssuer"].asString();

      auto token =
          jwt::create()
              .set_issuer(jwtIssuer)
              .set_type("JWT")
              .set_issued_at(std::chrono::system_clock::now())
              .set_expires_at(std::chrono::system_clock::now() +
                              std::chrono::hours(24 * 366))
              .set_payload_claim("userId", jwt::claim(user.getValueOfId()))
              .set_payload_claim("username",
                                 jwt::claim(user.getValueOfUsername()))
              .set_payload_claim("email", jwt::claim(user.getValueOfEmail()))
              .sign(jwt::algorithm::hs256{jwtSecurityKey});

      response.success = true;
      response.message = "Authentication successful";
      response.result["token"] = token;
      response.result["userId"] = user.getValueOfId();
      response.result["username"] = user.getValueOfUsername();
      response.result["fullName"] =
          user.getValueOfFirstName() + " " + user.getValueOfLastName();
      response.result["email"] = user.getValueOfEmail();
    } else {
      // Password is incorrect
      response.success = false;
      response.message = "Invalid credentials";
      response.error["code"] = constants::ERR_AUTH_INVALID_CREDENTIALS;
    }
  } catch (const DrogonDbException &e) {
    // User not found
    response.success = false;
    response.message = "User not found";
    response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    response.error["message"] = "User not found";
  }
  co_return response;
}

drogon::Task<dto::BaseApiResponse>
UserService::validateAdminUserCredentials(const dto::SigninDto &signin_dto) {
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mapper(dbClient);

  std::string normalizedInput = signin_dto.getUsernameOrEmail();
  std::transform(normalizedInput.begin(), normalizedInput.end(),
                 normalizedInput.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  Criteria criteria =
      (Criteria(Users::Cols::_username, CompareOperator::EQ, normalizedInput) ||
       Criteria(Users::Cols::_email, CompareOperator::EQ, normalizedInput)) &&
      Criteria(Users::Cols::_is_active, CompareOperator::EQ, true) &&
      Criteria(Users::Cols::_is_admin_user, CompareOperator::EQ, true) &&
      Criteria(Users::Cols::_is_locked_out, CompareOperator::EQ, false);

  gnp::dto::BaseApiResponse response;
  try {
    Users user = co_await mapper.findOne(criteria);
    bool passwordMatches = bcrypt::validatePassword(
        signin_dto.getPassword(), user.getValueOfPasswordHash());

    if (passwordMatches) {

      Users userToUpdate = user;
      userToUpdate.setLastActive(trantor::Date::now());
      co_await mapper.update(userToUpdate);

      std::unordered_set<std::string> allPermissions = co_await PermissionService::getUserPermissions(user.getValueOfId());

      Json::Value permissionsArray = Json::arrayValue;
      for (const auto &perm : allPermissions) {
        permissionsArray.append(perm);
      }
      response.result["permissions"] = permissionsArray;

      // Add permissions to JWT token payload
      Json::FastWriter writer;
      std::string permissionsStr = writer.write(permissionsArray);

      // Password is correct, generate JWT token
      auto &app = drogon::app();
      auto customConfig = app.getCustomConfig();
      std::string jwtSecurityKey =
          customConfig["JwtBearer"]["JwtSecurityKey"].asString();
      std::string jwtIssuer = customConfig["JwtBearer"]["JwtIssuer"].asString();

      auto token =
          jwt::create()
              .set_issuer(jwtIssuer)
              .set_type("JWT")
              .set_issued_at(std::chrono::system_clock::now())
              .set_expires_at(std::chrono::system_clock::now() +
                              std::chrono::hours(24 * 3660))
              .set_payload_claim("userId", jwt::claim(user.getValueOfId()))
              .set_payload_claim("username",
                                 jwt::claim(user.getValueOfUsername()))
              .set_payload_claim("email", jwt::claim(user.getValueOfEmail()))
              .sign(jwt::algorithm::hs256{jwtSecurityKey});

      response.success = true;
      response.message = "Authentication successful";
      response.result["token"] = token;
      response.result["userId"] = user.getValueOfId();
      response.result["username"] = user.getValueOfUsername();
      response.result["fullName"] =
          user.getValueOfFirstName() + " " + user.getValueOfLastName();
      response.result["email"] = user.getValueOfEmail();
    } else {
      // Password is incorrect
      response.success = false;
      response.message = "Invalid credentials";
      response.error["code"] = constants::ERR_AUTH_INVALID_CREDENTIALS;
    }
  } catch (const DrogonDbException &e) {
    // Database error or user not found
    response.success = false;
    response.message = "User not found";
    response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    response.error["message"] = "User not found";
  }
  co_return response;
}

drogon::Task<dto::BaseApiResponse>
UserService::validatePartnerUserCredentials(const dto::SigninDto &signin_dto) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mapper(dbClient);

  // Normalize email/username to lowercase for case-insensitive email matching
  std::string normalizedInput = signin_dto.getUsernameOrEmail();
  std::transform(normalizedInput.begin(), normalizedInput.end(),
                 normalizedInput.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  Criteria criteria =
      (Criteria(Users::Cols::_username, CompareOperator::EQ, normalizedInput) ||
       Criteria(Users::Cols::_email, CompareOperator::EQ, normalizedInput)) &&
      Criteria(Users::Cols::_is_active, CompareOperator::EQ, true) &&
      Criteria(Users::Cols::_is_partner_admin_user, CompareOperator::EQ,
               true) &&
      Criteria(Users::Cols::_is_locked_out, CompareOperator::EQ, false);

  dto::BaseApiResponse response;

  try {

    Users user = co_await mapper.findOne(criteria);

    bool passwordMatches = bcrypt::validatePassword(
        signin_dto.getPassword(), user.getValueOfPasswordHash());

    if (passwordMatches) {

      // Update last_active timestamp
      Users userToUpdate = user;
      userToUpdate.setLastActive(trantor::Date::now());
      co_await mapper.update(userToUpdate);

      // Password is correct, generate JWT token
      auto &app = drogon::app();
      auto customConfig = app.getCustomConfig();
      std::string jwtSecurityKey =
          customConfig["JwtBearer"]["JwtSecurityKey"].asString();
      std::string jwtIssuer = customConfig["JwtBearer"]["JwtIssuer"].asString();

      CoroMapper<drogon_model::Gnp::CommercialPartners> cpMapper(dbClient);

      auto commercialPartner =
          co_await cpMapper.findByPrimaryKey(user.getValueOfPartnerId());

      if (!commercialPartner.getValueOfSubAccountEnabled()) {

        response.success = false;
        response.message = "Access denied !";
        response.error["code"] = constants::ERR_UNAUTHORIZED;
        response.error["message"] =
            "Sub-account functionality is not enabled for " +
            commercialPartner.getValueOfName() +
            ". Please contact your administrator";
        co_return response;
      }

      if (commercialPartner.getValueOfRequireTwoFactorAuth()) {

        // send an OTP email to the partner user ...
        std::string requestId = drogon::utils::getUuid();

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> distrib(100000, 999999);
        std::string otp = std::to_string(distrib(gen));

        auto redisClient = drogon::app().getRedisClient();
        std::string redisValue = otp + ":" + user.getValueOfId();
        co_await redisClient->execCommandCoro(
            "SETEX %s %d %s", requestId.c_str(), 300, redisValue.c_str());

        auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
        auto &emailService = plugin->getEmailService();

        gnp::dto::SendEmailDto emailDto;
        emailDto.setTo(user.getValueOfEmail());
        emailDto.setSubject("Your OTP for Graphic News Plus Verification");

        std::string emailBody =
            R"(
            <!DOCTYPE html>
            <html>
            <head>
            <style>
              body { font-family: 'Helvetica Neue', Helvetica, Arial, sans-serif; background-color: #f9f9f9; margin: 0; padding: 0; }
              .container { max-width: 600px; margin: 40px auto; background-color: #ffffff; border-radius: 10px; overflow: hidden; box-shadow: 0 4px 12px rgba(0,0,0,0.05); }
              .header { background-color: #ffffff; padding: 30px 20px 20px; text-align: center; border-bottom: 1px solid #f0f0f0; }
              .header img { height: 60px; margin-bottom: 15px; border-radius: 8px; }
              .header h1 { margin: 0; font-size: 24px; font-weight: 600; color: #1a1a1a; }
              .content { padding: 40px 30px; text-align: center; color: #4a4a4a; line-height: 1.6; }
              .content p { margin: 0 0 15px; font-size: 16px; }
              .otp-container { margin: 30px 0; padding: 20px; background-color: #fdf2f2; border-radius: 8px; border: 1px dashed #f5c6c6; }
              .otp { font-size: 36px; font-weight: bold; color: #D32F2F; letter-spacing: 8px; margin: 0; }
              .footer { background-color: #f9f9f9; color: #999999; padding: 20px; text-align: center; font-size: 13px; border-top: 1px solid #eeeeee; }
            </style>
            </head>
            <body>
            <div class="container">
              <div class="header">
                <img src="https://dev.graphicnewsplus.com/favicon-mag.png" style="height: 35px;width: 35px;" alt="Graphic News Plus Logo">
                <h1>Graphic News Plus</h1>
              </div>
              <div class="content">
                <p>Hello )" +
            user.getValueOfFirstName() + R"(,</p>
                <p>Your One-Time Password (OTP) for verification is:</p>
                <div class="otp-container">
                  <div class="otp">)" +
            otp + R"(</div>
                </div>
                <p>This code will expire in 5 minutes.</p>
                <p>If you did not request this code, please ignore this email.</p>
              </div>
              <div class="footer"> &copy; )" +
            trantor::Date::now().toCustomFormattedString("%Y") +
            R"( Graphic News Plus. All rights reserved.
              </div>
            </div>
            </body>
            </html>
            )";

        emailDto.setBody(emailBody);

        co_await emailService.sendEmailAsync(emailDto);
        response.success = true;
        response.message = "OTP sent to " + user.getValueOfEmail();
        response.result["requestId"] = requestId;
        response.result["requiresTwoFactorAuth"] = true;

        co_return response;
      }

      auto token =
          jwt::create()
              .set_issuer(jwtIssuer)
              .set_type("JWT")
              .set_issued_at(std::chrono::system_clock::now())
              .set_expires_at(std::chrono::system_clock::now() +
                              std::chrono::hours(24 * 366))
              .set_payload_claim("partnerId",
                                 jwt::claim(user.getValueOfPartnerId()))
              .set_payload_claim(
                  "partnerEmail",
                  jwt::claim(commercialPartner.getValueOfBillingEmail()))
              .set_payload_claim("partnerUserId",
                                 jwt::claim(user.getValueOfId()))
              .set_payload_claim(
                  "partnerUserEmail",
                  jwt::claim(commercialPartner.getValueOfContactEmail()))
              .set_payload_claim("partnerName",
                                 jwt::claim(commercialPartner.getValueOfName()))
              .sign(jwt::algorithm::hs256{jwtSecurityKey});

      response.success = true;
      response.message = "Partner Authentication successful";
      response.result["token"] = token;
      response.result["partnerUserId"] = user.getValueOfPartnerId();
      response.result["partnerName"] = commercialPartner.getValueOfName();
      response.result["fullName"] =
          user.getValueOfFirstName() + " " + user.getValueOfLastName();
      response.result["partnerEmail"] =
          commercialPartner.getValueOfBillingEmail();
      response.result["requiresTwoFactorAuth"] = false;
    } else {
      // Password is incorrect
      response.success = false;
      response.message = "Invalid credentials";
      response.result["requiresTwoFactorAuth"] = false;
      response.error["code"] = constants::ERR_AUTH_INVALID_CREDENTIALS;
    }
  } catch (const DrogonDbException &e) {
    // Database error or user not found
    response.success = false;
    response.message = "User not found";
    response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    response.error["message"] = "User not found";
    response.result["requiresTwoFactorAuth"] = false;
  }
  co_return response;
}

drogon::Task<dto::BaseApiResponse>
UserService::validateAffiliateUserCredentials(
    const dto::SigninDto &signin_dto) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mapper(dbClient);

  // Normalize email/username to lowercase for case-insensitive email matching
  std::string normalizedInput = signin_dto.getUsernameOrEmail();
  std::transform(normalizedInput.begin(), normalizedInput.end(),
                 normalizedInput.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  Criteria criteria =
      (Criteria(Users::Cols::_username, CompareOperator::EQ, normalizedInput) ||
       Criteria(Users::Cols::_email, CompareOperator::EQ, normalizedInput)) &&
      Criteria(Users::Cols::_is_active, CompareOperator::EQ, true) &&
      Criteria(Users::Cols::_is_affiliate, CompareOperator::EQ, true) &&
      Criteria(Users::Cols::_is_locked_out, CompareOperator::EQ, false);

  dto::BaseApiResponse response;

  try {

    Users user = co_await mapper.findOne(criteria);

    bool passwordMatches = bcrypt::validatePassword(
        signin_dto.getPassword(), user.getValueOfPasswordHash());

    if (passwordMatches) {

      // Update last_active timestamp
      Users userToUpdate = user;
      userToUpdate.setLastActive(trantor::Date::now());
      co_await mapper.update(userToUpdate);

      // Password is correct, generate JWT token
      auto &app = drogon::app();
      auto customConfig = app.getCustomConfig();
      std::string jwtSecurityKey =
          customConfig["JwtBearer"]["JwtSecurityKey"].asString();
      std::string jwtIssuer = customConfig["JwtBearer"]["JwtIssuer"].asString();

      CoroMapper<drogon_model::Gnp::Affiliates> affiliateMapper(dbClient);

      auto affiliate = co_await affiliateMapper.findByPrimaryKey(
          user.getValueOfAffiliateId());

      if (affiliate.getValueOfRequireTwoFactorAuth()) {

        // send an OTP email to the partner user ...
        std::string requestId = drogon::utils::getUuid();

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> distrib(100000, 999999);
        std::string otp = std::to_string(distrib(gen));

        auto redisClient = drogon::app().getRedisClient();
        std::string redisValue = otp + ":" + user.getValueOfId();
        co_await redisClient->execCommandCoro(
            "SETEX %s %d %s", requestId.c_str(), 300, redisValue.c_str());

        auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
        auto &emailService = plugin->getEmailService();

        gnp::dto::SendEmailDto emailDto;
        emailDto.setTo(user.getValueOfEmail());
        emailDto.setSubject("Your OTP for Graphic News Plus Verification");

        std::string emailBody =
            R"(
            <!DOCTYPE html>
            <html>
            <head>
            <style>
              body { font-family: 'Helvetica Neue', Helvetica, Arial, sans-serif; background-color: #f9f9f9; margin: 0; padding: 0; }
              .container { max-width: 600px; margin: 40px auto; background-color: #ffffff; border-radius: 10px; overflow: hidden; box-shadow: 0 4px 12px rgba(0,0,0,0.05); }
              .header { background-color: #ffffff; padding: 30px 20px 20px; text-align: center; border-bottom: 1px solid #f0f0f0; }
              .header img { height: 60px; margin-bottom: 15px; border-radius: 8px; }
              .header h1 { margin: 0; font-size: 24px; font-weight: 600; color: #1a1a1a; }
              .content { padding: 40px 30px; text-align: center; color: #4a4a4a; line-height: 1.6; }
              .content p { margin: 0 0 15px; font-size: 16px; }
              .otp-container { margin: 30px 0; padding: 20px; background-color: #fdf2f2; border-radius: 8px; border: 1px dashed #f5c6c6; }
              .otp { font-size: 36px; font-weight: bold; color: #D32F2F; letter-spacing: 8px; margin: 0; }
              .footer { background-color: #f9f9f9; color: #999999; padding: 20px; text-align: center; font-size: 13px; border-top: 1px solid #eeeeee; }
            </style>
            </head>
            <body>
            <div class="container">
              <div class="header">
                <img src="https://dev.graphicnewsplus.com/favicon-mag.png" style="height: 35px;width: 35px;" alt="Graphic News Plus Logo">
                <h1>Graphic News Plus</h1>
              </div>
              <div class="content">
                <p>Hello )" +
            user.getValueOfFirstName() + R"(,</p>
                <p>Your One-Time Password (OTP) for verification is:</p>
                <div class="otp-container">
                  <div class="otp">)" +
            otp + R"(</div>
                </div>
                <p>This code will expire in 5 minutes.</p>
                <p>If you did not request this code, please ignore this email.</p>
              </div>
              <div class="footer"> &copy; )" +
            trantor::Date::now().toCustomFormattedString("%Y") +
            R"( Graphic News Plus. All rights reserved.
              </div>
            </div>
            </body>
            </html>
            )";

        emailDto.setBody(emailBody);

        co_await emailService.sendEmailAsync(emailDto);
        response.success = true;
        response.message = "OTP sent to " + user.getValueOfEmail();
        response.result["requestId"] = requestId;
        response.result["requiresTwoFactorAuth"] = true;

        co_return response;
      }

      auto token =
          jwt::create()
              .set_issuer(jwtIssuer)
              .set_type("JWT")
              .set_issued_at(std::chrono::system_clock::now())
              .set_expires_at(std::chrono::system_clock::now() +
                              std::chrono::hours(24 * 366))
              .set_payload_claim("affiliateId",
                                 jwt::claim(user.getValueOfAffiliateId()))
              .set_payload_claim("affiliateUserId",
                                 jwt::claim(user.getValueOfId()))
              .set_payload_claim("affiliateEmail",
                                 jwt::claim(affiliate.getValueOfEmail()))
              .set_payload_claim("affiliateName",
                                 jwt::claim(affiliate.getValueOfFirstName() +
                                            " " +
                                            affiliate.getValueOfLastName()))
              .sign(jwt::algorithm::hs256{jwtSecurityKey});

      response.success = true;
      response.message = "Affiliate Authentication successful";
      response.result["token"] = token;
      response.result["affiliateId"] = affiliate.getValueOfId();
      response.result["affiliateName"] = affiliate.getValueOfFirstName() + " " +
                                         affiliate.getValueOfLastName();
      response.result["affiliateEmail"] = affiliate.getValueOfEmail();
      response.result["requiresTwoFactorAuth"] = false;
    } else {
      // Password is incorrect
      response.success = false;
      response.message = "Invalid credentials";
      response.result["requiresTwoFactorAuth"] = false;
      response.error["code"] = constants::ERR_AUTH_INVALID_CREDENTIALS;
    }
  } catch (const DrogonDbException &e) {
    // Database error or user not found
    response.success = false;
    response.message = "Affiliate not found";
    response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    response.error["message"] = "Affiliate not found";
    response.result["requiresTwoFactorAuth"] = false;
  }
  co_return response;
}

drogon::Task<dto::BaseApiResponse>
UserService::validatePartnerUserOtp(const dto::VerifyPartnerUserOtpDto &dto) {
  gnp::dto::BaseApiResponse response;

  try {
    auto redisClient = drogon::app().getRedisClient();
    auto r = co_await redisClient->execCommandCoro("GET %s",
                                                   dto.getRequestId().c_str());

    if (r.type() == drogon::nosql::RedisResultType::kNil) {
      response.success = false;
      response.message = "OTP expired or invalid request ID";
      response.error["code"] = constants::ERR_AUTH_INVALID_CREDENTIALS;
      co_return response;
    }

    std::string redisValue = r.asString();
    size_t colonPos = redisValue.find(':');
    if (colonPos == std::string::npos) {
      response.success = false;
      response.message = "Invalid OTP data format";
      co_return response;
    }

    std::string storedOtp = redisValue.substr(0, colonPos);
    std::string userId = redisValue.substr(colonPos + 1);

    if (storedOtp != dto.getOtp()) {
      response.success = false;
      response.message = "Invalid OTP";
      response.error["code"] = constants::ERR_AUTH_INVALID_CREDENTIALS;
      co_return response;
    }

    // OTP is valid. Now generate the auth token.
    // First, delete the OTP so it can't be reused
    co_await redisClient->execCommandCoro("DEL %s", dto.getRequestId().c_str());

    auto dbClient = drogon::app().getDbClient();
    drogon::orm::CoroMapper<Users> mapper(dbClient);
    Users user = co_await mapper.findByPrimaryKey(userId);

    drogon::orm::CoroMapper<drogon_model::Gnp::CommercialPartners> cpMapper(
        dbClient);
    auto commercialPartner =
        co_await cpMapper.findByPrimaryKey(user.getValueOfPartnerId());

    auto &app = drogon::app();
    auto customConfig = app.getCustomConfig();
    std::string jwtSecurityKey =
        customConfig["JwtBearer"]["JwtSecurityKey"].asString();
    std::string jwtIssuer = customConfig["JwtBearer"]["JwtIssuer"].asString();

    auto token =
        jwt::create()
            .set_issuer(jwtIssuer)
            .set_type("JWT")
            .set_issued_at(std::chrono::system_clock::now())
            .set_expires_at(std::chrono::system_clock::now() +
                            std::chrono::hours(24 * 30))
            .set_payload_claim("partnerId",
                               jwt::claim(user.getValueOfPartnerId()))
            .set_payload_claim(
                "partnerEmail",
                jwt::claim(commercialPartner.getValueOfBillingEmail()))
            .set_payload_claim("partnerUserId", jwt::claim(user.getValueOfId()))
            .set_payload_claim(
                "partnerUserEmail",
                jwt::claim(commercialPartner.getValueOfContactEmail()))
            .set_payload_claim("partnerName",
                               jwt::claim(commercialPartner.getValueOfName()))
            .sign(jwt::algorithm::hs256{jwtSecurityKey});

    response.success = true;
    response.message = "Partner Authentication successful";
    response.result["token"] = token;
    response.result["partnerUserId"] = user.getValueOfPartnerId();
    response.result["partnerName"] = commercialPartner.getValueOfName();
    response.result["fullName"] =
        user.getValueOfFirstName() + " " + user.getValueOfLastName();
    response.result["partnerEmail"] =
        commercialPartner.getValueOfBillingEmail();

  } catch (const drogon::orm::DrogonDbException &e) {
    response.success = false;
    response.message = "Database error or user not found";
    response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
  } catch (const std::exception &e) {
    response.success = false;
    response.message = "Redis or Server error";
    response.error["details"] = e.what();
  }

  co_return response;
}

drogon::Task<dto::BaseApiResponse>
UserService::lockUserAccount(const std::string &userId) {
  auto dbClient = drogon::app().getDbClient();
  auto mp = drogon::orm::CoroMapper<Users>(dbClient);

  try {
    Users user = co_await mp.findOne(
        Criteria(Users::Cols::_id, CompareOperator::EQ, userId));

    if (user.getValueOfIsLockedOut()) {
      gnp::dto::BaseApiResponse response;
      response.success = true;
      response.message = "User account is already locked.";
      co_return response;
    }

    user.setIsLockedOut(true);
    co_await mp.update(user);

    gnp::dto::BaseApiResponse response;
    response.success = true;
    response.message = "User account locked successfully";
    co_return response;

  } catch (const DrogonDbException &e) {
    gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    if (e.base().what() ==
        std::string("Unexpected row number")) { // findOne throws if not found
      errorResponse.message = "User not found";
      errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    } else {
      errorResponse.message = "Failed to lock user account";
      errorResponse.error["code"] = constants::ERR_DB_QUERY;
      errorResponse.error["detail"] = e.base().what();
    }
    co_return errorResponse;
  }
}

drogon::Task<dto::BaseApiResponse>
UserService::unlockUserAccount(const std::string &userId) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mp(dbClient);

  gnp::dto::BaseApiResponse response;
  try {
    Users user = co_await mp.findOne(
        Criteria(Users::Cols::_id, CompareOperator::EQ, userId));

    if (!user.getValueOfIsLockedOut()) {
      response.success = true;
      response.message = "User account is already unlocked.";
      co_return response;
    }

    user.setIsLockedOut(false);
    co_await mp.update(user);

    response.success = true;
    response.message = "User account unlocked successfully";

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.message = "User not found or database error";
    response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
  }
  co_return response;
}

drogon::Task<dto::BaseApiResponse>
UserService::activateUserAccount(const std::string &userId) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mp(dbClient);

  gnp::dto::BaseApiResponse response;
  try {
    Users user = co_await mp.findOne(
        Criteria(Users::Cols::_id, CompareOperator::EQ, userId));

    if (user.getValueOfIsActive()) {
      response.success = true;
      response.message = "User account is already active.";
      co_return response;
    }

    user.setIsActive(true);
    co_await mp.update(user);

    response.success = true;
    response.message = "User account activated successfully";

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.message = "User not found or database error";
    response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
  }
  co_return response;
}

drogon::Task<dto::BaseApiResponse>
UserService::deactivateUserAccount(const std::string &userId) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mp(dbClient);

  gnp::dto::BaseApiResponse response;
  try {
    Users user = co_await mp.findOne(
        Criteria(Users::Cols::_id, CompareOperator::EQ, userId));

    if (!user.getValueOfIsActive()) {
      response.success = true;
      response.message = "User account is already inactive.";
      co_return response;
    }

    user.setIsActive(false);
    co_await mp.update(user);

    response.success = true;
    response.message = "User account deactivated successfully";

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.message = "User not found or database error";
    response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
  }
  co_return response;
}

drogon::Task<dto::BaseApiResponse>
UserService::deleteUser(const std::string &userId) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mp(dbClient);

  gnp::dto::BaseApiResponse response;
  try {
    // Check if user exists first
    co_await mp.findOne(
        Criteria(Users::Cols::_id, CompareOperator::EQ, userId));

    // Delete the user
    size_t count = co_await mp.deleteBy(
        Criteria(Users::Cols::_id, CompareOperator::EQ, userId));

    if (count > 0) {
      response.success = true;
      response.message = "User deleted successfully";
    } else {
      // This part should technically not be reached if findOne succeeded,
      // but added for completeness.
      response.success = false;
      response.message = "Failed to delete user";
      response.error["code"] = constants::ERR_DB_QUERY;
    }

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.message = "User not found or database error";
    response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
  }
  co_return response;
}

drogon::Task<gnp::dto::BaseApiResponse>
UserService::resetUserPassword(const std::string &userId) {
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mp(dbClient);

  try {
    // 1. Find the user
    auto user = co_await mp.findByPrimaryKey(userId);

    // 2. Generate new random password
    std::string newPassword =
        gnp::utils::PasswordUtils::generateRandomPassword(8);

    // 3. Update password hash
    user.setPasswordHash(bcrypt::generateHash(newPassword));
    user.setUpdatedAt(trantor::Date::now());
    co_await mp.update(user);

    // 4. Send email with new credentials
    auto plugin = drogon::app().getPlugin<plugins::GnpServicePlugin>();
    auto &emailService = plugin->getEmailService();

    dto::SendEmailDto emailDto;
    emailDto.setTo(gnp::utils::StringUtils::trim(user.getValueOfEmail()));
    emailDto.setSubject("Graphic News Plus - Password Reset");

    std::string emailBody = R"html(
      <!DOCTYPE html>
      <html>
      <head>
      <meta charset="UTF-8">
      <style>
        body { font-family: Arial, sans-serif; background-color: #f4f4f4; margin: 0; padding: 0; }
        .container { max-width: 600px; margin: 20px auto; background-color: #ffffff; border-radius: 8px; overflow: hidden; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
        .header { background-color: #D32F2F; color: #ffffff; padding: 20px; text-align: center; }
        .content { padding: 30px; color: #333333; }
        .credentials { background-color: #f9f9f9; padding: 15px; border-radius: 5px; margin: 20px 0; }
        .credential-item { margin: 10px 0; }
        .credential-label { font-weight: bold; color: #666; }
        .credential-value { font-size: 18px; color: #D32F2F; font-family: monospace; }
        .footer { background-color: #f4f4f4; color: #666666; padding: 10px; text-align: center; font-size: 12px; }
      </style>
      </head>
      <body>
      <div class="container">
        <div class="header">
          <h1>Graphic News Plus</h1>
        </div>
        <div class="content">
          <p>Hello )html" + user.getValueOfFirstName() +
                            R"html(,</p>
          <p>Your password for Graphic News Plus has been reset by an administrator.</p>
          <p>Below are your new login credentials:</p>
          <div class="credentials">
            <div class="credential-item">
              <div class="credential-label">Username:</div>
              <div class="credential-value">)html" +
                            user.getValueOfUsername() + R"html(</div>
            </div>
            <div class="credential-item">
              <div class="credential-label">Email:</div>
              <div class="credential-value">)html" +
                            user.getValueOfEmail() + R"html(</div>
            </div>
            <div class="credential-item">
              <div class="credential-label">New Password:</div>
              <div class="credential-value">)html" +
                            newPassword + R"html(</div>
            </div>
          </div>
          <p>Please keep these credentials secure and change your password after your next login.</p>
          <p>You can access the platform at: <a href="https://new.graphicnewsplus.com">https://new.graphicnewsplus.com</a></p>
        </div>
        <div class="footer">
          &copy; )html" + trantor::Date::now().toCustomFormattedString("%Y") +
                            R"html( Graphic News Plus. All rights reserved.
        </div>
      </div>
      </body>
      </html>
    )html";

    emailDto.setBody(emailBody);
    co_await emailService.sendEmailAsync(emailDto);

    gnp::dto::BaseApiResponse response;
    response.success = true;
    response.message = "Password reset successfully. New password has been "
                       "sent to the user's email.";
    response.result["userId"] = userId;
    co_return response;

  } catch (const drogon::orm::UnexpectedRows &e) {
    gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "User not found";
    errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    co_return errorResponse;
  } catch (const DrogonDbException &e) {
    gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Database error while resetting password";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<gnp::dto::BaseApiResponse>
UserService::deletePartnerAdminUser(const std::string &userId,
                                    const std::string &partnerId) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mp(dbClient);

  gnp::dto::BaseApiResponse response;
  try {
    // Check if user exists first and belongs to partner
    co_await mp.findOne(
        Criteria(Users::Cols::_id, CompareOperator::EQ, userId) &&
        Criteria(Users::Cols::_partner_id, CompareOperator::EQ, partnerId));

    // Delete the user
    size_t count = co_await mp.deleteBy(
        Criteria(Users::Cols::_id, CompareOperator::EQ, userId));

    if (count > 0) {
      response.success = true;
      response.message = "Admin user deleted successfully";
    } else {
      // This part should technically not be reached if findOne succeeded,
      // but added for completeness.
      response.success = false;
      response.message = "Failed to delete user";
    }
  } catch (const DrogonDbException &e) {
    response.success = false;
    response.message = "User not found or database error";
    response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
  }
  co_return response;
}

// user profile
drogon::Task<dto::BaseApiResponse>
UserService::getUserMetaData(const std::string &userId) {

  auto dbClient = drogon::app().getDbClient();
  dto::BaseApiResponse response;

  try {
    auto userMp = drogon::orm::CoroMapper<Users>(dbClient);
    auto user = co_await userMp.findOne(
        Criteria(Users::Cols::_id, CompareOperator::EQ, userId));

    Json::Value result;

    // BioData
    Json::Value bioData;
    bioData["fullname"] =
        user.getValueOfFirstName() + " " + user.getValueOfLastName();
    bioData["username"] = user.getValueOfUsername();
    bioData["email"] = user.getValueOfEmail();
    bioData["phoneNumber"] = user.getValueOfPhoneNumber();
    result["bioData"] = bioData;

    // Subscriptions
    Json::Value subscriptions = Json::arrayValue;

    auto subMp =
        drogon::orm::CoroMapper<drogon_model::Gnp::UserSubscriptions>(dbClient);

    auto subs = co_await subMp.findBy(
        Criteria(drogon_model::Gnp::UserSubscriptions::Cols::_user_id,
                 CompareOperator::EQ, userId));

    for (const auto &sub : subs) {
      Json::Value subJson;
      Json::Value subModelJson = sub.toJson();

      subJson["id"] = subModelJson["id"];

      std::string identifier =
          subModelJson["subscription_plan_description"].asString();
      if (identifier.empty()) {
        identifier = subModelJson["subscription_identifier"].asString();
      }
      subJson["subscriptionIdentifier"] = identifier;

      subJson["email"] = subModelJson["email"];
      subJson["currentSubscriptionPlanId"] =
          subModelJson["subscription_plan_id"];
      subJson["startDate"] = subModelJson["start_date"];
      subJson["endDate"] = subModelJson["end_date"];
      subJson["currentBillingCycle"] = subModelJson["billing_cycle"];
      subJson["isActive"] = subModelJson["is_active"].asBool();
      subJson["nextRenewalDate"] = subModelJson["end_date"];
      subJson["createdAt"] = subModelJson["created_at"];

      subscriptions.append(subJson);
    }
    result["subscriptions"] = subscriptions;

    // Transactions
    Json::Value transactions = Json::arrayValue;
    auto payMp = drogon::orm::CoroMapper<drogon_model::Gnp::Payments>(dbClient);
    auto pays =
        co_await payMp
            .orderBy(drogon_model::Gnp::Payments::Cols::_created_at,
                     SortOrder::DESC)
            .limit(5)
            .findBy(Criteria(drogon_model::Gnp::Payments::Cols::_user_id,
                             CompareOperator::EQ, userId));

    for (const auto &pay : pays) {
      Json::Value payJson;
      Json::Value payModelJson = pay.toJson();

      payJson["id"] = payModelJson["id"];
      payJson["userName"] = payModelJson["user_name"];
      payJson["userEmail"] = payModelJson["user_email"];
      payJson["packageName"] = payModelJson["package_name"];
      payJson["amountPaid"] = payModelJson["amount_paid"];
      payJson["receiptNo"] = payModelJson["receipt_no"];
      payJson["transactionReference"] = payModelJson["transaction_reference"];
      payJson["status"] = payModelJson["status"];
      payJson["createdAt"] = payModelJson["created_at"];

      transactions.append(payJson);
    }
    result["transactions"] = transactions;

    response.success = true;
    response.result = result;

  } catch (const drogon::orm::DrogonDbException &e) {
    response.success = false;
    if (e.base().what() == std::string("Unexpected row number")) {
      response.message = "User not found";
      response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    } else {
      response.message = "Database error while fetching user metadata.";
      response.error["code"] = constants::ERR_DB_QUERY;
      response.error["detail"] = e.base().what();
    }
  }

  co_return response;
}

drogon::Task<dto::BaseApiResponse>
UserService::changeUserPassword(const std::string userId,
                                std::string accountType,
                                const dto::ChangePasswordDto &dto) {

  // accountTypes : public-user, admin-user, partner-admin-user
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mapper(dbClient);

  dto::BaseApiResponse response;

  Criteria criteria(Users::Cols::_id, CompareOperator::EQ, userId);

  if (accountType == "admin-user") {
    criteria = criteria &&
               Criteria(Users::Cols::_is_admin_user, CompareOperator::EQ, true);
  } else if (accountType == "partner-admin-user") {
    criteria = criteria && Criteria(Users::Cols::_is_partner_admin_user,
                                    CompareOperator::EQ, true);
  }

  try {
    Users user = co_await mapper.findOne(criteria);

    std::string storedHash = gnp::utils::PasswordUtils::normalizeBcryptHash(
        user.getValueOfPasswordHash());
    bool passwordMatches =
        bcrypt::validatePassword(dto.getOldPassword(), storedHash);

    if (!passwordMatches) {
      response.success = false;
      response.message = "Incorrect current password";
      response.error["code"] = constants::ERR_AUTH_INVALID_CREDENTIALS;
      co_return response;
    }

    std::string newHash = bcrypt::generateHash(dto.getNewPassword());
    user.setPasswordHash(newHash);
    user.setUpdatedAt(trantor::Date::now());

    co_await mapper.update(user);

    response.success = true;
    response.message = "Password changed successfully";

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.message = "User not found";
    response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
  }

  co_return response;
}

drogon::Task<dto::BaseApiResponse>
UserService::getAllUserGroups(int pageNo, int pageSize,
                              const std::string &query) {
  auto dbClient = drogon::app().getDbClient();
  auto mp = CoroMapper<UserGroups>(dbClient);

  Criteria searchCriteria;
  if (!query.empty()) {
    std::string likeQuery = "%" + query + "%";
    searchCriteria =
        Criteria(UserGroups::Cols::_title, CompareOperator::Like, likeQuery);
  }

  dto::BaseApiResponse response;
  try {
    size_t totalCount = co_await mp.count(searchCriteria);
    if (totalCount == 0) {
      response.success = true;
      response.result["data"] = Json::arrayValue;
      response.result["totalCount"] = 0;
      co_return response;
    }

    int offset = (pageNo - 1) * pageSize;
    auto groups =
        co_await mp.limit(pageSize).offset(offset).findBy(searchCriteria);

    auto totalPages = (totalCount + pageSize - 1) / pageSize;

    response.success = true;
    response.result["totalCount"] = (Json::UInt64)totalCount;
    response.result["pageNo"] = pageNo;
    response.result["pageSize"] = pageSize;
    response.result["totalPages"] =
        (int)((totalCount + pageSize - 1) / pageSize);
    response.result["lowerBound"] = pageSize * (pageNo - 1) + 1;
    response.result["upperBound"] = (int)totalPages == pageNo
                                        ? (Json::UInt64)totalCount
                                        : (Json::UInt64)(pageNo * pageSize);

    Json::Value data = Json::arrayValue;

    for (const auto &group : groups) {
      Json::Value groupJson = group.toJson();
      Json::Value camelCaseGroup;
      camelCaseGroup["id"] = groupJson["id"];
      camelCaseGroup["title"] = groupJson["title"];
      camelCaseGroup["parentId"] = groupJson["parent_id"];
      camelCaseGroup["path"] = groupJson["path"];

      if (group.getRoles()) {
        Json::Value rolesJson;
        Json::Reader reader;
        if (reader.parse(group.getValueOfRoles(), rolesJson)) {
          camelCaseGroup["roles"] = rolesJson;
        } else {
          camelCaseGroup["roles"] = group.getValueOfRoles();
        }
      } else {
        camelCaseGroup["roles"] = Json::arrayValue;
      }

      camelCaseGroup["activeUsers"] = groupJson["active_users"];
      camelCaseGroup["blockedUsers"] = groupJson["blocked_users"];
      camelCaseGroup["createdBy"] = groupJson["created_by"];
      camelCaseGroup["createdAt"] = groupJson["created_at"];
      camelCaseGroup["modifiedBy"] = groupJson["modified_by"];
      camelCaseGroup["modifiedAt"] = groupJson["modified_at"];
      data.append(camelCaseGroup);
    }
    response.result["data"] = data;

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.error["message"] = "Database error while fetching user groups.";
    response.error["detail"] = e.base().what();
  }
  co_return response;
}

drogon::Task<dto::BaseApiResponse>
UserService::createUserGroup(const dto::UserGroupDto &userGroupDto) {
  auto dbClient = drogon::app().getDbClient();
  auto mp = CoroMapper<UserGroups>(dbClient);

  dto::BaseApiResponse response;
  try {
    size_t count = co_await mp.count(Criteria(UserGroups::Cols::_title,
                                              CompareOperator::EQ,
                                              userGroupDto.getTitle()));
    if (count > 0) {
      response.success = false;
      response.message = "User group with this name already exists";
      response.error["code"] = constants::ERR_DUPLICATE_RESOURCE;
      co_return response;
    }

    UserGroups group;
    group.setTitle(userGroupDto.getTitle());
    group.setDescription(userGroupDto.getDescription());

    if (userGroupDto.hasParentId() && !userGroupDto.getParentId().empty()) {
      group.setParentId(userGroupDto.getParentId());
    }

    if (userGroupDto.hasRoles() && !userGroupDto.getRoles().empty()) {
      Json::Value rolesJson = Json::arrayValue;
      for (const auto &item : userGroupDto.getRoles()) {
        Json::Value r;
        r["id"] = item.id;
        r["name"] = item.name;
        rolesJson.append(r);
      }
      Json::FastWriter writer;
      group.setRoles(writer.write(rolesJson));
    }

    group.setActiveUsers(0);
    group.setBlockedUsers(0);
    auto now = trantor::Date::now();
    group.setCreatedAt(now);
    group.setModifiedAt(now);

    auto inserted = co_await mp.insert(group);

    response.success = true;
    response.message = "User group created successfully";
    response.result["userGroupId"] = inserted.getValueOfId();

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.message = "Failed to create user group";
    response.error["detail"] = e.base().what();
  }
  co_return response;
}

drogon::Task<dto::BaseApiResponse>
UserService::updateUserGroup(const dto::UserGroupDto &userGroupDto,
                             const std::string &userGroupId) {
  auto dbClient = drogon::app().getDbClient();
  auto mp = CoroMapper<UserGroups>(dbClient);

  dto::BaseApiResponse response;
  try {
    auto group = co_await mp.findOne(
        Criteria(UserGroups::Cols::_id, CompareOperator::EQ, userGroupId));

    if (!userGroupDto.getTitle().empty() &&
        userGroupDto.getTitle() != group.getValueOfTitle()) {
      size_t nameCount = co_await mp.count(Criteria(UserGroups::Cols::_title,
                                                    CompareOperator::EQ,
                                                    userGroupDto.getTitle()));
      if (nameCount > 0) {
        response.success = false;
        response.message = "User group with this name already exists";
        response.error["code"] = constants::ERR_DUPLICATE_RESOURCE;
        co_return response;
      }
      group.setTitle(userGroupDto.getTitle());
      group.setDescription(userGroupDto.getDescription());
    }

    if (userGroupDto.hasRoles()) {
      if (!userGroupDto.getRoles().empty()) {
        Json::Value rolesJson = Json::arrayValue;
        for (const auto &item : userGroupDto.getRoles()) {
          Json::Value r;
          r["id"] = item.id;
          r["name"] = item.name;
          rolesJson.append(r);
        }
        Json::FastWriter writer;
        group.setRoles(writer.write(rolesJson));
      } else {
        group.setRolesToNull();
      }
    }

    if (userGroupDto.hasParentId()) {
      if (!userGroupDto.getParentId().empty()) {
        group.setParentId(userGroupDto.getParentId());
      } else {
        group.setParentIdToNull();
      }
    }

    group.setModifiedAt(trantor::Date::now());
    co_await mp.update(group);

    response.success = true;
    response.message = "User group updated successfully";

  } catch (const UnexpectedRows &e) {
    response.success = false;
    response.message = "User group not found";
    response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
  } catch (const DrogonDbException &e) {
    response.success = false;
    response.message = "Failed to update user group";
    response.error["detail"] = e.base().what();
  }
  co_return response;
}

drogon::Task<dto::BaseApiResponse>
UserService::deleteUserGroup(const std::string &userGroupId) {
  auto dbClient = drogon::app().getDbClient();
  auto mp = CoroMapper<UserGroups>(dbClient);

  dto::BaseApiResponse response;
  try {
    auto count = co_await mp.deleteBy(
        Criteria(UserGroups::Cols::_id, CompareOperator::EQ, userGroupId));

    if (count > 0) {
      response.success = true;
      response.message = "User group deleted successfully";
    } else {
      response.success = false;
      response.message = "User group not found";
      response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    }

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.message = "Failed to delete user group";
    response.error["detail"] = e.base().what();
  }
  co_return response;
}

void UserService::checkAccountStatus(
    const std::string &identifier, const std::string &identifierType,
    const std::function<void(const gnp::dto::BaseApiResponse &)> &callback) {

  auto dbClient = drogon::app().getDbClient();
  Mapper<Users> mp(dbClient);
  Criteria criteria;

  if (identifierType == "email") {
    criteria = Criteria(Users::Cols::_email, CompareOperator::EQ, identifier);
  } else if (identifierType == "username") {
    criteria =
        Criteria(Users::Cols::_username, CompareOperator::EQ, identifier);
  } else if (identifierType == "phone") {
    criteria =
        Criteria(Users::Cols::_phone_number, CompareOperator::EQ, identifier);
  } else {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.message = "Invalid identifier type. Must be 'email' or 'username' "
                       "or 'phone number'.";
    callback(response);
    return;
  }

  mp.findOne(
      criteria,
      [=](const Users &user) {
        gnp::dto::BaseApiResponse response;
        response.success = true;
        response.message = "User account status retrieved";

        bool hasPassword = !user.getValueOfPasswordHash().empty();
        response.result["hasPassword"] = hasPassword;

        if (identifierType == "email") {
          bool hasUsername = !user.getValueOfUsername().empty();
          response.result["hasUsername"] = hasUsername;
        }

        callback(response);
      },
      [=](const DrogonDbException &e) {
        gnp::dto::BaseApiResponse response;

        response.success = false;
        response.message = "User not found";
        response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
        callback(response);
      });
}

void UserService::sendOtp(
    const std::string &userEmail,
    const std::function<void(const gnp::dto::BaseApiResponse &)> &callback) {

  auto dbClient = drogon::app().getDbClient();
  Mapper<Users> mp(dbClient);

  // 1. Check if user exists
  mp.findOne(
      Criteria(Users::Cols::_email, CompareOperator::EQ, userEmail),
      [=](const Users &user) {
        // User exists
        // 2. Generate UUID (RequestId) and OTP
        std::string requestId = drogon::utils::getUuid();

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> distrib(100000, 999999);
        std::string otp = std::to_string(distrib(gen));

        // 3. Store in Redis
        auto redisClient = drogon::app().getRedisClient();
        // Key: requestId, Value: otp, Expiry: 300 seconds
        redisClient->execCommandAsync(
            [=](const drogon::nosql::RedisResult &r) {
              if (r.type() == drogon::nosql::RedisResultType::kNil) {
                // Handle redis error if needed, but SETEX usually returns OK
              }

              // 4. Send Email
              // HTML Body with Red Theme
              std::string emailBody =
                  R"(
                  <!DOCTYPE html>
                  <html>
                  <head>
                  <style>
                    body { font-family: 'Helvetica Neue', Helvetica, Arial, sans-serif; background-color: #f9f9f9; margin: 0; padding: 0; }
                    .container { max-width: 600px; margin: 40px auto; background-color: #ffffff; border-radius: 10px; overflow: hidden; box-shadow: 0 4px 12px rgba(0,0,0,0.05); }
                    .header { background-color: #ffffff; padding: 30px 20px 20px; text-align: center; border-bottom: 1px solid #f0f0f0; }
                    .header img { height: 60px; margin-bottom: 15px; border-radius: 8px; }
                    .header h1 { margin: 0; font-size: 24px; font-weight: 600; color: #1a1a1a; }
                    .content { padding: 40px 30px; text-align: center; color: #4a4a4a; line-height: 1.6; }
                    .content p { margin: 0 0 15px; font-size: 16px; }
                    .otp-container { margin: 30px 0; padding: 20px; background-color: #fdf2f2; border-radius: 8px; border: 1px dashed #f5c6c6; }
                    .otp { font-size: 36px; font-weight: bold; color: #D32F2F; letter-spacing: 8px; margin: 0; }
                    .footer { background-color: #f9f9f9; color: #999999; padding: 20px; text-align: center; font-size: 13px; border-top: 1px solid #eeeeee; }
                  </style>
                  </head>
                  <body>
                  <div class="container">
                    <div class="header">
                      <img src="https://dev.graphicnewsplus.com/favicon-mag.png" alt="Graphic News Plus Logo">
                      <h1>Graphic News Plus</h1>
                    </div>
                    <div class="content">
                      <p>Hello Reader,</p>
                      <p>Your One-Time Password (OTP) for verification is:</p>
                      <div class="otp-container">
                        <div class="otp">)" +
                  otp + R"(</div>
                      </div>
                      <p>This code will expire in 5 minutes.</p>
                      <p>If you did not request this code, please ignore this email.</p>
                    </div>
                    <div class="footer"> &copy; )" +
                  trantor::Date::now().toCustomFormattedString("%Y") +
                  R"( Graphic News Plus. All rights reserved.
                    </div>
                  </div>
                  </body>
                  </html>
                  )";

              auto emailService =
                  std::make_shared<gnp::services::EmailService>();
              gnp::dto::SendEmailDto dto;
              dto.setTo(userEmail);
              dto.setSubject("Your OTP for Graphic News Plus");
              dto.setBody(emailBody);

              // emailService->sendEmail(dto, [=](const
              // gnp::dto::BaseApiResponse &emailResponse) {
              //       gnp::dto::BaseApiResponse response;
              //
              //       if (emailResponse.success) {
              //         response.success = true;
              //         response.message = "OTP sent successfully";
              //         Json::Value result;
              //         result["requestId"] = requestId;
              //         result["expiry"] = 300;
              //         result["email"] = userEmail;
              //         response.result = result;
              //
              //       } else {
              //         response.success = false;
              //         response.message = "Failed to send OTP email";
              //         response.error = emailResponse.error;
              //       }
              //       callback(response);
              //     });
            },
            [=](const std::exception &e) {
              gnp::dto::BaseApiResponse response;
              response.success = false;
              response.message = "Redis error";
              response.error["details"] = e.what();
              callback(response);
            },
            "SETEX %s %d %s", requestId.c_str(), 300, otp.c_str());
      },
      [=](const DrogonDbException &e) {
        gnp::dto::BaseApiResponse response;
        response.success = false;
        response.message = "User not found";
        response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
        callback(response);
      });
}

void UserService::verifyOtp(
    const std::string &userEmail, const std::string &otp,
    const std::string &requestId,
    const std::function<void(const gnp::dto::BaseApiResponse &)> &callback) {

  auto redisClient = drogon::app().getRedisClient();

  redisClient->execCommandAsync(
      [=](const drogon::nosql::RedisResult &r) {
        gnp::dto::BaseApiResponse response;
        if (r.type() == drogon::nosql::RedisResultType::kNil) {
          response.success = false;
          response.message = "OTP expired or invalid request ID";
          callback(response);
          return;
        }

        std::string storedOtp = r.asString();
        if (storedOtp == otp) {
          // OTP matches, now get the userId
          auto dbClient = drogon::app().getDbClient();
          Mapper<Users> mp(dbClient);

          mp.findOne(
              Criteria(Users::Cols::_email, CompareOperator::EQ, userEmail),
              [=](const Users &user) {
                // Generate Session ID
                std::string sessionId = drogon::utils::getUuid();
                std::string userId = user.getValueOfId();

                // Store Session ID in Redis (10 mins expiration)
                auto redisClient = drogon::app().getRedisClient();
                redisClient->execCommandAsync(
                    [=](const drogon::nosql::RedisResult &r) {
                      gnp::dto::BaseApiResponse response;
                      response.success = true;
                      response.result["isValid"] = true;
                      response.result["sessionId"] = sessionId;
                      response.message = "OTP verified successfully";

                      // Delete the OTP request ID
                      redisClient->execCommandAsync(
                          [](const drogon::nosql::RedisResult &) {},
                          [](const std::exception &) {}, "DEL %s",
                          requestId.c_str());

                      callback(response);
                    },
                    [=](const std::exception &e) {
                      gnp::dto::BaseApiResponse response;
                      response.success = false;
                      response.message = "Redis error during session creation";
                      response.error["details"] = e.what();
                      callback(response);
                    },
                    "SETEX %s %d %s", sessionId.c_str(), 600, userId.c_str());
              },
              [=](const DrogonDbException &e) {
                gnp::dto::BaseApiResponse response;
                response.success = false;
                response.message = "User not found associated with this email";
                callback(response);
              });
        } else {
          response.success = false;
          response.message = "Invalid OTP";
          callback(response);
        }
      },
      [=](const std::exception &e) {
        gnp::dto::BaseApiResponse response;
        response.success = false;
        response.message = "Redis error";
        response.error["details"] = e.what();
        callback(response);
      },
      "GET %s", requestId.c_str());
}

void UserService::setPassword(
    const std::string &sessionId, const std::string &password,
    const std::string &confirmPassword,
    const std::function<void(const gnp::dto::BaseApiResponse &)> &callback) {

  if (password != confirmPassword) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.message = "Passwords do not match";
    callback(response);
    return;
  }

  auto redisClient = drogon::app().getRedisClient();

  // Validate Session ID
  redisClient->execCommandAsync(
      [=](const drogon::nosql::RedisResult &r) {
        if (r.type() == drogon::nosql::RedisResultType::kNil) {
          gnp::dto::BaseApiResponse response;
          response.success = false;
          response.message =
              "Invalid or expired session. Please verify OTP again.";
          callback(response);
          return;
        }

        std::string userId = r.asString();

        // Delete Session ID (Single Attempt)
        redisClient->execCommandAsync([](const drogon::nosql::RedisResult &) {},
                                      [](const std::exception &) {}, "DEL %s",
                                      sessionId.c_str());

        // Proceed to set password
        std::string passwordHash = bcrypt::generateHash(password);

        auto dbClient = drogon::app().getDbClient();
        Mapper<Users> mp(dbClient);

        mp.findOne(
            Criteria(Users::Cols::_id, CompareOperator::EQ, userId),
            [=](Users user) {
              user.setPasswordHash(passwordHash);

              Mapper<Users> updateMp(dbClient);
              updateMp.update(
                  user,
                  [callback](const size_t count) {
                    gnp::dto::BaseApiResponse response;
                    if (count == 0) {
                      response.success = false;
                      response.message =
                          "User not found or password not updated";
                    } else {
                      response.success = true;
                      response.message = "Password set successfully";
                    }
                    callback(response);
                  },
                  [=](const DrogonDbException &e) {
                    gnp::dto::BaseApiResponse response;
                    response.success = false;
                    response.message = "Database error";
                    response.error["details"] = e.base().what();
                    callback(response);
                  });
            },
            [=](const DrogonDbException &e) {
              gnp::dto::BaseApiResponse response;
              response.success = false;
              response.message = "User not found";
              response.error["details"] = e.base().what();
              callback(response);
            });
      },
      [=](const std::exception &e) {
        gnp::dto::BaseApiResponse response;
        response.success = false;
        response.message = "Redis error";
        response.error["details"] = e.what();
        callback(response);
      },
      "GET %s", sessionId.c_str());
}

drogon::Task<gnp::dto::BaseApiResponse>
UserService::registerProspectiveUser(const dto::UserDto &userDto) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mp(dbClient);
  dto::BaseApiResponse response;

  bool userExists = false;

  try {
    // 1. Check if user exists
    co_await mp.findOne(
        Criteria(Users::Cols::_email, CompareOperator::EQ, userDto.getEmail()));

    // If findOne succeeds, user exists
    userExists = true;

  } catch (const DrogonDbException &e) {
    // User not found, proceeds to creation
    userExists = false;
  } catch (const std::exception &e) {
    response.success = false;
    response.message = "An unexpected error occurred during user lookup.";
    response.error["detail"] = e.what();
    co_return response;
  }

  if (userExists) {
    response.success = false;
    response.message = "User with this email already exists.";
    co_return response;
  }

  // 2. Create new user
  try {
    Users newUser;
    newUser.setFirstName(userDto.getFirstName());
    newUser.setLastName(userDto.getLastName());
    newUser.setEmail(userDto.getEmail());
    newUser.setPhoneNumber(userDto.getPhoneNumber());
    newUser.setIsActive(true);
    newUser.setIsLockedOut(false);
    newUser.setCreatedAt(trantor::Date::now());
    newUser.setUsernameToNull();
    newUser.setPasswordHashToNull();

    auto createdUser = co_await mp.insert(newUser);
    response.success = true;
    response.message = "Prospective user registered successfully.";
    response.result["id"] = createdUser.getValueOfId();

  } catch (const DrogonDbException &insertErr) {
    response.success = false;
    response.message = "Database error while creating prospective user.";
    response.error["detail"] = insertErr.base().what();
  } catch (const std::exception &e) {
    response.success = false;
    response.message = "An unexpected error occurred during creation.";
    response.error["detail"] = e.what();
  }

  co_return response;
}

} // namespace gnp::services
