//
// Created by Emmanuel Addo-Odame on 06/01/2026.
//

#include "CommercialPartnerService.h"
#include "CommercialPartnerApiKeys.h"
#include "CommercialPartners.h"
#include "Newspapers.h"
#include "PublicationReads.h"
#include "SubscriptionPlans.h"
#include "SubscriptionRenewalHistory.h"
#include "UserSessions.h"
#include "UserSubscriptions.h"
#include "Users.h"
#include "bcrypt.h"
#include "constants/ErrorCodes.h"
#include "dto/BaseApiResponse.h"
#include "dto/GeneratePartnerApiKeyDto.h"
#include "dto/SendEmailDto.h"
#include "utils/StringUtils.h"
#include "services/reports/ReportInstrumentation.h"
#include "plugins/GnpServicePlugin.h"
#include "services/email/EmailService.h"
#include "utils/CsvParser.h"
#include "utils/IdGeneratorUtils.h"
#include "utils/PasswordUtils.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <drogon/orm/CoroMapper.h>
#include <drogon/utils/Utilities.h>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "dto/ReportDto.h"

using namespace drogon::orm;

using ::drogon_model::Gnp::CommercialPartners;
using ::drogon_model::Gnp::PublicationReads;
using ::drogon_model::Gnp::SubscriptionPlans;
using ::drogon_model::Gnp::Users;
using ::drogon_model::Gnp::UserSessions;
using ::drogon_model::Gnp::UserSubscriptions;

namespace gnp::services {


drogon::Task<gnp::dto::BaseApiResponse> CommercialPartnerService::getAll(int pageNo, int pageSize, const std::string &query) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<CommercialPartners> mp(dbClient);

  // 1. Build the search criteria
  Criteria searchCriteria;
  if (!query.empty()) {
    std::string likeQuery = "%" + query + "%";

    searchCriteria = Criteria(CommercialPartners::Cols::_name,
                              CompareOperator::Like, likeQuery) ||
                     Criteria(CommercialPartners::Cols::_contact_name,
                              CompareOperator::Like, likeQuery) ||
                     Criteria(CommercialPartners::Cols::_contact_email,
                              CompareOperator::Like, likeQuery);
  }

  try {
    size_t totalCount = co_await mp.count(searchCriteria);
    if (totalCount == 0) {
      dto::BaseApiResponse response;
      response.success = true;
      response.result["data"] = Json::arrayValue;
      response.result["totalCount"] = 0;
      co_return response;
    }

    // 3. Find the paginated data
    int offset = (pageNo - 1) * pageSize;
    auto commercialPartners =
        co_await mp.limit(pageSize).offset(offset).findBy(searchCriteria);

    // 4. Build the final response
    gnp::dto::BaseApiResponse response;
    auto totalPages = (totalCount + pageSize - 1) / pageSize;

    response.success = true;
    response.result["totalCount"] = (Json::UInt64)totalCount;
    response.result["pageNo"] = pageNo;
    response.result["pageSize"] = pageSize;
    response.result["lowerBound"] = pageSize * (pageNo - 1) + 1;
    response.result["upperBound"] = Json::Value(
        (int)totalPages == pageNo ? (Json::UInt64)totalCount : (Json::UInt64)(pageNo * pageSize));
    response.result["totalPages"] = (int)totalPages;

    Json::Value data = Json::arrayValue;

    for (const auto &commercialPartner : commercialPartners) {
      Json::Value commercialPartnerJson = commercialPartner.toJson();

      // Convert snake_case to camelCase
      Json::Value camelCaseCommercialPartner;

      camelCaseCommercialPartner["id"] = commercialPartnerJson["id"];
      camelCaseCommercialPartner["partnerIdentifier"] =
          commercialPartnerJson["identifier"];
      camelCaseCommercialPartner["name"] = commercialPartnerJson["name"];
      camelCaseCommercialPartner["contactName"] = commercialPartnerJson["contact_name"];
      camelCaseCommercialPartner["contactEmail"] = commercialPartnerJson["contact_email"];
      camelCaseCommercialPartner["contactPhone"] = commercialPartnerJson["contact_phone"];
      camelCaseCommercialPartner["billingEmail"] = commercialPartnerJson["billing_email"];
      camelCaseCommercialPartner["billingCycle"] = commercialPartnerJson["billing_cycle"];
      camelCaseCommercialPartner["currency"] = commercialPartnerJson["currency"];
      camelCaseCommercialPartner["status"] = commercialPartnerJson["status"];
      camelCaseCommercialPartner["accountType"] = commercialPartnerJson["account_type"];
      camelCaseCommercialPartner["subAccountEnabled"] = commercialPartnerJson["sub_account_enabled"];
      camelCaseCommercialPartner["subscriberQuota"] = commercialPartnerJson["subscriber_quota"];
      camelCaseCommercialPartner["createdAt"] = commercialPartnerJson["created_at"];

      data.append(camelCaseCommercialPartner);
    }

    response.result["data"] = data;
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] =
        "Database error while fetching commercial partners.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}



drogon::Task<dto::BaseApiResponse> CommercialPartnerService::getAllSubscribers(int pageNo, int pageSize,
                                            const std::string &query,
                                            const std::string &partnerId) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mp(dbClient);

  // 1. Build the search criteria
  Criteria searchCriteria =
      Criteria(Users::Cols::_partner_id, CompareOperator::EQ, partnerId);

  if (!query.empty()) {
    std::string likeQuery = "%" + query + "%";

    searchCriteria =
        searchCriteria &&
        (Criteria(Users::Cols::_first_name, CompareOperator::Like, likeQuery) ||
         Criteria(Users::Cols::_last_name, CompareOperator::Like, likeQuery) ||
         Criteria(Users::Cols::_email, CompareOperator::Like, likeQuery) ||
         Criteria(Users::Cols::_username, CompareOperator::Like, likeQuery) ||
         Criteria(Users::Cols::_phone_number, CompareOperator::Like,
                  likeQuery));
  }

  try {
    size_t totalCount = co_await mp.count(searchCriteria);
    if (totalCount == 0) {
      gnp::dto::BaseApiResponse response;
      response.success = true;
      response.result["data"] = Json::arrayValue;
      response.result["totalCount"] = 0;
      co_return response;
    }

    // 3. Find the paginated data
    int offset = (pageNo - 1) * pageSize;
    auto users = co_await mp.limit(pageSize)
                     .offset(offset)
                     .orderBy(Users::Cols::_created_at, SortOrder::DESC)
                     .findBy(searchCriteria);

    std::vector<std::string> userIds;
    for (const auto &user : users) {
      userIds.push_back(user.getValueOfId());
    }

    CoroMapper<UserSubscriptions> subMapper(dbClient);
    std::vector<UserSubscriptions> subs;
    if (!userIds.empty()) {
      subs = co_await subMapper.findBy(
          Criteria(UserSubscriptions::Cols::_user_id, CompareOperator::In,
                   userIds) &&
          Criteria(UserSubscriptions::Cols::_is_active, CompareOperator::EQ,
                   true));
    }

    // 4. Build the final response
    gnp::dto::BaseApiResponse response;
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

    for (const auto &user : users) {

      Json::Value userJson = user.toJson();
      Json::Value camelCaseRole;
      camelCaseRole["id"] = userJson["id"];
      camelCaseRole["firstName"] = userJson["first_name"];
      camelCaseRole["lastName"] = userJson["last_name"];
      camelCaseRole["email"] = userJson["email"];
      camelCaseRole["phoneNumber"] = userJson["phone_number"];
      camelCaseRole["profileImageUrl"] = userJson["profile_image_url"];
      camelCaseRole["isActive"] = userJson["is_active"];
      camelCaseRole["lastActive"] = userJson["last_active"];

      auto subIt = std::find_if(
          subs.begin(), subs.end(), [&](const UserSubscriptions &s) {
            return s.getValueOfUserId() == user.getValueOfId();
          });

      if (subIt != subs.end()) {
        camelCaseRole["activatedOn"] =
            subIt->getValueOfStartDate().toDbString();
        camelCaseRole["validUntil"] = subIt->getValueOfEndDate().toDbString();
      } else {
        camelCaseRole["activatedOn"] = Json::nullValue;
        camelCaseRole["validUntil"] = Json::nullValue;
      }

      data.append(camelCaseRole);
    }
    response.result["data"] = data;

    co_return response;

  } catch (const DrogonDbException &e) {
    gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] =
        "Database error while fetching subscribers.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<dto::BaseApiResponse> CommercialPartnerService::getPartnerDetails(const std::string &partnerId) {

  auto dbClient = drogon::app().getDbClient();

  CoroMapper<drogon_model::Gnp::CommercialPartners> partnerMapper(dbClient);

  try {
    auto commercialPartner = co_await partnerMapper.findByPrimaryKey(partnerId);

    ::gnp::dto::BaseApiResponse response;
    response.success = true;

    Json::Value commercialPartnerJson = commercialPartner.toJson();

    // Convert snake_case to camelCase
    Json::Value camelCaseCommercialPartner;

    camelCaseCommercialPartner["partnerIdentifier"] =
        commercialPartnerJson["identifier"];
    camelCaseCommercialPartner["name"] = commercialPartnerJson["name"];

    camelCaseCommercialPartner["billingEmail"] =
        commercialPartnerJson["billing_email"];
    camelCaseCommercialPartner["contactPhone"] =
        commercialPartnerJson["contact_phone"];
    camelCaseCommercialPartner["requireTwoFactorAuth"] =
        commercialPartnerJson["require_two_factor_auth"];

    auto logoBytes = commercialPartner.getValueOfOrganizationLogo();
    if (!logoBytes.empty()) {
      camelCaseCommercialPartner["organizationLogo"] =
          drogon::utils::base64Encode((const unsigned char *)logoBytes.data(),
                                      logoBytes.size());
    } else {
      camelCaseCommercialPartner["organizationLogo"] = Json::nullValue;
    }

    response.result = camelCaseCommercialPartner;
    co_return response;

  } catch (const drogon::orm::DrogonDbException &e) {
    ::gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Commercial Partner not found";
    errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<dto::BaseApiResponse> CommercialPartnerService::createPartner(const dto::CreatePartnerDto &dto) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<CommercialPartners> mp(dbClient);

  try {
    CommercialPartners newPartner;

    newPartner.setName(dto.getName());
    newPartner.setContactName(dto.getContactName());

    newPartner.setContactEmail(utils::StringUtils::trim(dto.getContactEmail()));

    newPartner.setContactPhone(dto.getContactPhone());

    newPartner.setBillingEmail(utils::StringUtils::trim(dto.getBillingEmail()));

    newPartner.setDefaultSubscriptionPlanId(dto.getDefaultSubscriptionPlanId());
    newPartner.setDefaultSubscriptionPlanDescription(dto.getDefaultSubscriptionPlanName());
    newPartner.setCurrency(dto.getCurrency());
    char unitPriceBuf[64];
    snprintf(unitPriceBuf, sizeof(unitPriceBuf), "%.2f", dto.getPartnerInvoice().getUnitPrice());
    newPartner.setCostPerHead(unitPriceBuf);
    newPartner.setSubscriberQuota(dto.getSubscriberQuota());
    newPartner.setRemainingQuota(dto.getSubscriberQuota());
    newPartner.setStatus("Active");
    newPartner.setCurrentInvoiceNo(dto.getPartnerInvoice().getInvoiceNumber());
    char totalAmountDueBuf[64];
    snprintf(totalAmountDueBuf, sizeof(totalAmountDueBuf), "%.2f", dto.getPartnerInvoice().getInvoiceAmount());
    newPartner.setTotalAmountDue(totalAmountDueBuf);
    newPartner.setSubAccountEnabled(dto.getSubAccountEnabled());
    newPartner.setSubscriptionStartDate(dto.getSubscriptionStartDate());
    newPartner.setSubscriptionEndDate(dto.getSubscriptionEndDate());

    newPartner.setCreatedAt(trantor::Date::now());

    auto commercialPartner = co_await mp.insert(newPartner);

    // Generate random 8-character password
    std::string password = gnp::utils::PasswordUtils::generateRandomPassword(8);

    // Create admin user for the partner
    CoroMapper<Users> userMapper(dbClient);

    Users newUser;
    newUser.setEmail(dto.getContactEmail());
    newUser.setUsername(dto.getContactEmail());
    // split contact name into first name and lastname by the first space

    newUser.setFirstName(dto.getContactName());
    newUser.setLastName("");
    newUser.setPhoneNumber(dto.getContactPhone());
    newUser.setPasswordHash(bcrypt::generateHash(password));
    newUser.setIsPartnerAdminUser(true);
    newUser.setPartnerId(commercialPartner.getValueOfId());
    newUser.setIsActive(true);
    newUser.setIsLockedOut(false);
    newUser.setIsAdminUser(false);
    newUser.setCreatedAt(trantor::Date::now());

    auto user = co_await userMapper.insert(newUser);

    // Send email with credentials
    auto plugin = drogon::app().getPlugin<plugins::GnpServicePlugin>();
    auto &emailService = plugin->getEmailService();

    dto::SendEmailDto emailDto;
    emailDto.setTo(dto.getContactEmail());
    emailDto.setSubject("Graphic News Plus Account Details");

    std::string emailBody =
        R"html(
              <!DOCTYPE html>
              <html>
              <head>
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
                  <p>Hello )html" +
        dto.getContactName() + R"html(,</p>
                  <p>Welcome to Graphic News Plus! Your administrator account has been created successfully.</p>
                  <p>Below are your login credentials:</p>
                  <div class="credentials">
                    <div class="credential-item">
                      <div class="credential-label">Username (Email):</div>
                      <div class="credential-value">)html" +
        dto.getContactEmail() + R"html(</div>
                    </div>
                    <div class="credential-item">
                      <div class="credential-label">Password:</div>
                      <div class="credential-value">)html" +
        password + R"html(</div>
                    </div>
                  </div>
                  <p>Please keep these credentials secure and change your password after your first login.</p>
                  <p>You're all set! Log in now to explore more engaging content made just for you.</p>
                </div>
                <div class="footer">
                  &copy; )html" +
        trantor::Date::now().toCustomFormattedString("%Y") +
        R"html( Graphic News Plus. All rights reserved.
                </div>
              </div>
              </body>
              </html>
            )html";

    emailDto.setBody(emailBody);

    co_await emailService.sendEmailAsync(emailDto);

    // create invoice
    dto::PartnerInvoiceDto partnerInvoiceDto;
    partnerInvoiceDto.setInvoiceNumber(dto.getPartnerInvoice().getInvoiceNumber());
    partnerInvoiceDto.setPartnerId(commercialPartner.getValueOfId());
    partnerInvoiceDto.setPartnerName(dto.getName());
    partnerInvoiceDto.setPartnerEmail(dto.getBillingEmail());

    partnerInvoiceDto.setPartnerEmail(utils::StringUtils::trim(dto.getBillingEmail()));

    partnerInvoiceDto.setBalance(dto.getPartnerInvoice().getBalance());
    partnerInvoiceDto.setInvoiceAmount(dto.getPartnerInvoice().getInvoiceAmount());
    partnerInvoiceDto.setBillingCycle(dto.getPartnerInvoice().getBillingCycle());
    partnerInvoiceDto.setCurrency(dto.getPartnerInvoice().getCurrency());
    partnerInvoiceDto.setDescription(dto.getPartnerInvoice().getDescription());
    partnerInvoiceDto.setDueDate(dto.getSubscriptionEndDate());
    partnerInvoiceDto.setStatus(dto.getPartnerInvoice().getStatus());
    partnerInvoiceDto.setUnitPrice(dto.getPartnerInvoice().getUnitPrice());

    auto &partnerInvoiceService = plugin->getPartnerInvoiceService();

    co_await partnerInvoiceService.createInvoice(partnerInvoiceDto);

    // Send onboarding email to billing email
    dto::SendEmailDto onboardingEmailDto;
    onboardingEmailDto.setTo(utils::StringUtils::trim(dto.getBillingEmail()));
    onboardingEmailDto.setSubject("Welcome to Graphic Partner Platform - Onboarding & Invoice");

    std::ostringstream amountStream;
    amountStream << std::fixed << std::setprecision(2)
                 << dto.getPartnerInvoice().getInvoiceAmount();
    std::string formattedAmount = amountStream.str();

    // Add thousands separators for better readability
    size_t dotPos = formattedAmount.find('.');
    int pos = (dotPos == std::string::npos) ? (int)formattedAmount.length()
                                            : (int)dotPos;
    for (int i = pos - 3; i > 0; i -= 3) {
      formattedAmount.insert(i, ",");
    }

    std::string dueDateStr = dto.getSubscriptionEndDate().toCustomFormattedString("%d-%b-%Y");

    std::string onboardingEmailBody =
        R"html(
              <!DOCTYPE html>
              <html>
              <head>
              <style>
                body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: #f8f9fa; margin: 0; padding: 0; }
                .container { max-width: 600px; margin: 40px auto; background-color: #ffffff; border-radius: 12px; overflow: hidden; box-shadow: 0 10px 30px rgba(0,0,0,0.05); }
                .header { background-color: #D32F2F; color: #ffffff; padding: 40px 20px; text-align: center; }
                .header h1 { margin: 0; font-size: 28px; font-weight: 600; letter-spacing: 1px; }
                .content { padding: 40px; color: #444444; line-height: 1.6; }
                .welcome-text { font-size: 18px; margin-bottom: 20px; color: #222222; }
                .invoice-card { background-color: #ffffff; border: 1px solid #e0e0e0; border-radius: 12px; margin: 30px 0; padding: 0; overflow: hidden; box-shadow: 0 4px 12px rgba(0,0,0,0.08); }
                .invoice-header { background: linear-gradient(135deg, #fdfdfd 0%, #f5f5f5 100%); padding: 20px 25px; border-bottom: 1px solid #e0e0e0; }
                .invoice-title { font-weight: 700; color: #D32F2F; font-size: 14px; margin: 0; text-transform: uppercase; letter-spacing: 1.2px; }
                .invoice-body { padding: 30px; }
                .invoice-row { margin: 0 0 18px 0; display: flex; justify-content: space-between; align-items: center; }
                .invoice-row:last-of-type { margin-bottom: 0; }
                .label { color: #888; font-weight: 500; font-size: 14px; }
                .value { color: #333; font-weight: 600; font-size: 14px; text-align: right; }
                .total-row { margin-top: 25px; padding-top: 20px; border-top: 2px solid #f0f0f0; }
                .total-label { font-size: 20px; font-weight: 700; color: #222; }
                .total-value { font-size: 20px; font-weight: 700; color: #D32F2F; text-align: right; }
                .footer { background-color: #f8f9fa; color: #999999; padding: 20px; text-align: center; font-size: 13px; border-top: 1px solid #eeeeee; }
                .btn { display: inline-block; background-color: #D32F2F; color: #ffffff !important; padding: 12px 30px; border-radius: 6px; text-decoration: none; font-weight: bold; margin-top: 20px; }
              </style>
              </head>
              <body>
              <div class="container">
                <div class="header">
                  <h1>Graphic Partner Platform</h1>
                </div>
                <div class="content">
                  <p class="welcome-text">Congratulations, )html" +
        dto.getName() + R"html(!</p>
                  <p>We are thrilled to welcome you to the Graphic Partner Platform. Your organization has been successfully onboarded, and you now have access to our premium suite of tools and content distribution services.</p>

                  <p>As part of your subscription to the <strong>)html" +
        dto.getDefaultSubscriptionPlanName() +
        R"html(</strong>, a new invoice has been generated for your account:</p>

                  <div class="invoice-card">
                    <div class="invoice-header">
                      <div class="invoice-title">INVOICE SUMMARY</div>
                    </div>
                    <div class="invoice-body">
                      <div class="invoice-row">
                        <span class="label">Invoice Number:</span>
                        <span class="value">)html" +
        dto.getPartnerInvoice().getInvoiceNumber() + R"html(</span>
                      </div>
                      <div class="invoice-row">
                        <span class="label">Description:</span>
                        <span class="value">)html" +
        dto.getPartnerInvoice().getDescription() + R"html(</span>
                      </div>
                      <div class="invoice-row">
                        <span class="label">Billing Cycle:</span>
                        <span class="value">)html" +
        dto.getPartnerInvoice().getBillingCycle() + R"html(</span>
                      </div>
                      <div class="invoice-row">
                        <span class="label">Due Date:</span>
                        <span class="value">)html" +
        dueDateStr + R"html(</span>
                      </div>
                      <div class="invoice-row total-row">
                        <span class="total-label">Total Amount:</span>
                        <span class="total-value">)html" +
        dto.getPartnerInvoice().getCurrency() + " " + formattedAmount +
        R"html(</span>
                      </div>
                    </div>
                  </div>

                  <p>You can manage your subscriptions, view full invoices, and track your performance directly from your Partner Dashboard.</p>

                  <div style="text-align: center;">
                      <a href="https://dev.graphicnewsplus.com/partners/account/login" class="btn">Access Partner Dashboard</a>
                  </div>

                  <p style="margin-top: 30px;">If you have any questions regarding your invoice or the onboarding process, please don't hesitate to contact our support team.</p>
                </div>
                <div class="footer">
                  &copy; )html" + trantor::Date::now().toCustomFormattedString("%Y") +
        R"html( Graphic News Plus. All rights reserved.<br>
                  Providing premium content solutions for our partners.
                </div>
              </div>
              </body>
              </html>
            )html";

    onboardingEmailDto.setBody(onboardingEmailBody);
    co_await emailService.sendEmailAsync(onboardingEmailDto);

    // Prepare success response
    ::gnp::dto::BaseApiResponse successResponse;
    successResponse.success = true;
    successResponse.message = "Partner created successfully";
    successResponse.result["id"] = commercialPartner.getValueOfId();
    successResponse.result["adminUserId"] = user.getValueOfId();

    co_return successResponse;

  } catch (const DrogonDbException &e) {
    ::gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Database error while creating Partner";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<dto::BaseApiResponse> CommercialPartnerService::updatePartner(const dto::UpdatePartnerDto &dto) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<CommercialPartners> mp(dbClient);

  try {
    auto commercialPartner = co_await mp.findByPrimaryKey(dto.getId());

    commercialPartner.setName(dto.getName());
    commercialPartner.setContactName(dto.getContactName());
    commercialPartner.setContactEmail(utils::StringUtils::trim(dto.getContactEmail()));
    commercialPartner.setContactPhone(dto.getContactPhone());
    commercialPartner.setBillingEmail(utils::StringUtils::trim(dto.getBillingEmail()));
    commercialPartner.setCurrency(dto.getCurrency());
    commercialPartner.setSubscriberQuota(dto.getSubscriberQuota());
    commercialPartner.setSubAccountEnabled(dto.getSubAccountEnabled());
    commercialPartner.setSubscriptionStartDate(dto.getSubscriptionStartDate());
    commercialPartner.setSubscriptionEndDate(dto.getSubscriptionEndDate());

    co_await mp.update(commercialPartner);

    dto::BaseApiResponse response;
    response.success = true;
    response.message = "Commercial Partner updated successfully";
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to update Commercial Partner";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}


drogon::Task<dto::BaseApiResponse> CommercialPartnerService::createPartnerSubscriber(const dto::CreatePartnerSubscriberDto &dto) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mp(dbClient);

  // Trim email and format phone number first for consistent validation
  std::string email = utils::StringUtils::trim(dto.getEmail());
  std::string phoneNumber = dto.getPhoneNumber();

  // Format phone number
  if (phoneNumber.length() >= 3 && phoneNumber.substr(0, 3) == "233") {
    phoneNumber = "0" + phoneNumber.substr(3);
  } else if (phoneNumber.length() >= 4 && phoneNumber.substr(0, 4) == "+233") {
    phoneNumber = "0" + phoneNumber.substr(4);
  }

  try {
    // 1. Check if email already exists
    auto existingEmailUsers = co_await mp.findBy(
        Criteria(Users::Cols::_email, CompareOperator::EQ, email));

    if (!existingEmailUsers.empty()) {
      dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "A user with this email already exists";
      errorResponse.error["code"] = constants::ERR_DUPLICATE_EMAIL;
      errorResponse.error["field"] = "email";
      errorResponse.error["detail"] = "Email address is already registered";
      co_return errorResponse;
    }

    // 2. Check if phone number already exists
    auto existingPhoneUsers = co_await mp.findBy(
        Criteria(Users::Cols::_phone_number, CompareOperator::EQ, phoneNumber));

    if (!existingPhoneUsers.empty()) {
      dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "A user with this phone number already exists";
      errorResponse.error["code"] = constants::ERR_DUPLICATE_PHONE;
      errorResponse.error["field"] = "phoneNumber";
      errorResponse.error["detail"] = "Phone number is already registered";
      co_return errorResponse;
    }

    // 3. Validate partner exists and has available quota
    CoroMapper<CommercialPartners> partnerMapper(dbClient);
    auto partners = co_await partnerMapper.findBy(
        Criteria(CommercialPartners::Cols::_id, CompareOperator::EQ, dto.getPartnerId()));

    if (partners.empty()) {
      dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "Partner record not found";
      errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
      errorResponse.error["detail"] = "The specified partner does not exist";
      co_return errorResponse;
    }

    auto& partner = partners[0];

    if (partner.getValueOfRemainingQuota() <= 0) {
      dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "Partner has reached maximum subscriber quota";
      errorResponse.error["code"] = constants::ERR_QUOTA_EXCEEDED;
      errorResponse.error["detail"] = "No remaining subscriber slots available";
      co_return errorResponse;
    }

    // 4. Generate password and create user
    std::string password = utils::PasswordUtils::generateRandomPassword(8);

    Users newUser;
    newUser.setFirstName(dto.getFirstName());
    newUser.setLastName(dto.getLastName());
    newUser.setEmail(email);
    newUser.setUsername(email);
    newUser.setPhoneNumber(phoneNumber);
    newUser.setPartnerId(dto.getPartnerId());
    newUser.setCountry("GH");
    newUser.setPasswordHash(bcrypt::generateHash(password));
    newUser.setIsActive(true);
    newUser.setIsLockedOut(false);
    newUser.setCreatedAt(trantor::Date::now());

    auto user = co_await mp.insert(newUser);

    LOG_INFO << "[createPartnerSubscriber] Created subscriber — email: " << user.getValueOfEmail()
             << ", password: " << password;

    // 5. Fetch plan and newspapers for entitlements
    CoroMapper<SubscriptionPlans> planMapper(dbClient);

    auto plans = co_await planMapper.findBy(
        Criteria(SubscriptionPlans::Cols::_id, CompareOperator::EQ, partner.getValueOfDefaultSubscriptionPlanId()));

    if (plans.empty()) {
      dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "Default subscription plan not found";
      errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
      errorResponse.error["detail"] = "Partner has no valid subscription plan";
      co_return errorResponse;
    }

    auto& plan = plans[0];

    auto startDateObj = partner.getValueOfSubscriptionStartDate();
    auto endDateObj = partner.getValueOfSubscriptionEndDate();

    // 6. Get publications
    std::vector<std::string> pubIdsList;
    try {
      Json::Reader reader;
      Json::Value pubIdsJson;
      if (reader.parse(plan.getValueOfTargetPublications(), pubIdsJson) &&
          pubIdsJson.isArray()) {
        for (const auto &id : pubIdsJson) {
          pubIdsList.push_back(id.asString());
        }
      }
    } catch (...) {
      // Log warning but continue with empty publications
      LOG_WARN << "[createPartnerSubscriber] Failed to parse target publications for plan: "
               << plan.getValueOfId();
    }

    CoroMapper<drogon_model::Gnp::Newspapers> newsMapper(dbClient);
    std::vector<drogon_model::Gnp::Newspapers> newspapers;
    if (!pubIdsList.empty()) {
      newspapers = co_await newsMapper.findBy(
          Criteria(drogon_model::Gnp::Newspapers::Cols::_publication_id,
                   CompareOperator::In, pubIdsList) &&
          Criteria(drogon_model::Gnp::Newspapers::Cols::_publication_date,
                   CompareOperator::GE, startDateObj) &&
          Criteria(drogon_model::Gnp::Newspapers::Cols::_publication_date,
                   CompareOperator::LE, endDateObj));
    }

    // 7. Create subscription record
    CoroMapper<UserSubscriptions> subMapper(dbClient);
    UserSubscriptions newSub;
    newSub.setUserId(user.getValueOfId());
    newSub.setPartnerId(partner.getValueOfId());
    newSub.setSubscriptionPlanId(plan.getValueOfId());
    newSub.setSubscriptionPlanDescription(plan.getValueOfName());
    newSub.setEmail(user.getValueOfEmail());
    newSub.setStartDate(startDateObj);
    newSub.setEndDate(endDateObj);
    newSub.setIsActive(true);

    Json::Value newEntArray = Json::arrayValue;
    for (const auto &news : newspapers) {
      Json::Value ent;
      ent["id"] = news.getValueOfId();
      ent["uniqueId"] = utils::IdGeneratorUtils::generateAlphanumericId();
      newEntArray.append(ent);
    }
    Json::StreamWriterBuilder writerBuilder;
    newSub.setNewspaperEntitlements(Json::writeString(writerBuilder, newEntArray));
    newSub.setCreatedAt(trantor::Date::now());
    newSub.setSubscriptionIdentifier(utils::IdGeneratorUtils::generateRandomSixDigit());
    co_await subMapper.insert(newSub);

    // 8. Determine billing cycle
    auto determineBillingCycle = [](const trantor::Date &start,
                                    const trantor::Date &end) -> std::string {
      int64_t diffDays =
          (end.microSecondsSinceEpoch() - start.microSecondsSinceEpoch()) /
          (1000000LL * 3600 * 24);
      if (diffDays <= 7)
        return "Weekly";
      if (diffDays <= 31)
        return "Monthly";
      if (diffDays <= 92)
        return "Quarterly";
      if (diffDays <= 184)
        return "Half-Yearly";
      return "Annual";
    };

    // 9. Create renewal history record
    CoroMapper<drogon_model::Gnp::SubscriptionRenewalHistory> renewalMapper(dbClient);
    drogon_model::Gnp::SubscriptionRenewalHistory renewal;
    renewal.setSubscriptionIdentifier(newSub.getValueOfSubscriptionIdentifier());
    renewal.setUserId(user.getValueOfId());
    renewal.setUserName(user.getValueOfFirstName() + " " + user.getValueOfLastName());
    renewal.setEmail(user.getValueOfEmail());
    renewal.setSubscriptionPlanId(plan.getValueOfId());
    renewal.setSubscriptionPlanName(plan.getValueOfName());
    renewal.setPastBillingCycle("N/A");
    renewal.setCurrentBillingCycle(determineBillingCycle(startDateObj, endDateObj));
    renewal.setTransactionStatus("Successful");
    renewal.setPaidBy(partner.getValueOfName());
    renewal.setAmountPaid(partner.getValueOfCostPerHead());
    renewal.setCreatedAt(trantor::Date::now());
    co_await renewalMapper.insert(renewal);

    // 10. Send email with credentials
    auto plugin = drogon::app().getPlugin<plugins::GnpServicePlugin>();
    auto &emailService = plugin->getEmailService();

    dto::SendEmailDto emailDto;
    emailDto.setTo(email);
    emailDto.setSubject("Graphic News Plus Account Details");

    // Email body construction (same as original)
    std::string emailBody = R"(
    <!DOCTYPE html>
    <html>
    <head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
      body {
        margin: 0;
        padding: 0;
        background-color: #f4f6f8;
        font-family: 'Segoe UI', Arial, sans-serif;
      }

      .wrapper {
        width: 100%;
        padding: 30px 0;
      }

      .container {
        max-width: 600px;
        margin: 0 auto;
        background: #ffffff;
        border-radius: 10px;
        overflow: hidden;
        box-shadow: 0 8px 20px rgba(0,0,0,0.08);
      }

      .header {
        background: linear-gradient(135deg, #D32F2F, #9A0007);
        color: #ffffff;
        padding: 30px 20px;
        text-align: center;
      }

      .header h1 {
        margin: 0;
        font-size: 24px;
      }

      .content {
        padding: 30px;
        color: #333;
        line-height: 1.6;
      }

      .credentials {
        background: #f9fafb;
        border: 1px solid #eee;
        padding: 20px;
        border-radius: 8px;
        margin: 25px 0;
      }

      .credential-item {
        margin-bottom: 15px;
      }

      .credential-label {
        font-size: 13px;
        color: #888;
        margin-bottom: 5px;
      }

      .credential-value {
        font-size: 16px;
        font-weight: bold;
        color: #D32F2F;
        font-family: monospace;
        background: #fff;
        padding: 8px 10px;
        border-radius: 5px;
        border: 1px dashed #ddd;
      }

      .cta {
        text-align: center;
        margin: 30px 0 15px;
      }

      .btn {
        display: inline-block;
        padding: 12px 25px;
        background-color: #D32F2F;
        color: #ffffff;
        text-decoration: none;
        border-radius: 6px;
        font-weight: 600;
        font-size: 14px;
      }

      .btn:hover {
        background-color: #b71c1c;
      }

      .link {
        word-break: break-all;
        color: #D32F2F;
      }

      .footer {
        background: #f4f6f8;
        color: #888;
        text-align: center;
        padding: 20px;
        font-size: 12px;
      }

    </style>
    </head>

    <body>
    <div class="wrapper">
      <div class="container">

        <div class="header">
          <h1>Graphic News Plus</h1>
          <p>Your Premium News Experience</p>
        </div>

        <div class="content">
          <p>Hello )" + dto.getFirstName() +
                            R"(,</p>

          <p>
            Welcome to <strong>Graphic News Plus</strong>! Your corporate account has been successfully created.
          </p>

          <p>You can now access the platform here:</p>

          <p>
            <a href="https://new.graphicnewsplus.com?al=t" class="link">
              https://new.graphicnewsplus.com?al=t
            </a>
          </p>

          <p>Here are your login credentials:</p>

          <div class="credentials">
            <div class="credential-item">
              <div class="credential-label">Username (Email)</div>
              <div class="credential-value">)" +
                            email + R"(</div>
            </div>

            <div class="credential-item">
              <div class="credential-label">Password</div>
              <div class="credential-value">)" +
                            password + R"(</div>
            </div>
          </div>

          <p>Your subscription details:</p>

          <div class="credentials">
            <div class="credential-item">
              <div class="credential-label">Package</div>
              <div style="font-size: 16px; font-weight: bold; color: #333; background: #fff; padding: 8px 10px; border-radius: 5px; border: 1px solid #ddd;">)" +
                            plan.getValueOfName() + R"(</div>
            </div>

            <div class="credential-item">
              <div class="credential-label">Validity Period</div>
              <div style="font-size: 16px; font-weight: bold; color: #333; background: #fff; padding: 8px 10px; border-radius: 5px; border: 1px solid #ddd;">)" +
                            startDateObj.toCustomFormattedString("%d %b %Y") +
                            " to " +
                            endDateObj.toCustomFormattedString("%d %b %Y") +
                            R"(</div>
            </div>
          </div>

          <p>
            For security reasons, please change your password immediately after your first login.
          </p>

          <div class="cta">
            <a href="https://new.graphicnewsplus.com?al=t" style="color:#ffffff !important; text-decoration:none;" class="btn">
              Login to Your Account
            </a>
          </div>

          <p style="font-size:13px; color:#777;">
            If the button above doesn’t work, copy and paste this link into your browser:
            <br>
            <a href="https://new.graphicnewsplus.com?al=t" class="link">
              https://new.graphicnewsplus.com?al=t
            </a>
          </p>

          <p>
            Enjoy unlimited access to curated, high-quality news content tailored for you.
          </p>
        </div>

        <div class="footer">
          <p>&copy; )" + trantor::Date::now().toCustomFormattedString("%Y") +
                            R"( Graphic News Plus</p>
          <p>All rights reserved.</p>
        </div>

      </div>
    </div>
    </body>
    </html>
    )";

    emailDto.setBody(emailBody);
    co_await emailService.sendEmailAsync(emailDto);

    LOG_INFO << "[createPartnerSubscriber] Created subscriber — email: "
             << user.getValueOfEmail() << ", password: " << password;

    // 11. Reduce subscriber slots for commercial partner
    partner.setRemainingQuota(partner.getValueOfRemainingQuota() - 1);
    co_await partnerMapper.update(partner);

    // 12. Prepare success response
    dto::BaseApiResponse successResponse;
    successResponse.success = true;
    successResponse.message = "Subscriber created successfully";
    successResponse.result["id"] = user.getValueOfId();

    co_return successResponse;

  } catch (const drogon::orm::DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Database error while creating Subscriber";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}


drogon::Task<dto::BaseApiResponse> CommercialPartnerService::updatePartnerSubscriber(const dto::UpdatePartnerSubscriberDto &dto) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mp(dbClient);

  try {
    // 1. Find the user by ID and Partner ID to ensure ownership
    auto user = co_await mp.findOne(
        Criteria(Users::Cols::_id, CompareOperator::EQ, dto.getId()) &&
        Criteria(Users::Cols::_partner_id, CompareOperator::EQ,
                 dto.getPartnerId()));

    // 2. Trim and sanitize input values
    std::string newEmail = utils::StringUtils::trim(dto.getEmail());
    std::string newPhoneNumber = dto.getPhoneNumber();
    std::string newFirstName = utils::StringUtils::trim(dto.getFirstName());
    std::string newLastName = utils::StringUtils::trim(dto.getLastName());

    // Format phone number if it has country code
    if (newPhoneNumber.length() >= 3 && newPhoneNumber.substr(0, 3) == "233") {
      newPhoneNumber = "0" + newPhoneNumber.substr(3);
    } else if (newPhoneNumber.length() >= 4 && newPhoneNumber.substr(0, 4) == "+233") {
      newPhoneNumber = "0" + newPhoneNumber.substr(4);
    }

    // 3. Check if email is being changed and if it already exists (excluding current user)
    if (!newEmail.empty() && newEmail != user.getValueOfEmail()) {
      auto existingEmailUsers = co_await mp.findBy(Criteria(Users::Cols::_email, CompareOperator::EQ, newEmail) &&
          Criteria(Users::Cols::_id, CompareOperator::NE, dto.getId()));

      if (!existingEmailUsers.empty()) {
        dto::BaseApiResponse errorResponse;
        errorResponse.success = false;
        errorResponse.message = "A user with this email already exists";
        errorResponse.error["code"] = constants::ERR_DUPLICATE_EMAIL;
        errorResponse.error["field"] = "email";
        errorResponse.error["detail"] = "Email address is already registered by another user";
        co_return errorResponse;
      }
    }

    // 4. Check if phone number is being changed and if it already exists (excluding current user)
    if (!newPhoneNumber.empty() && newPhoneNumber != user.getValueOfPhoneNumber()) {
      auto existingPhoneUsers = co_await mp.findBy(
          Criteria(Users::Cols::_phone_number, CompareOperator::EQ, newPhoneNumber) &&
          Criteria(Users::Cols::_id, CompareOperator::NE, dto.getId()));

      if (!existingPhoneUsers.empty()) {
        dto::BaseApiResponse errorResponse;
        errorResponse.success = false;
        errorResponse.message = "A user with this phone number already exists";
        errorResponse.error["code"] = constants::ERR_DUPLICATE_PHONE;
        errorResponse.error["field"] = "phoneNumber";
        errorResponse.error["detail"] = "Phone number is already registered by another user";
        co_return errorResponse;
      }
    }

    // 5. Check if first name is provided
    if (!newFirstName.empty()) {
      user.setFirstName(newFirstName);
    }

    // 6. Check if last name is provided
    if (!newLastName.empty()) {
      user.setLastName(newLastName);
    }

    // 7. Update phone number if provided
    if (!newPhoneNumber.empty()) {
      user.setPhoneNumber(newPhoneNumber);
    }

    // 8. Update email and username if provided
    if (!newEmail.empty()) {
      user.setEmail(newEmail);
      user.setUsername(newEmail);
    }

    // 9. Save changes
    co_await mp.update(user);

    dto::BaseApiResponse response;
    response.success = true;
    response.message = "Partner subscriber info updated successfully";
    response.result["id"] = user.getValueOfId();
    response.result["email"] = user.getValueOfEmail();
    response.result["phoneNumber"] = user.getValueOfPhoneNumber();
    response.result["firstName"] = user.getValueOfFirstName();
    response.result["lastName"] = user.getValueOfLastName();

    co_return response;

  } catch (const drogon::orm::UnexpectedRows &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Subscriber not found or does not belong to this partner";
    errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    errorResponse.error["detail"] = e.what();
    co_return errorResponse;
  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to update partner subscriber";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}


drogon::Task<dto::BaseApiResponse> CommercialPartnerService::assignPartnerSubscribersToPlan(
    const dto::AssignPartnerSubscriberPlanDto &dto) {

  auto dbClient = drogon::app().getDbClient();

  try {
    // 1. Validate that the partner exists
    CoroMapper<CommercialPartners> partnerMapper(dbClient);
    co_await partnerMapper.findOne(Criteria(CommercialPartners::Cols::_id,
                                            CompareOperator::EQ,
                                            dto.getPartnerId()));

    // 2. Validate that the subscription plan exists
    CoroMapper<SubscriptionPlans> planMapper(dbClient);
    co_await planMapper.findOne(Criteria(SubscriptionPlans::Cols::_id,
                                         CompareOperator::EQ, dto.getPlanId()));

    // 3. Process each subscriber
    auto subscriberIds = dto.getSubscriberIds();
    int successCount = 0;
    int failureCount = 0;
    std::vector<std::string> failedUsers;

    if (subscriberIds.empty()) {
      dto::BaseApiResponse response;
      response.success = true;
      response.message = "No subscribers to assign";
      response.result["successCount"] = 0;
      response.result["failureCount"] = 0;
      co_return response;
    }

    CoroMapper<Users> userMapper(dbClient);
    CoroMapper<UserSubscriptions> subscriptionMapper(dbClient);

    for (const auto &userId : subscriberIds) {
      try {
        // Verify user exists and belongs to the partner
        Criteria userCriteria =
            Criteria(Users::Cols::_id, CompareOperator::EQ, userId) &&
            Criteria(Users::Cols::_partner_id, CompareOperator::EQ,
                     dto.getPartnerId());

        auto user = co_await userMapper.findOne(userCriteria);

        // Create subscription record
        UserSubscriptions subscription;
        subscription.setUserId(userId);
        subscription.setSubscriptionPlanDescription(
            dto.getSubscriptionPlanDescription());
        subscription.setEmail(user.getValueOfEmail());
        subscription.setIsActive(true);
        subscription.setCreatedAt(trantor::Date::now());
        subscription.setPartnerId(dto.getPartnerId());
        subscription.setBillingCycle(dto.getBillingCycle());
        subscription.setSubscriptionIdentifier(
            gnp::utils::IdGeneratorUtils::generateRandomSixDigit());
        subscription.setSubscriptionPlanId(dto.getPlanId());

        co_await subscriptionMapper.insert(subscription);
        successCount++;

      } catch (const DrogonDbException &) {
        failureCount++;
        failedUsers.push_back(userId);
      }
    }

    dto::BaseApiResponse response;
    response.success = (failureCount < subscriberIds.size());
    response.message = failureCount == 0
                           ? "Subscriber assignment completed"
                           : "Subscriber assignment completed with errors";
    response.result["successCount"] = successCount;
    response.result["failureCount"] = failureCount;

    if (failureCount > 0) {
      Json::Value failedArray = Json::arrayValue;
      for (const auto &failedUserId : failedUsers) {
        failedArray.append(failedUserId);
      }
      response.result["failedUsers"] = failedArray;
    }

    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to assign subscribers to plan";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<dto::BaseApiResponse> CommercialPartnerService::updatePartnerQuota(std::string partnerId,
                                             const dto::PartnerQuotaDto &dto) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<CommercialPartners> mp(dbClient);

  try {
    // 1. Validate that the partner exists
    auto partner = co_await mp.findOne(Criteria(
        CommercialPartners::Cols::_id, CompareOperator::EQ, partnerId));

    // 2. Calculate new quota delta and adjust remaining quota
    int currentTotalQuota = partner.getValueOfSubscriberQuota();
    int currentRemainingQuota = partner.getValueOfRemainingQuota();
    int newTotalQuota = dto.getQuota();

    int usedQuota = currentTotalQuota - currentRemainingQuota;

    if (newTotalQuota < usedQuota) {
      dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message =
          "New quota cannot be less than the already used quota";
      errorResponse.error["code"] = constants::ERR_UNSUPPORTED_OPERATION;
      co_return errorResponse;
    }

    int newRemainingQuota = newTotalQuota - usedQuota;

    // 3. Update partner record
    partner.setSubscriberQuota(newTotalQuota);
    partner.setRemainingQuota(newRemainingQuota);

    co_await mp.update(partner);

    // create a new invoice of the remaining deficit only if the quota was
    // increased
    int quotaDeficit = newTotalQuota - currentTotalQuota;
    if (quotaDeficit > 0) {
      double unitPrice = 0.0;
      try {
        unitPrice = std::stod(partner.getValueOfCostPerHead());
      } catch (...) {
      }

      if (unitPrice > 0.0) {
        double deficitAmount = quotaDeficit * unitPrice;

        dto::PartnerInvoiceDto partnerInvoiceDto;
        partnerInvoiceDto.setInvoiceNumber(
            "GNP-INV-" + utils::IdGeneratorUtils::generateAlphanumericId(8));
        partnerInvoiceDto.setPartnerId(partner.getValueOfId());
        partnerInvoiceDto.setPartnerName(partner.getValueOfName());
        partnerInvoiceDto.setPartnerEmail(partner.getValueOfBillingEmail());
        partnerInvoiceDto.setBalance(deficitAmount);
        partnerInvoiceDto.setBillingCycle("On-Demand");
        partnerInvoiceDto.setInvoiceAmount(deficitAmount);
        partnerInvoiceDto.setCurrency(partner.getValueOfCurrency());
        partnerInvoiceDto.setDescription("Additional subscriber quota invoice");
        partnerInvoiceDto.setDueDate(partner.getValueOfSubscriptionEndDate());
        partnerInvoiceDto.setStatus("Pending");
        partnerInvoiceDto.setUnitPrice(unitPrice);
        partnerInvoiceDto.setBillingCycle("N/A");

        auto plugin = drogon::app().getPlugin<plugins::GnpServicePlugin>();
        auto &partnerInvoiceService = plugin->getPartnerInvoiceService();

        co_await partnerInvoiceService.createInvoice(partnerInvoiceDto);
      }
    }

    dto::BaseApiResponse response;
    response.success = true;
    response.message = "Partner quota updated successfully";

    // Add updated quota info to response
    response.result["subscriberQuota"] = newTotalQuota;
    response.result["remainingQuota"] = newRemainingQuota;

    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to update partner quota";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<dto::BaseApiResponse> CommercialPartnerService::getPartnerSubscriptionSummary(const std::string &partnerId) {

  auto dbClient = drogon::app().getDbClient();

  std::string sql = "SELECT subscription_plan_description, COUNT(*) as "
                    "subscriber_count FROM user_subscriptions "
                    "WHERE partner_id = $1 GROUP BY "
                    "subscription_plan_description";

  try {
    auto result = co_await dbClient->execSqlCoro(sql, partnerId);

    dto::BaseApiResponse response;
    response.success = true;
    response.message = "Partner subscription summary fetched successfully";

    Json::Value data = Json::arrayValue;
    for (const auto &row : result) {
      Json::Value item;
      item["subscriptionPlanDescription"] =
          row["subscription_plan_description"].isNull()
              ? "No Description"
              : row["subscription_plan_description"].as<std::string>();
      item["subscriberCount"] = (Json::Int64)row["subscriber_count"].as<long>();

      data.append(item);
    }

    response.result = data;
    co_return response;

  } catch (const drogon::orm::DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to fetch partner subscription summary";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<dto::BaseApiResponse> CommercialPartnerService::updatePartnerLogo(const std::string &partnerId, const std::string &logoContent,
    std::optional<bool> requireTwoFactorAuth) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<CommercialPartners> mp(dbClient);

  try {
    auto commercialPartner = co_await mp.findOne(Criteria(
        CommercialPartners::Cols::_id, CompareOperator::EQ, partnerId));

    if (!logoContent.empty()) {
      commercialPartner.setOrganizationLogo(logoContent);
    }

    if (requireTwoFactorAuth.has_value()) {
      commercialPartner.setRequireTwoFactorAuth(requireTwoFactorAuth.value());
    }

    co_await mp.update(commercialPartner);

    dto::BaseApiResponse response;
    response.success = true;
    response.message = "Partner settings updated successfully";
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to update Partner settings";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<dto::BaseApiResponse> CommercialPartnerService::deletePartner(const std::string &partnerId) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<CommercialPartners> mp(dbClient);

  Criteria criteria =
      Criteria(CommercialPartners::Cols::_id, CompareOperator::EQ, partnerId);

  try {
    // First verify the partner exists
    co_await mp.findOne(criteria);

    // Partner found, proceed with deletion
    const auto count = co_await mp.deleteBy(criteria);

    dto::BaseApiResponse response;
    if (count > 0) {
      response.success = true;
      response.message = "Commercial Partner deleted successfully";
    } else {
      response.success = false;
      response.message = "Failed to delete Commercial Partner";
      response.error["code"] = constants::ERR_DB_QUERY;
    }
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Commercial Partner not found";
    errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}




drogon::Task<dto::BaseApiResponse> CommercialPartnerService::deactivateSubscriber(const std::string &partnerId, const std::string &id) {
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> userMapper(dbClient);
  CoroMapper<UserSubscriptions> subMapper(dbClient);

  try {
    // 1. Verify user exists and belongs to the partner
    auto user = co_await userMapper.findOne(
        Criteria(Users::Cols::_id, CompareOperator::EQ, id) &&
        Criteria(Users::Cols::_partner_id, CompareOperator::EQ, partnerId));

    // 2. Check if user has an active subscription
    auto subscriptions = co_await subMapper.findBy(
        Criteria(UserSubscriptions::Cols::_user_id, CompareOperator::EQ, id) &&
        Criteria(UserSubscriptions::Cols::_is_active, CompareOperator::EQ, true));

    if (!subscriptions.empty()) {
      // Deactivate the subscription
      auto sub = subscriptions[0];
      sub.setIsActive(false);
      co_await subMapper.update(sub);
    }

    // 3. Deactivate the user
    user.setIsActive(false);
    co_await userMapper.update(user);

    // 4. Update partner quota (recover the seat)
    CoroMapper<CommercialPartners> partnerMapper(dbClient);
    auto partner = co_await partnerMapper.findByPrimaryKey(partnerId);
    partner.setRemainingQuota(partner.getValueOfRemainingQuota() + 1);
    co_await partnerMapper.update(partner);

    dto::BaseApiResponse response;
    response.success = true;
    response.message = "Subscriber deactivated successfully";
    response.result["userId"] = id;
    response.result["status"] = "Inactive";
    co_return response;

  } catch (const drogon::orm::UnexpectedRows &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Subscriber not found or does not belong to this partner";
    errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    co_return errorResponse;
  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Database error while deactivating subscriber";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}



drogon::Task<dto::BaseApiResponse> CommercialPartnerService::resetSubscriberPassword(const std::string &partnerId, const std::string &id) {
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mp(dbClient);

  try {
    // 1. Verify user exists and belongs to the partner
    auto user = co_await mp.findOne(
        Criteria(Users::Cols::_id, CompareOperator::EQ, id) &&
        Criteria(Users::Cols::_partner_id, CompareOperator::EQ, partnerId));

    // 2. Generate new password
    std::string newPassword = utils::PasswordUtils::generateRandomPassword(8);

    // 3. Update password hash
    user.setPasswordHash(bcrypt::generateHash(newPassword));
    co_await mp.update(user);

    // 4. Send notification (email or SMS based on email type)
    std::string userEmail = user.getValueOfEmail();
    std::string userPhone = user.getValueOfPhoneNumber();

    if (isSystemGeneratedEmail(userEmail)) {
      // Send SMS with new password
      std::string message = "Your Graphic News Plus password has been reset. "
                            "Your new password is: " + newPassword +
                            ". Please login at https://new.graphicnewsplus.com";
      co_await sendSmsNotification(userPhone, message);
      LOG_INFO << "[resetSubscriberPassword] Password reset SMS sent to: " << userPhone;

      dto::BaseApiResponse response;
      response.success = true;
      response.message = "Password reset successfully. New password has been sent via SMS.";
      response.result["userId"] = id;
      co_return response;
    } else {
      // Send email with new credentials
      auto plugin = drogon::app().getPlugin<plugins::GnpServicePlugin>();
      auto &emailService = plugin->getEmailService();

      dto::SendEmailDto emailDto;
      emailDto.setTo(gnp::utils::StringUtils::trim(userEmail));
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
            <p>Hello )html" + user.getValueOfFirstName() + R"html(,</p>
            <p>Your password for Graphic News Plus has been reset by your organization administrator.</p>
            <p>Below are your new login credentials:</p>
            <div class="credentials">
              <div class="credential-item">
                <div class="credential-label">Username (Email):</div>
                <div class="credential-value">)html" + userEmail + R"html(</div>
              </div>
              <div class="credential-item">
                <div class="credential-label">New Password:</div>
                <div class="credential-value">)html" + newPassword + R"html(</div>
              </div>
            </div>
            <p>Please keep these credentials secure and change your password after your next login.</p>
            <p>You can access the platform at: <a href="https://new.graphicnewsplus.com">https://new.graphicnewsplus.com</a></p>
          </div>
          <div class="footer">
            &copy; )html" + trantor::Date::now().toCustomFormattedString("%Y") + R"html( Graphic News Plus. All rights reserved.
          </div>
        </div>
        </body>
        </html>
      )html";

      emailDto.setBody(emailBody);
      co_await emailService.sendEmailAsync(emailDto);

      dto::BaseApiResponse response;
      response.success = true;
      response.message = "Password reset successfully. New password has been sent to the subscriber's email.";
      response.result["userId"] = id;
      co_return response;
    }

  } catch (const drogon::orm::UnexpectedRows &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Subscriber not found or does not belong to this partner";
    errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    co_return errorResponse;
  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Database error while resetting password";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}



// for admin use
drogon::Task<dto::BaseApiResponse> CommercialPartnerService::getPartnerStats() {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<CommercialPartners> mp(dbClient);

  try {
    // 1. Get Total Revenue
    double totalRevenue = 0.0;
    auto revenueResult = co_await dbClient->execSqlCoro(
        "SELECT SUM(invoice_amount - balance) FROM partner_invoices");
    if (!revenueResult.empty() && !revenueResult[0][0].isNull()) {
      totalRevenue = revenueResult[0][0].as<double>();
    }

    // 2. Get Total Partners
    const auto totalPartners = co_await mp.count(Criteria());

    // 3. Get Active Partners
    const auto activePartners = co_await mp.count(Criteria(
        CommercialPartners::Cols::_status, CompareOperator::EQ, "Active"));

    // 4. Get Total Subscriber Quota
    long totalQuota = 0;
    auto quotaResult = co_await dbClient->execSqlCoro(
        "SELECT SUM(subscriber_quota) FROM commercial_partners");
    if (!quotaResult.empty() && !quotaResult[0][0].isNull()) {
      totalQuota = quotaResult[0][0].as<long>();
    }

    // 5. Get Total Partner Users
    long totalPartnerUsers = 0;
    auto usersResult = co_await dbClient->execSqlCoro(
        "SELECT COUNT(*) FROM users WHERE is_partner_admin_user = false "
        "AND partner_id != '00000000-0000-0000-0000-000000000000'");
    if (!usersResult.empty() && !usersResult[0][0].isNull()) {
      totalPartnerUsers = usersResult[0][0].as<long>();
    }

    // 6. Calculate Seat Utilization
    double utilization = 0.0;
    if (totalQuota > 0) {
      utilization = ((double)totalPartnerUsers / totalQuota) * 100.0;
    }

    // 7. Construct Response
    dto::BaseApiResponse response;
    response.success = true;
    Json::Value data = Json::arrayValue;

    // Total Revenue
    Json::Value revenueStat;
    revenueStat["name"] = "Total Revenue";
    revenueStat["value"] = totalRevenue;
    revenueStat["change"] = "+15%"; // Mocked
    revenueStat["changeType"] = "increase";
    revenueStat["icon"] = "BanknotesIcon";
    revenueStat["bgColor"] = "bg-green-50";
    revenueStat["iconColor"] = "text-green-600";
    revenueStat["prefix"] = "GHS ";
    revenueStat["suffix"] = "";
    data.append(revenueStat);

    // Total Partners
    Json::Value totalPartnersStat;
    totalPartnersStat["name"] = "Total Partners";
    totalPartnersStat["value"] = (Json::UInt64)totalPartners;
    totalPartnersStat["change"] = "+8.2%"; // Mocked
    totalPartnersStat["changeType"] = "increase";
    totalPartnersStat["icon"] = "CheckCircleIcon";
    totalPartnersStat["bgColor"] = "bg-blue-50";
    totalPartnersStat["iconColor"] = "text-blue-600";
    totalPartnersStat["prefix"] = "";
    totalPartnersStat["suffix"] = "";
    data.append(totalPartnersStat);

    // Active Partners
    Json::Value activePartnersStat;
    activePartnersStat["name"] = "Active Partners";
    activePartnersStat["value"] = (Json::UInt64)activePartners;
    activePartnersStat["change"] = "-2.1%"; // Mocked
    activePartnersStat["changeType"] = "decrease";
    activePartnersStat["icon"] = "CheckCircleIcon";
    activePartnersStat["bgColor"] = "bg-yellow-50";
    activePartnersStat["iconColor"] = "text-yellow-600";
    activePartnersStat["prefix"] = "";
    activePartnersStat["suffix"] = "";
    data.append(activePartnersStat);

    // Seat Utilization
    Json::Value seatStat;
    seatStat["name"] = "Seat Utilization";
    seatStat["value"] = (int)utilization;
    seatStat["change"] = "-14%"; // Mocked
    seatStat["changeType"] = "decrease";
    seatStat["icon"] = "ClockIcon";
    seatStat["bgColor"] = "bg-red-50";
    seatStat["iconColor"] = "text-red-600";
    seatStat["prefix"] = "";
    seatStat["suffix"] = "%";
    data.append(seatStat);

    response.result = data;
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to fetch partner stats";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

void CommercialPartnerService::getPartnerDetails(
    const std::string &id,
    const std::function<void(const dto::BaseApiResponse &)> &callback) {

  auto dbClient = drogon::app().getDbClient();
  Mapper<CommercialPartners> mp(dbClient);

  // Create criteria to find the partner with specified ID
  Criteria criteria = Criteria(CommercialPartners::Cols::_id, CompareOperator::EQ, id);

  mp.findOne(
      criteria,
      [callback](const CommercialPartners &commercialPartner) {
        dto::BaseApiResponse response;
        response.success = true;

        Json::Value commercialPartnerJson = commercialPartner.toJson();

        // Convert snake_case to camelCase
        Json::Value camelCaseCommercialPartner;

        camelCaseCommercialPartner["id"] = commercialPartnerJson["id"];
        camelCaseCommercialPartner["partnerIdentifier"] =
            commercialPartnerJson["identifier"];
        camelCaseCommercialPartner["name"] = commercialPartnerJson["name"];
        camelCaseCommercialPartner["contactName"] =
            commercialPartnerJson["contact_name"];
        camelCaseCommercialPartner["contactEmail"] =
            commercialPartnerJson["contact_email"];
        camelCaseCommercialPartner["contactPhone"] =
            commercialPartnerJson["contact_phone"];
        camelCaseCommercialPartner["billingEmail"] =
            commercialPartnerJson["billing_email"];
        camelCaseCommercialPartner["billingCycle"] =
            commercialPartnerJson["billing_cycle"];
        camelCaseCommercialPartner["currency"] =
            commercialPartnerJson["currency"];
        camelCaseCommercialPartner["status"] = commercialPartnerJson["status"];
        camelCaseCommercialPartner["subAccountEnabled"] =
            commercialPartnerJson["sub_account_enabled"];
        camelCaseCommercialPartner["subscriberQuota"] =
            commercialPartnerJson["subscriber_quota"];
        camelCaseCommercialPartner["remainingQuota"] =
            commercialPartnerJson["remaining_quota"];
        camelCaseCommercialPartner["createdAt"] =
            commercialPartnerJson["created_at"];

        response.result = camelCaseCommercialPartner;
        callback(response);
      },
      [callback](const DrogonDbException &e) {
        dto::BaseApiResponse errorResponse;
        errorResponse.success = false;
        errorResponse.message = "Commercial Partner not found";
        errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
        errorResponse.error["detail"] = e.base().what();
        callback(errorResponse);
      });
}

void CommercialPartnerService::enableSubaccount(
    const std::string &partnerId,
    const std::function<void(const dto::BaseApiResponse &)> &callback) {

  auto dbClient = drogon::app().getDbClient();
  Mapper<CommercialPartners> mp(dbClient);

  // Create criteria to find the user with specified ID in the tenant
  Criteria criteria =
      Criteria(CommercialPartners::Cols::_id, CompareOperator::EQ, partnerId);

  // Find the user first
  mp.findOne(
      criteria,
      [=](CommercialPartners commercialPartner) {
        if (commercialPartner.getValueOfSubAccountEnabled()) {

          dto::BaseApiResponse response;
          response.success = true;
          response.message = "Commercial Partner account is already enabled.";
          callback(response);
          return;
        }

        // Set the user as not locked out
        commercialPartner.setSubAccountEnabled(true);

        // Update the user in the database
        Mapper<CommercialPartners> updateMp(dbClient);
        updateMp.update(
            commercialPartner,
            [callback](const size_t count) {
              // Successfully updated
              gnp::dto::BaseApiResponse response;
              response.success = true;
              response.message =
                  "Commercial Partner account enabled successfully";
              callback(response);
            },
            [=](const DrogonDbException &e) {
              // Error during update
              gnp::dto::BaseApiResponse errorResponse;
              errorResponse.success = false;
              errorResponse.message =
                  "Failed to enable Commercial Partner account";
              errorResponse.error["code"] = constants::ERR_DB_QUERY;
              errorResponse.error["detail"] = e.base().what();
              callback(errorResponse);
            });
      },
      [callback](const DrogonDbException &e) {
        // partner not found
        gnp::dto::BaseApiResponse errorResponse;
        errorResponse.success = false;
        errorResponse.message = "Commercial Partner not found";
        errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
        errorResponse.error["detail"] = e.base().what();
        callback(errorResponse);
      });
}

void CommercialPartnerService::disableSubaccount(
    const std::string &partnerId,
    const std::function<void(const dto::BaseApiResponse &)> &callback) {

  auto dbClient = drogon::app().getDbClient();
  Mapper<CommercialPartners> mp(dbClient);

  // Create criteria to find the user with specified ID in the tenant
  Criteria criteria =
      Criteria(CommercialPartners::Cols::_id, CompareOperator::EQ, partnerId);

  // Find the user first
  mp.findOne(
      criteria,
      [=](CommercialPartners commercialPartner) {
        if (!commercialPartner.getValueOfSubAccountEnabled()) {

          dto::BaseApiResponse response;
          response.success = true;
          response.message = "Commercial Partner account is already disabled.";
          callback(response);
          return;
        }

        // Set the user as not locked out
        commercialPartner.setSubAccountEnabled(false);

        // Update the user in the database
        Mapper<CommercialPartners> updateMp(dbClient);
        updateMp.update(
            commercialPartner,
            [callback](const size_t count) {
              // Successfully updated
              gnp::dto::BaseApiResponse response;
              response.success = true;
              response.message =
                  "Commercial Partner account disabled successfully";
              callback(response);
            },
            [=](const DrogonDbException &e) {
              // Error during update
              gnp::dto::BaseApiResponse errorResponse;
              errorResponse.success = false;
              errorResponse.message =
                  "Failed to disable Commercial Partner account";
              errorResponse.error["code"] = constants::ERR_DB_QUERY;
              errorResponse.error["detail"] = e.base().what();
              callback(errorResponse);
            });
      },
      [callback](const DrogonDbException &e) {
        // partner not found
        gnp::dto::BaseApiResponse errorResponse;
        errorResponse.success = false;
        errorResponse.message = "Commercial Partner not found";
        errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
        errorResponse.error["detail"] = e.base().what();
        callback(errorResponse);
      });
}

void CommercialPartnerService::updateStatus(
    const std::string &partnerId, const std::string &status,
    const std::function<void(const dto::BaseApiResponse &)> &callback) {

  auto dbClient = drogon::app().getDbClient();
  Mapper<CommercialPartners> mp(dbClient);

  // Create criteria to find the user with specified ID in the tenant
  Criteria criteria =
      Criteria(CommercialPartners::Cols::_id, CompareOperator::EQ, partnerId);

  // Find the user first
  mp.findOne(
      criteria,
      [=](CommercialPartners commercialPartner) {
        // Set the user as not locked out
        commercialPartner.setStatus(status);

        // Update the user in the database
        Mapper<CommercialPartners> updateMp(dbClient);
        updateMp.update(
            commercialPartner,
            [callback](const size_t count) {
              // Successfully updated
              gnp::dto::BaseApiResponse response;
              response.success = true;
              response.message =
                  "Commercial Partner status updated successfully";
              callback(response);
            },
            [=](const DrogonDbException &e) {
              // Error during update
              gnp::dto::BaseApiResponse errorResponse;
              errorResponse.success = false;
              errorResponse.message =
                  "Failed to update Commercial Partner status";
              errorResponse.error["code"] = constants::ERR_DB_QUERY;
              errorResponse.error["detail"] = e.base().what();
              callback(errorResponse);
            });
      },
      [callback](const DrogonDbException &e) {
        // partner not found
        gnp::dto::BaseApiResponse errorResponse;
        errorResponse.success = false;
        errorResponse.message = "Commercial Partner not found";
        errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
        errorResponse.error["detail"] = e.base().what();
        callback(errorResponse);
      });
}

drogon::Task<::gnp::dto::BaseApiResponse>
CommercialPartnerService::deletePartnerSubscriberAsync(
    const std::string &partnerId, const std::string &subscriberId) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<::drogon_model::Gnp::Users> userMapper(dbClient);
  CoroMapper<::drogon_model::Gnp::CommercialPartners> partnerMapper(dbClient);

  try {
    // 1. Fetch user to verify ownership
    auto user = co_await userMapper.findByPrimaryKey(subscriberId);

    if (user.getValueOfPartnerId() != partnerId) {
      dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "User does not belong to this partner";
      errorResponse.error["code"] = constants::ERR_UNAUTHORIZED;
      co_return errorResponse;
    }

    // 2. Delete user
    co_await userMapper.deleteByPrimaryKey(subscriberId);

    // 3. Update partner quota
    auto partner = co_await partnerMapper.findByPrimaryKey(partnerId);
    partner.setRemainingQuota(partner.getValueOfRemainingQuota() + 1);
    co_await partnerMapper.update(partner);

    // 4. Delete related subscription records
    CoroMapper<::drogon_model::Gnp::UserSubscriptions> subMapper(dbClient);
    co_await subMapper.deleteBy(
        Criteria(::drogon_model::Gnp::UserSubscriptions::Cols::_user_id,
                 CompareOperator::EQ, subscriberId));

    // 5. Delete related renewal records
    CoroMapper<::drogon_model::Gnp::SubscriptionRenewalHistory> renewalMapper(
        dbClient);
    co_await renewalMapper.deleteBy(Criteria(
        ::drogon_model::Gnp::SubscriptionRenewalHistory::Cols::_user_id,
        CompareOperator::EQ, subscriberId));

    dto::BaseApiResponse successResponse;
    successResponse.success = true;
    successResponse.message =
        "Subscriber deleted successfully, quota recovered";
    co_return successResponse;

  } catch (const DrogonDbException &e) {
    ::gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Database error during subscriber deletion";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<::gnp::dto::BaseApiResponse>
CommercialPartnerService::getPartnerApiKeys(const std::string &partnerId) {
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<drogon_model::Gnp::CommercialPartnerApiKeys> mp(dbClient);

  try {
    auto apiKeys = co_await mp.findBy(
        Criteria(drogon_model::Gnp::CommercialPartnerApiKeys::Cols::_partner_id,
                 CompareOperator::EQ, partnerId));

    gnp::dto::BaseApiResponse response;
    response.success = true;
    response.message = "API keys fetched successfully";

    auto pgArrayToJson = [](const std::string &pgArr) {
      Json::Value arr = Json::arrayValue;
      if (pgArr.length() < 2 || pgArr.front() != '{' || pgArr.back() != '}')
        return arr;
      std::string content = pgArr.substr(1, pgArr.length() - 2);
      std::stringstream ss(content);
      std::string item;
      while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
          if (item.front() == '"' && item.back() == '"' && item.length() >= 2) {
            item = item.substr(1, item.length() - 2);
          }
          arr.append(item);
        }
      }
      return arr;
    };

    Json::Value data = Json::arrayValue;
    for (const auto &apiKey : apiKeys) {
      Json::Value apiKeyJson = apiKey.toJson();
      Json::Value camelCaseApiKey;

      camelCaseApiKey["id"] = apiKeyJson["id"];
      camelCaseApiKey["partnerId"] = apiKeyJson["partner_id"];
      camelCaseApiKey["partnerName"] = apiKeyJson["partner_name"];
      camelCaseApiKey["clientId"] = apiKeyJson["client_id"];
      camelCaseApiKey["label"] = apiKeyJson["label"];

      // Parse scopes (database type is json)
      if (apiKeyJson["scopes"].isString()) {
        std::string scopesStr = apiKeyJson["scopes"].asString();
        Json::Value scopesJson;
        Json::Reader reader;
        if (reader.parse(scopesStr, scopesJson)) {
          camelCaseApiKey["scopes"] = scopesJson;
        } else {
          camelCaseApiKey["scopes"] = Json::arrayValue;
        }
      } else {
        camelCaseApiKey["scopes"] = apiKeyJson["scopes"];
      }

      // Parse allowedIps (PostgreSQL array type: {"val1","val2"})
      camelCaseApiKey["allowedIps"] =
          pgArrayToJson(apiKeyJson["allowed_ips"].asString());

      camelCaseApiKey["isActive"] = apiKeyJson["is_active"];
      camelCaseApiKey["lastUsedAt"] = apiKeyJson["last_used_at"];
      camelCaseApiKey["createdAt"] = apiKeyJson["created_at"];

      data.append(camelCaseApiKey);
    }
    response.result = data;
    co_return response;
  } catch (const DrogonDbException &e) {
    gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to fetch API keys";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<::gnp::dto::BaseApiResponse> CommercialPartnerService::generatePartnerApiKey(
    const ::gnp::dto::GeneratePartnerApiKeyDto &dto) {
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<drogon_model::Gnp::CommercialPartnerApiKeys> mp(dbClient);

  try {
    // 1. Generate Client ID and Client Secret
    std::string clientId =
        "gnp_" + utils::IdGeneratorUtils::generateAlphanumericId(10);
    std::string clientSecret =
        "gnp_sk_" + utils::IdGeneratorUtils::generateAlphanumericId(18);

    // 2. Hash Client Secret
    std::string clientSecretHash = bcrypt::generateHash(clientSecret);

    // 3. Prepare model
    drogon_model::Gnp::CommercialPartnerApiKeys apiKey;
    apiKey.setPartnerId(dto.getPartnerId());
    apiKey.setClientId(clientId);
    apiKey.setClientSecretHash(clientSecretHash);
    apiKey.setLabel(dto.getLabel());
    apiKey.setPartnerName(dto.getPartnerName());

    // Format scopes as JSON string (database type is json)
    Json::Value scopesJson = Json::arrayValue;
    for (const auto &scope : dto.getScopes()) {
      scopesJson.append(scope);
    }
    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = ""; // Compact JSON
    apiKey.setScopes(Json::writeString(writerBuilder, scopesJson));

    // Format allowed IPs as PostgreSQL array literal: {"ip1", "ip2"}
    std::string ipsStr = "{";
    for (size_t i = 0; i < dto.getAllowedIps().size(); ++i) {
      ipsStr += "\"" + dto.getAllowedIps()[i] + "\"";
      if (i < dto.getAllowedIps().size() - 1)
        ipsStr += ",";
    }
    ipsStr += "}";
    apiKey.setAllowedIps(ipsStr);

    apiKey.setIsActive(true);
    apiKey.setCreatedAt(trantor::Date::now());

    // 4. Save to database
    co_await mp.insert(apiKey);

    // 5. Prepare response
    gnp::dto::BaseApiResponse response;
    response.success = true;
    response.message = "API key generated successfully";

    Json::Value data;

    data["clientSecret"] = clientSecret; // Return plain secret ONLY ONCE
    data["clientId"] = clientId;

    for (const auto &scope : dto.getScopes())
      scopesJson.append(scope);
    data["scopes"] = scopesJson;

    Json::Value ipsJson = Json::arrayValue;
    for (const auto &ip : dto.getAllowedIps())
      ipsJson.append(ip);
    data["allowedIps"] = ipsJson;

    response.result = data;
    co_return response;
  } catch (const DrogonDbException &e) {
    gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to generate API key";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<::gnp::dto::BaseApiResponse> CommercialPartnerService::revokePartnerApiKey(const std::string &partnerId,
                                              const std::string &id) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<drogon_model::Gnp::CommercialPartnerApiKeys> mp(dbClient);

  try {
    auto apiKey = co_await mp.findOne(
        Criteria(drogon_model::Gnp::CommercialPartnerApiKeys::Cols::_partner_id,
                 CompareOperator::EQ, partnerId) &&
        Criteria(drogon_model::Gnp::CommercialPartnerApiKeys::Cols::_id,
                 CompareOperator::EQ, id));

    apiKey.setIsActive(false);
    co_await mp.update(apiKey);

    gnp::dto::BaseApiResponse response;
    response.success = true;
    response.message = "API key revoked successfully";
    co_return response;

  } catch (const DrogonDbException &e) {
    gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to revoke API key";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<::gnp::dto::BaseApiResponse> CommercialPartnerService::activatePartnerApiKey(const std::string &partnerId,
                                                const std::string &id) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<drogon_model::Gnp::CommercialPartnerApiKeys> mp(dbClient);

  try {
    auto apiKey = co_await mp.findOne(
        Criteria(drogon_model::Gnp::CommercialPartnerApiKeys::Cols::_partner_id,
                 CompareOperator::EQ, partnerId) &&
        Criteria(drogon_model::Gnp::CommercialPartnerApiKeys::Cols::_id,
                 CompareOperator::EQ, id));

    if (apiKey.getValueOfIsActive()) {

      gnp::dto::BaseApiResponse response;
      response.success = true;
      response.message = "API key already active";
      co_return response;
    }

    apiKey.setIsActive(true);
    co_await mp.update(apiKey);

    gnp::dto::BaseApiResponse response;
    response.success = true;
    response.message = "API key activated successfully";
    co_return response;

  } catch (const DrogonDbException &e) {
    gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to revoke API key";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<::gnp::dto::BaseApiResponse> CommercialPartnerService::deletePartnerApiKey(const std::string &id) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<drogon_model::Gnp::CommercialPartnerApiKeys> mp(dbClient);

  try {

    co_await mp.deleteByPrimaryKey(id);

    gnp::dto::BaseApiResponse response;
    response.success = true;
    response.message = "API key deleted successfully";
    co_return response;

  } catch (const DrogonDbException &e) {
    gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to delete API key";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<::gnp::dto::BaseApiResponse> CommercialPartnerService::updatePartnerApiKey(
    const ::gnp::dto::UpdatePartnerApiKeyDto &dto) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<drogon_model::Gnp::CommercialPartnerApiKeys> mp(dbClient);

  try {
    auto apiKey = co_await mp.findByPrimaryKey(dto.getId());

    // Update scopes (database type is json)
    Json::Value scopesJson = Json::arrayValue;
    for (const auto &scope : dto.getScopes()) {
      scopesJson.append(scope);
    }
    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = ""; // Compact JSON
    apiKey.setScopes(Json::writeString(writerBuilder, scopesJson));

    // Update allowed IPs (database type is text[])
    std::string ipsStr = "{";
    for (size_t i = 0; i < dto.getAllowedIps().size(); ++i) {
      ipsStr += "\"" + dto.getAllowedIps()[i] + "\"";
      if (i < dto.getAllowedIps().size() - 1)
        ipsStr += ",";
    }
    ipsStr += "}";
    apiKey.setAllowedIps(ipsStr);

    co_await mp.update(apiKey);

    gnp::dto::BaseApiResponse response;
    response.success = true;
    response.message = "API key updated successfully";
    co_return response;

  } catch (const DrogonDbException &e) {
    gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to update API key";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<::gnp::dto::BaseApiResponse> CommercialPartnerService::onboardSubscriberAsync(
    const std::string &clientId, const std::string &clientSecret,
    const ::gnp::dto::PartnerOnboardingDto &dto) {

  LOG_INFO << "[onboardSubscriberAsync] START — clientId=" << clientId
           << " phoneNumber=" << dto.getPhoneNumber()
           << " fullName=" << dto.getFullName()
           << " startDate=" << dto.getStartDate()
           << " endDate=" << dto.getEndDate()
           << " smsProvider=" << dto.getSmsProvider();

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<drogon_model::Gnp::CommercialPartnerApiKeys> apiKeyMapper(dbClient);

  try {
    // STEP 1: Verify API Key exists and is active
    LOG_INFO
        << "[onboardSubscriberAsync] STEP 1 — Looking up API key for clientId="
        << clientId;
    drogon_model::Gnp::CommercialPartnerApiKeys apiKey;
    try {
      apiKey = co_await apiKeyMapper.findOne(
          Criteria(
              drogon_model::Gnp::CommercialPartnerApiKeys::Cols::_client_id,
              CompareOperator::EQ, clientId) &&
          Criteria(
              drogon_model::Gnp::CommercialPartnerApiKeys::Cols::_is_active,
              CompareOperator::EQ, true));
      LOG_INFO << "[onboardSubscriberAsync] STEP 1 — API key found, partnerId="
               << apiKey.getValueOfPartnerId();
    } catch (const DrogonDbException &e) {
      LOG_ERROR
          << "[onboardSubscriberAsync] STEP 1 FAILED — API key lookup threw: "
          << e.base().what() << " (clientId=" << clientId
          << " may not exist or is_active=false)";
      gnp::dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "Onboarding failed or unauthorized";
      errorResponse.error["code"] = constants::ERR_DB_QUERY;
      errorResponse.error["detail"] = e.base().what();
      co_return errorResponse;
    }

    // STEP 2: Validate client secret via bcrypt
    LOG_INFO << "[onboardSubscriberAsync] STEP 2 — Validating clientSecret "
                "against stored hash";
    if (!bcrypt::validatePassword(clientSecret,
                                  apiKey.getValueOfClientSecretHash())) {
      LOG_ERROR << "[onboardSubscriberAsync] STEP 2 FAILED — clientSecret "
                   "bcrypt mismatch for clientId="
                << clientId;
      gnp::dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "Invalid ClientSecret";
      errorResponse.error["code"] = constants::ERR_UNAUTHORIZED;
      co_return errorResponse;
    }
    LOG_INFO << "[onboardSubscriberAsync] STEP 2 — clientSecret validated "
                "successfully";

    // Update last used at
    apiKey.setLastUsedAt(trantor::Date::now());
    co_await apiKeyMapper.update(apiKey);

    // STEP 3: Look up existing user by phone + partnerId
    const std::string partnerId = apiKey.getValueOfPartnerId();
    LOG_INFO << "[onboardSubscriberAsync] STEP 3 — Searching for existing "
                "user: phoneNumber="
             << dto.getPhoneNumber() << " partnerId=" << partnerId;
    CoroMapper<drogon_model::Gnp::Users> userMapper(dbClient);
    auto existingUsers = co_await userMapper.findBy(Criteria(drogon_model::Gnp::Users::Cols::_phone_number, CompareOperator::EQ, dto.getPhoneNumber()) && Criteria(drogon_model::Gnp::Users::Cols::_partner_id, CompareOperator::EQ, partnerId));
    LOG_INFO << "[onboardSubscriberAsync] STEP 3 — existingUsers count="
             << existingUsers.size();

    // STEP 4: Load partner record
    LOG_INFO << "[onboardSubscriberAsync] STEP 4 — Loading partner record for "
                "partnerId="
             << partnerId;
    CoroMapper<drogon_model::Gnp::CommercialPartners> partnerMapper(dbClient);
    drogon_model::Gnp::CommercialPartners partner;
    try {
      partner = co_await partnerMapper.findByPrimaryKey(partnerId);
      LOG_INFO << "[onboardSubscriberAsync] STEP 4 — Partner found: name="
               << partner.getValueOfName() << " defaultPlanId="
               << partner.getValueOfDefaultSubscriptionPlanId()
               << " remainingQuota=" << partner.getValueOfRemainingQuota();
    } catch (const DrogonDbException &e) {
      LOG_ERROR
          << "[onboardSubscriberAsync] STEP 4 FAILED — Partner lookup threw: "
          << e.base().what() << " (partnerId=" << partnerId
          << " may not exist)";
      gnp::dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "Onboarding failed or unauthorized";
      errorResponse.error["code"] = constants::ERR_DB_QUERY;
      errorResponse.error["detail"] = e.base().what();
      co_return errorResponse;
    }

    // STEP 5: Load default subscription plan
    const std::string defaultPlanId =
        partner.getValueOfDefaultSubscriptionPlanId();
    LOG_INFO << "[onboardSubscriberAsync] STEP 5 — Loading subscription plan "
                "for planId="
             << defaultPlanId;
    CoroMapper<drogon_model::Gnp::SubscriptionPlans> planMapper(dbClient);
    drogon_model::Gnp::SubscriptionPlans plan;
    try {
      plan = co_await planMapper.findByPrimaryKey(defaultPlanId);
      LOG_INFO << "[onboardSubscriberAsync] STEP 5 — Plan found: name="
               << plan.getValueOfName()
               << " targetPublications=" << plan.getValueOfTargetPublications();
    } catch (const DrogonDbException &e) {
      LOG_ERROR
          << "[onboardSubscriberAsync] STEP 5 FAILED — Subscription plan "
             "lookup threw: "
          << e.base().what() << " (planId=" << defaultPlanId
          << " may not exist or partner has no defaultSubscriptionPlanId)";
      gnp::dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "Onboarding failed or unauthorized";
      errorResponse.error["code"] = constants::ERR_DB_QUERY;
      errorResponse.error["detail"] = e.base().what();
      co_return errorResponse;
    }

    std::string targetPubsStr = plan.getValueOfTargetPublications();
    std::vector<std::string> pubIds;
    if (!targetPubsStr.empty()) {
      Json::Value targetPubs;
      Json::CharReaderBuilder readerBuilder;
      std::string errs;
      std::istringstream s(targetPubsStr);
      if (Json::parseFromStream(readerBuilder, s, &targetPubs, &errs)) {
        for (const auto &p : targetPubs) {
          pubIds.push_back(p.asString());
        }
      } else {
        LOG_WARN << "[onboardSubscriberAsync] STEP 5 — Failed to parse "
                    "targetPublications JSON: "
                 << errs;
      }
    }
    LOG_INFO << "[onboardSubscriberAsync] STEP 5 — pubIds count="
             << pubIds.size();

    // STEP 6: Resolve subscription date window
    trantor::Date startDateObj;
    trantor::Date endDateObj;

    if (!dto.getStartDate().empty()) {
      startDateObj =
          trantor::Date::fromDbStringLocal(dto.getStartDate() + " 00:00:00");
    } else {
      startDateObj = trantor::Date::now();
    }

    if (!dto.getEndDate().empty()) {
      endDateObj =
          trantor::Date::fromDbStringLocal(dto.getEndDate() + " 23:59:59");
    } else {
      endDateObj = trantor::Date(trantor::Date::now().microSecondsSinceEpoch() +
                                 30LL * 24 * 3600 * 1000000);
    }

    LOG_INFO << "[onboardSubscriberAsync] STEP 6 — Date window: startDate="
             << startDateObj.toCustomFormattedString("%Y-%m-%d")
             << " endDate=" << endDateObj.toCustomFormattedString("%Y-%m-%d")
             << " (dtoStartDate='" << dto.getStartDate() << "' dtoEndDate='"
             << dto.getEndDate() << "')";

    if (endDateObj.microSecondsSinceEpoch() <
        startDateObj.microSecondsSinceEpoch()) {
      LOG_ERROR << "[onboardSubscriberAsync] STEP 6 FAILED — endDate is before "
                   "startDate";
      gnp::dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "End date cannot be earlier than start date.";
      errorResponse.error["code"] = constants::ERR_VALIDATION;
      co_return errorResponse;
    }

    // STEP 7: Fetch newspapers for the date window
    std::vector<drogon_model::Gnp::Newspapers> newspapers;
    if (!pubIds.empty()) {

      LOG_INFO << "[onboardSubscriberAsync] STEP 7 — Fetching newspapers for "
                  "pubIds window";

      CoroMapper<drogon_model::Gnp::Newspapers> newsMapper(dbClient);

      Criteria pubCriteria =
          Criteria(drogon_model::Gnp::Newspapers::Cols::_publication_id,
                   CompareOperator::In, pubIds) &&
          Criteria(drogon_model::Gnp::Newspapers::Cols::_is_archived,
                   CompareOperator::EQ, false) &&
          Criteria(drogon_model::Gnp::Newspapers::Cols::_publication_date,
                   CompareOperator::GE, startDateObj) &&
          Criteria(drogon_model::Gnp::Newspapers::Cols::_publication_date,
                   CompareOperator::LE, endDateObj);

      newspapers = co_await newsMapper.findBy(pubCriteria);
      LOG_INFO << "[onboardSubscriberAsync] STEP 7 — newspapers fetched count="
               << newspapers.size();
    } else {
      LOG_INFO << "[onboardSubscriberAsync] STEP 7 — No pubIds configured on "
                  "plan, skipping newspaper fetch";
    }

    Json::StreamWriterBuilder writerBuilder;
    writerBuilder["indentation"] = "";

    CoroMapper<UserSubscriptions> subMapper(dbClient);
    CoroMapper<drogon_model::Gnp::SubscriptionRenewalHistory> renewalMapper(
        dbClient);

    auto determineBillingCycle = [](const trantor::Date &start,
                                    const trantor::Date &end) -> std::string {
      int64_t diffDays =
          (end.microSecondsSinceEpoch() - start.microSecondsSinceEpoch()) /
          (1000000LL * 3600 * 24);
      if (diffDays <= 7)
        return "Weekly";
      if (diffDays <= 31)
        return "Monthly";
      if (diffDays <= 92)
        return "Quarterly";
      if (diffDays <= 184)
        return "Half-Yearly";
      return "Annual";
    };

    // STEP 8: Handle existing user path (renewal)
    if (!existingUsers.empty()) {
      auto existingUser = existingUsers[0];
      LOG_INFO
          << "[onboardSubscriberAsync] STEP 8 — RENEWAL PATH: existing userId="
          << existingUser.getValueOfId()
          << " email=" << existingUser.getValueOfEmail();

      auto subs = co_await subMapper.findBy(
          Criteria(UserSubscriptions::Cols::_user_id, CompareOperator::EQ,
                   existingUser.getValueOfId()));
      LOG_INFO
          << "[onboardSubscriberAsync] STEP 8 — existingSubscriptions count="
          << subs.size();

      if (!subs.empty()) {
        auto sub = subs[0];

        auto oldStartDate = sub.getValueOfStartDate();
        auto oldEndDate = sub.getValueOfEndDate();

        std::string currentStartStr =
            oldStartDate.toCustomFormattedString("%Y-%m-%d");
        std::string currentEndStr =
            oldEndDate.toCustomFormattedString("%Y-%m-%d");
        std::string newStartStr =
            startDateObj.toCustomFormattedString("%Y-%m-%d");
        std::string newEndStr = endDateObj.toCustomFormattedString("%Y-%m-%d");

        LOG_INFO << "[onboardSubscriberAsync] STEP 8 — Existing sub period: "
                 << currentStartStr << " -> " << currentEndStr
                 << "; Requested: " << newStartStr << " -> " << newEndStr;

        if (currentStartStr == newStartStr && currentEndStr == newEndStr) {
          LOG_WARN << "[onboardSubscriberAsync] STEP 8 — Duplicate period "
                      "detected, rejecting";
          gnp::dto::BaseApiResponse errorResponse;
          errorResponse.success = false;
          errorResponse.message =
              "An active subscription already exists for the period " +
              newStartStr + " to " + newEndStr + ".";
          errorResponse.error["code"] = constants::ERR_VALIDATION;
          co_return errorResponse;
        }

        sub.setStartDate(startDateObj);
        sub.setEndDate(endDateObj);

        std::string currEntStr = sub.getValueOfNewspaperEntitlements();
        Json::Value currEnt;
        if (!currEntStr.empty()) {
          Json::CharReaderBuilder readerBuilder;
          std::string errs;
          std::istringstream s(currEntStr);
          if (!Json::parseFromStream(readerBuilder, s, &currEnt, &errs)) {
            LOG_WARN << "[onboardSubscriberAsync] STEP 8 — Failed to parse "
                        "existing entitlements: "
                     << errs;
            currEnt = Json::arrayValue;
          }
        } else {
          currEnt = Json::arrayValue;
        }

        for (const auto &news : newspapers) {
          bool found = false;
          for (const auto &e : currEnt) {
            if (e.isObject() && e.isMember("id") &&
                e["id"].asString() == news.getValueOfId()) {
              found = true;
              break;
            }
          }
          if (!found) {
            Json::Value ent;
            ent["id"] = news.getValueOfId();
            ent["uniqueId"] =
                gnp::utils::IdGeneratorUtils::generateAlphanumericId();
            currEnt.append(ent);
          }
        }

        sub.setNewspaperEntitlements(Json::writeString(writerBuilder, currEnt));
        LOG_INFO << "[onboardSubscriberAsync] STEP 8 — Updating existing "
                    "subscription record";
        co_await subMapper.update(sub);

        drogon_model::Gnp::SubscriptionRenewalHistory renewal;
        renewal.setSubscriptionIdentifier(
            sub.getValueOfSubscriptionIdentifier());
        renewal.setUserId(existingUser.getValueOfId());
        renewal.setUserName(existingUser.getValueOfFirstName() + " " +
                            existingUser.getValueOfLastName());
        renewal.setEmail(existingUser.getValueOfEmail());
        renewal.setSubscriptionPlanId(plan.getValueOfId());
        renewal.setPartnerId(partnerId);
        renewal.setSubscriptionPlanName(plan.getValueOfName());
        renewal.setPastBillingCycle(
            determineBillingCycle(oldStartDate, oldEndDate));
        renewal.setCurrentBillingCycle(
            determineBillingCycle(startDateObj, endDateObj));
        renewal.setTransactionStatus("Successful");
        renewal.setPaidBy(partner.getValueOfName());
        renewal.setAmountPaid(partner.getValueOfCostPerHead());
        renewal.setCreatedAt(trantor::Date::now());
        LOG_INFO << "[onboardSubscriberAsync] STEP 8 — Inserting renewal "
                    "history record";
        co_await renewalMapper.insert(renewal);
      } else {
        LOG_INFO << "[onboardSubscriberAsync] STEP 8 — No existing "
                    "subscription for user, creating new sub record";
        drogon_model::Gnp::UserSubscriptions newSub;
        newSub.setUserId(existingUser.getValueOfId());
        newSub.setPartnerId(apiKey.getValueOfPartnerId());
        newSub.setSubscriptionPlanId(plan.getValueOfId());
        newSub.setSubscriptionPlanDescription(plan.getValueOfName());
        newSub.setEmail(existingUser.getValueOfEmail());
        newSub.setStartDate(startDateObj);
        newSub.setEndDate(endDateObj);
        newSub.setBillingCycle(determineBillingCycle(startDateObj, endDateObj));
        newSub.setIsActive(true);

        Json::Value newEntArray = Json::arrayValue;
        for (const auto &news : newspapers) {
          Json::Value ent;
          ent["id"] = news.getValueOfId();
          ent["uniqueId"] =
              gnp::utils::IdGeneratorUtils::generateAlphanumericId();
          newEntArray.append(ent);
        }
        newSub.setNewspaperEntitlements(
            Json::writeString(writerBuilder, newEntArray));
        newSub.setCreatedAt(trantor::Date::now());
        newSub.setSubscriptionIdentifier(
            gnp::utils::IdGeneratorUtils::generateRandomSixDigit());
        co_await subMapper.insert(newSub);
        LOG_INFO << "[onboardSubscriberAsync] STEP 8 — New subscription "
                    "inserted for existing user";

        drogon_model::Gnp::SubscriptionRenewalHistory renewal;
        renewal.setSubscriptionIdentifier(
            newSub.getValueOfSubscriptionIdentifier());
        renewal.setUserId(existingUser.getValueOfId());
        renewal.setUserName(existingUser.getValueOfFirstName() + " " +
                            existingUser.getValueOfLastName());
        renewal.setEmail(existingUser.getValueOfEmail());
        renewal.setSubscriptionPlanId(plan.getValueOfId());
        renewal.setSubscriptionPlanName(plan.getValueOfName());
        renewal.setPastBillingCycle("N/A");
        renewal.setCurrentBillingCycle(
            determineBillingCycle(startDateObj, endDateObj));
        renewal.setTransactionStatus("Successful");
        renewal.setPaidBy("Partner API");
        renewal.setAmountPaid("0.00");
        renewal.setCreatedAt(trantor::Date::now());
        co_await renewalMapper.insert(renewal);
        LOG_INFO << "[onboardSubscriberAsync] STEP 8 — Renewal history "
                    "inserted for existing user (first sub)";
      }

      if (dto.getSmsProvider() == "platform" ||
          dto.getSmsProvider() == "Platform") {
        LOG_INFO << "[onboardSubscriberAsync] STEP 8 — Sending renewal SMS via "
                    "platform to "
                 << dto.getPhoneNumber();
        auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
        auto &hubtelSmsApi = plugin->getHubtelSmsApi();
        std::string endDateFmt = endDateObj.toCustomFormattedString("%d-%b-%Y");
        std::string messageContent = "Your Graphic NewsPlus subscription has "
                                     "been renewed successfully. Valid till " +
                                     endDateFmt +
                                     ". Visit https://new.graphicnewsplus.com "
                                     "to access more engaging content.";
        co_await hubtelSmsApi.sendSms(dto.getPhoneNumber(), messageContent);
      }

      LOG_INFO
          << "[onboardSubscriberAsync] DONE — Renewal completed for userId="
          << existingUser.getValueOfId();
      gnp::dto::BaseApiResponse response;
      response.success = true;
      response.message = "Subscriber renewed successfully";
      response.result["status"] = "Active";
      response.result["email"] = existingUser.getValueOfEmail();
      response.result["endDate"] =
          endDateObj.toCustomFormattedString("%d-%b-%Y");
      co_return response;
    }

    // STEP 9: New subscriber creation path
    LOG_INFO << "[onboardSubscriberAsync] STEP 9 — NEW USER PATH: creating "
                "subscriber for phoneNumber="
             << dto.getPhoneNumber();
    std::string fullName = dto.getFullName();
    std::string firstName, lastName;
    size_t lastSpace = fullName.find_last_of(' ');
    if (lastSpace != std::string::npos) {
      firstName = fullName.substr(0, lastSpace);
      lastName = fullName.substr(lastSpace + 1);
    } else {
      firstName = fullName;
    }
    LOG_INFO << "[onboardSubscriberAsync] STEP 9 — Parsed name: firstName='"
             << firstName << "' lastName='" << lastName << "'";

    drogon_model::Gnp::Users newUser;
    newUser.setFirstName(firstName);
    if (!lastName.empty())
      newUser.setLastName(lastName);
    newUser.setPhoneNumber(dto.getPhoneNumber());
    newUser.setEmail(dto.getPhoneNumber() + "@graphicnewsplus.com.gh");
    newUser.setPartnerId(apiKey.getValueOfPartnerId());
    newUser.setIsActive(true);
    newUser.setIsLockedOut(false);
    newUser.setCreatedAt(trantor::Date::now());
    newUser.setUsername(dto.getPhoneNumber());
    std::string password = utils::PasswordUtils::generateRandomPassword(8);
    newUser.setPasswordHash(bcrypt::generateHash(password));

    LOG_INFO
        << "[onboardSubscriberAsync] STEP 9 — Inserting new user record (email="
        << dto.getPhoneNumber() + "@graphicnewsplus.com.gh"
        << ")";
    auto newUserResult = co_await userMapper.insert(newUser);
    LOG_INFO << "[onboardSubscriberAsync] STEP 9 — New user inserted, userId="
             << newUserResult.getValueOfId();

    // STEP 10: Update partner quota
    auto remainingQuota = partner.getValueOfRemainingQuota();
    LOG_INFO << "[onboardSubscriberAsync] STEP 10 — remainingQuota="
             << remainingQuota;
    if (remainingQuota > 0) {
      partner.setRemainingQuota(remainingQuota - 1);
      co_await partnerMapper.update(partner);
      LOG_INFO
          << "[onboardSubscriberAsync] STEP 10 — Partner quota decremented to "
          << (remainingQuota - 1);
    } else {
      LOG_WARN << "[onboardSubscriberAsync] STEP 10 — remainingQuota is 0, "
                  "quota NOT decremented";
    }

    // STEP 11: Create subscription record
    LOG_INFO << "[onboardSubscriberAsync] STEP 11 — Creating UserSubscription "
                "record";
    drogon_model::Gnp::UserSubscriptions newSub;
    newSub.setUserId(newUserResult.getValueOfId());
    newSub.setPartnerId(apiKey.getValueOfPartnerId());
    newSub.setSubscriptionPlanId(plan.getValueOfId());
    newSub.setSubscriptionPlanDescription(plan.getValueOfName());
    newSub.setEmail(newUserResult.getValueOfEmail());
    newSub.setStartDate(startDateObj);
    newSub.setEndDate(endDateObj);
    newSub.setIsActive(true);

    Json::Value newEntArray = Json::arrayValue;
    for (const auto &news : newspapers) {
      Json::Value ent;
      ent["id"] = news.getValueOfId();
      ent["uniqueId"] = utils::IdGeneratorUtils::generateAlphanumericId();
      newEntArray.append(ent);
    }
    newSub.setNewspaperEntitlements(
        Json::writeString(writerBuilder, newEntArray));
    newSub.setCreatedAt(trantor::Date::now());
    newSub.setSubscriptionIdentifier(
        utils::IdGeneratorUtils::generateRandomSixDigit());
    co_await subMapper.insert(newSub);
    LOG_INFO << "[onboardSubscriberAsync] STEP 11 — UserSubscription inserted, "
                "identifier="
             << newSub.getValueOfSubscriptionIdentifier();

    // STEP 12: Insert renewal history
    LOG_INFO << "[onboardSubscriberAsync] STEP 12 — Inserting "
                "SubscriptionRenewalHistory";
    drogon_model::Gnp::SubscriptionRenewalHistory renewal;
    renewal.setSubscriptionIdentifier(
        newSub.getValueOfSubscriptionIdentifier());
    renewal.setUserId(newUserResult.getValueOfId());
    renewal.setUserName(newUserResult.getValueOfFirstName() + " " +
                        newUserResult.getValueOfLastName());
    renewal.setEmail(newUserResult.getValueOfEmail());
    renewal.setSubscriptionPlanId(plan.getValueOfId());
    renewal.setSubscriptionPlanName(plan.getValueOfName());
    renewal.setPastBillingCycle("N/A");
    renewal.setCurrentBillingCycle(
        determineBillingCycle(startDateObj, endDateObj));
    renewal.setTransactionStatus("Successful");
    renewal.setPaidBy("Partner API");
    renewal.setAmountPaid("0.00");
    renewal.setCreatedAt(trantor::Date::now());
    co_await renewalMapper.insert(renewal);
    LOG_INFO << "[onboardSubscriberAsync] STEP 12 — SubscriptionRenewalHistory "
                "inserted";

    if (dto.getSmsProvider() != "platform") {
      LOG_INFO
          << "[onboardSubscriberAsync] DONE — smsProvider='"
          << dto.getSmsProvider()
          << "' (not 'platform'), returning credentials without sending SMS";
      gnp::dto::BaseApiResponse response;
      response.success = true;
      response.message = "Subscriber onboarded successfully";
      response.result["username"] = dto.getPhoneNumber();
      response.result["password"] = password;
      response.result["email"] =
          dto.getPhoneNumber() + "@graphicnewsplus.com.gh";
      response.result["status"] = "Active";
      response.result["createdAt"] =
          trantor::Date::now().toCustomFormattedString("%d-%b-%Y %H:%M:%S");
      response.result["message"] =
          "Congratulations! Your Graphic NewsPlus account has been created. "
          "Visit https://new.graphicnewsplus.com and login with Username: " +
          dto.getPhoneNumber() + " & Password: " + password;
      co_return response;
    }

    // STEP 13: Send welcome SMS
    LOG_INFO << "[onboardSubscriberAsync] STEP 13 — Sending welcome SMS to "
                "phoneNumber="
             << dto.getPhoneNumber();
    auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
    auto &hubtelSmsApi = plugin->getHubtelSmsApi();

    std::string messageContent =
        "Congratulations! Your Graphic NewsPlus account has been created. "
        "Visit https://new.graphicnewsplus.com and login with Username: " +
        dto.getPhoneNumber() + " & Password: " + password;

    co_await hubtelSmsApi.sendSms(dto.getPhoneNumber(), messageContent);
    LOG_INFO << "[onboardSubscriberAsync] STEP 13 — SMS sent";

    LOG_INFO << "[onboardSubscriberAsync] DONE — New subscriber onboarded "
                "successfully for phoneNumber="
             << dto.getPhoneNumber();
    gnp::dto::BaseApiResponse response;
    response.success = true;
    response.message = "Subscriber onboarded successfully";
    response.result["status"] = "Active";
    response.result["email"] = dto.getPhoneNumber() + "@graphicnewsplus.com.gh";
    response.result["createdAt"] =
        trantor::Date::now().toCustomFormattedString("%d-%b-%Y %H:%M:%S");
    co_return response;

  } catch (const DrogonDbException &e) {
    LOG_ERROR << "[onboardSubscriberAsync] UNHANDLED DB EXCEPTION — "
              << e.base().what() << " (clientId=" << clientId
              << " phoneNumber=" << dto.getPhoneNumber() << ")";
    gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Onboarding failed or unauthorized";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<::gnp::dto::BaseApiResponse> CommercialPartnerService::getSubscriberSubscriptionSummary(
    std::string partnerId, std::string userId) {
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<UserSubscriptions> subMapper(dbClient);
  CoroMapper<drogon_model::Gnp::SubscriptionRenewalHistory> renewalMapper(
      dbClient);
  CoroMapper<CommercialPartners> partnerMapper(dbClient);

  try {
    // 1. Fetch UserSubscription summary
    auto subscriptions =
        co_await subMapper.findBy(Criteria(UserSubscriptions::Cols::_user_id,
                                           CompareOperator::EQ, userId) &&
                                  Criteria(UserSubscriptions::Cols::_partner_id,
                                           CompareOperator::EQ, partnerId) &&
                                  Criteria(UserSubscriptions::Cols::_is_active,
                                           CompareOperator::EQ, true));

    if (subscriptions.empty()) {
      dto::BaseApiResponse response;
      response.success = true;
      response.result["subscriptionSummary"] = Json::nullValue;
      response.result["renewalHistory"] = Json::arrayValue;
      co_return response;
    }

    auto subscription = subscriptions.front();

    // 2. Fetch Partner for Name
    auto partner = co_await partnerMapper.findOne(Criteria(
        CommercialPartners::Cols::_id, CompareOperator::EQ, partnerId));

    // 3. Fetch Renewal History
    auto renewals =
        co_await renewalMapper
            .orderBy(drogon_model::Gnp::SubscriptionRenewalHistory::Cols::
                         _created_at,
                     SortOrder::DESC)
            .findBy(Criteria(drogon_model::Gnp::SubscriptionRenewalHistory::
                                 Cols::_user_id,
                             CompareOperator::EQ, userId) &&
                    Criteria(drogon_model::Gnp::SubscriptionRenewalHistory::
                                 Cols::_partner_id,
                             CompareOperator::EQ, partnerId));

    // 4. Build JSON Response
    Json::Value result;

    Json::Value summary;
    summary["package"] =
        subscription.getValueOfSubscriptionPlanDescription() + " (Assigned)";
    summary["dueDate"] =
        subscription.getValueOfEndDate().toCustomFormattedString("%b %d, %Y");
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

      // Amount format: $0.00 (Covered by {Partner Name})
      std::string amountStr = renewal.getValueOfAmountPaid();
      std::string partnerName = partner.getValueOfName();
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

drogon::Task<::gnp::dto::BaseApiResponse> CommercialPartnerService::checkSubscriberStatus(
    const std::string &clientId, const std::string &clientSecret,
    const std::string &phoneNumber) {
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<drogon_model::Gnp::CommercialPartnerApiKeys> apiKeyMapper(
      dbClient);

  try {
    // 1. Verify API Key
    auto apiKey = co_await apiKeyMapper.findOne(
        Criteria(drogon_model::Gnp::CommercialPartnerApiKeys::Cols::_client_id,
                 CompareOperator::EQ, clientId) &&
        Criteria(drogon_model::Gnp::CommercialPartnerApiKeys::Cols::_is_active,
                 CompareOperator::EQ, true));

    if (!bcrypt::validatePassword(clientSecret,
                                  apiKey.getValueOfClientSecretHash())) {
      gnp::dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "Invalid ClientSecret";
      errorResponse.error["code"] = constants::ERR_UNAUTHORIZED;
      co_return errorResponse;
    }

    // Update last used at
    apiKey.setLastUsedAt(trantor::Date::now());
    co_await apiKeyMapper.update(apiKey);

    // 2. Find User
    CoroMapper<drogon_model::Gnp::Users> userMapper(dbClient);
    Criteria userCriteria =
        Criteria(drogon_model::Gnp::Users::Cols::_phone_number,
                 CompareOperator::EQ, phoneNumber) &&
        Criteria(drogon_model::Gnp::Users::Cols::_partner_id,
                 CompareOperator::EQ, apiKey.getValueOfPartnerId());

    auto users = co_await userMapper.findBy(userCriteria);
    if (users.empty()) {
      gnp::dto::BaseApiResponse response;
      response.success = true; // Request successful, but user doesn't exist
      response.message = "Subscriber not found";
      response.result["exists"] = false;
      response.result["status"] = "None";
      co_return response;
    }

    auto user = users[0];

    // 3. Check Subscription Status
    CoroMapper<drogon_model::Gnp::UserSubscriptions> subMapper(dbClient);
    Criteria subCriteria =
        Criteria(drogon_model::Gnp::UserSubscriptions::Cols::_user_id,
                 CompareOperator::EQ, user.getValueOfId()) &&
        Criteria(drogon_model::Gnp::UserSubscriptions::Cols::_is_active,
                 CompareOperator::EQ, true);

    auto subs = co_await subMapper.findBy(subCriteria);

    gnp::dto::BaseApiResponse response;
    response.success = true;
    response.message = "Subscriber status retrieved successfully";

    Json::Value subscriber;
    subscriber["firstName"] = user.getValueOfFirstName();
    subscriber["lastName"] = user.getValueOfLastName();
    subscriber["email"] = user.getValueOfEmail();
    subscriber["phoneNumber"] = user.getValueOfPhoneNumber();

    Json::Value subscription;
    subscription["exists"] = true;
    subscription["subscriptionPlan"] =
        "Corporate (Daily Graphic & Graphic Business)";
    subscription["subscriptionType"] = "Bundle";
    subscription["billingCycle"] = "Monthly";
    // subscription["isActive"] = !subs.empty();
    // subscription["status"] = !subs.empty() ? "Active" : "Inactive";

    if (!subs.empty()) {
      auto sub = subs[0];
      subscription["subscriptionPlan"] =
          sub.getValueOfSubscriptionPlanDescription();
      subscription["subscriptionId"] = sub.getValueOfSubscriptionIdentifier();
    }

    response.result["subscriber"] = subscriber;
    response.result["subscription"] = subscription;

    co_return response;

  } catch (const DrogonDbException &e) {
    gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to check subscriber status";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<::gnp::dto::BaseApiResponse> CommercialPartnerService::retrieveSubscriberDetails(
    const std::string &clientId, const std::string &clientSecret,
    const std::string &phoneNumber) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<drogon_model::Gnp::CommercialPartnerApiKeys> apiKeyMapper(
      dbClient);

  try {
    // 1. Verify API Key
    auto apiKey = co_await apiKeyMapper.findOne(
        Criteria(drogon_model::Gnp::CommercialPartnerApiKeys::Cols::_client_id,
                 CompareOperator::EQ, clientId) &&
        Criteria(drogon_model::Gnp::CommercialPartnerApiKeys::Cols::_is_active,
                 CompareOperator::EQ, true));

    if (!bcrypt::validatePassword(clientSecret,
                                  apiKey.getValueOfClientSecretHash())) {
      gnp::dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "Invalid ClientSecret";
      errorResponse.error["code"] = constants::ERR_UNAUTHORIZED;
      co_return errorResponse;
    }

    // Update last used at
    apiKey.setLastUsedAt(trantor::Date::now());
    co_await apiKeyMapper.update(apiKey);

    // 2. Find User
    CoroMapper<drogon_model::Gnp::Users> userMapper(dbClient);
    Criteria userCriteria =
        Criteria(drogon_model::Gnp::Users::Cols::_phone_number,
                 CompareOperator::EQ, phoneNumber) &&
        Criteria(drogon_model::Gnp::Users::Cols::_partner_id,
                 CompareOperator::EQ, apiKey.getValueOfPartnerId());

    auto users = co_await userMapper.findBy(userCriteria);
    if (users.empty()) {
      gnp::dto::BaseApiResponse response;
      response.success = false;
      response.message = "Subscriber not found";
      response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
      co_return response;
    }

    auto user = users[0];

    // 3. Construct response with details
    gnp::dto::BaseApiResponse response;
    response.success = true;
    response.message = "Subscriber details retrieved successfully";

    Json::Value data;
    data["firstName"] = user.getValueOfFirstName();
    data["lastName"] = user.getValueOfLastName();
    data["email"] = user.getValueOfEmail();
    data["phoneNumber"] = user.getValueOfPhoneNumber();
    data["createdAt"] = user.getValueOfCreatedAt().toDbString();

    response.result = data;
    co_return response;

  } catch (const DrogonDbException &e) {
    gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to retrieve subscriber details";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<::gnp::dto::BaseApiResponse> CommercialPartnerService::getPartnerOverviewStats(
    const std::string &partnerId) {

  auto dbClient = drogon::app().getDbClient();
  try {
    auto now = trantor::Date::now();
    auto periodStart = now.after(-30 * 24 * 3600);
    auto prevPeriodStart = now.after(-60 * 24 * 3600);

    auto calculateChange = [](double current, double previous) -> double {
      if (previous == 0)
        return current > 0 ? 100.0 : 0.0;
      return ((current - previous) / previous) * 100.0;
    };

    auto getChangeType = [](double change) -> std::string {
      if (change > 0)
        return "increase";
      if (change < 0)
        return "decrease";
      return "neutral";
    };

    // 1. Get Active Members (Snapshot count + new members trend)
    CoroMapper<UserSubscriptions> subMapper(dbClient);
    auto activeMembers =
        co_await subMapper.count(Criteria(UserSubscriptions::Cols::_partner_id,
                                          CompareOperator::EQ, partnerId) &&
                                 Criteria(UserSubscriptions::Cols::_is_active,
                                          CompareOperator::EQ, true));

    std::string newMembersSql =
        "SELECT "
        "(SELECT COUNT(*) FROM user_subscriptions WHERE partner_id = $1 AND "
        "created_at >= $2) as current, "
        "(SELECT COUNT(*) FROM user_subscriptions WHERE partner_id = $1 AND "
        "created_at >= $3 AND created_at < $2) as previous";

    auto membersRes = co_await dbClient->execSqlCoro(
        newMembersSql, partnerId, periodStart, prevPeriodStart);
    long currentNewMembers =
        membersRes.size() > 0 && !membersRes[0]["current"].isNull()
            ? membersRes[0]["current"].as<long>()
            : 0;
    long prevNewMembers =
        membersRes.size() > 0 && !membersRes[0]["previous"].isNull()
            ? membersRes[0]["previous"].as<long>()
            : 0;
    double activeMembersChange =
        calculateChange(currentNewMembers, prevNewMembers);

    // 2. Get Total Quota & Remaining
    CoroMapper<CommercialPartners> partnerMapper(dbClient);
    auto partner = co_await partnerMapper.findByPrimaryKey(partnerId);
    auto totalQuota = partner.getValueOfSubscriberQuota();
    auto remainingQuota = partner.getValueOfRemainingQuota();

    // 3. Get Active Sessions (Snapshot count + sessions started trend)
    CoroMapper<UserSessions> sessionMapper(dbClient);
    auto activeSessions = co_await sessionMapper.count(
        Criteria(UserSessions::Cols::_partner_id, CompareOperator::EQ,
                 partnerId) &&
        Criteria(UserSessions::Cols::_is_active, CompareOperator::EQ, true));

    std::string sessionsChangeSql =
        "SELECT "
        "(SELECT COUNT(*) FROM user_sessions WHERE partner_id = $1 AND "
        "session_start >= $2) as current, "
        "(SELECT COUNT(*) FROM user_sessions WHERE partner_id = $1 AND "
        "session_start >= $3 AND session_start < $2) as previous";

    auto sessionsRes = co_await dbClient->execSqlCoro(
        sessionsChangeSql, partnerId, periodStart, prevPeriodStart);
    long currentSessions =
        sessionsRes.size() > 0 && !sessionsRes[0]["current"].isNull()
            ? sessionsRes[0]["current"].as<long>()
            : 0;
    long prevSessions =
        sessionsRes.size() > 0 && !sessionsRes[0]["previous"].isNull()
            ? sessionsRes[0]["previous"].as<long>()
            : 0;
    double activeSessionsChange =
        calculateChange(currentSessions, prevSessions);

    // 4. Get Engagement Rate Change
    std::string activeReadersSql =
        "SELECT "
        "(SELECT COUNT(DISTINCT user_id) FROM publication_reads WHERE "
        "partner_id = $1 AND read_at >= $2) as current, "
        "(SELECT COUNT(DISTINCT user_id) FROM publication_reads WHERE "
        "partner_id = $1 AND read_at >= $3 AND read_at < $2) as previous";

    auto readersRes = co_await dbClient->execSqlCoro(
        activeReadersSql, partnerId, periodStart, prevPeriodStart);
    long currentActiveReaders =
        readersRes.size() > 0 && !readersRes[0]["current"].isNull()
            ? readersRes[0]["current"].as<long>()
            : 0;
    long prevActiveReaders =
        readersRes.size() > 0 && !readersRes[0]["previous"].isNull()
            ? readersRes[0]["previous"].as<long>()
            : 0;

    auto totalMembers = co_await subMapper.count(Criteria(
        UserSubscriptions::Cols::_partner_id, CompareOperator::EQ, partnerId));

    // Estimate members 30 days ago
    long prevTotalMembers = totalMembers - currentNewMembers;
    if (prevTotalMembers < 0)
      prevTotalMembers = 0;

    double currentEngagementRate = 0.0;
    if (totalMembers > 0) {
      currentEngagementRate =
          (double)currentActiveReaders / totalMembers * 100.0;
    }

    double prevEngagementRate = 0.0;
    if (prevTotalMembers > 0) {
      prevEngagementRate = (double)prevActiveReaders / prevTotalMembers * 100.0;
    }

    double engagementRateChange =
        calculateChange(currentEngagementRate, prevEngagementRate);

    gnp::dto::BaseApiResponse response;
    response.success = true;
    response.message = "Overview stats retrieved successfully";

    Json::Value data;
    data["activeMembers"] = (Json::UInt64)activeMembers;
    data["activeMembersChange"] = std::round(activeMembersChange * 10) / 10.0;
    data["activeMembersChangeType"] = getChangeType(activeMembersChange);

    data["totalQuota"] = (Json::UInt64)totalQuota;
    data["remainingQuota"] = (Json::UInt64)remainingQuota;

    data["engagementRate"] = std::round(currentEngagementRate * 10) / 10.0;
    data["engagementRateChange"] = std::round(engagementRateChange * 10) / 10.0;
    data["engagementRateChangeType"] = getChangeType(engagementRateChange);

    data["activeSessions"] = (Json::UInt64)activeSessions;
    data["activeSessionsChange"] = std::round(activeSessionsChange * 10) / 10.0;
    data["activeSessionsChangeType"] = getChangeType(activeSessionsChange);

    response.result = data;
    co_return response;

  } catch (const DrogonDbException &e) {
    gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to retrieve overview stats";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<::gnp::dto::BaseApiResponse> CommercialPartnerService::getPartnerEngagementReport(
    const std::string &partnerId, const std::string &period) {

  auto dbClient = drogon::app().getDbClient();
  try {
    int days = (period == "30d") ? 30 : 7;
    auto now = trantor::Date::now();
    auto periodStart = now.after(-days * 24 * 3600);
    auto prevPeriodStart = now.after(-2 * days * 24 * 3600);

    // 1. Total Reads
    std::string readsSql = "SELECT "
                           "(SELECT COUNT(*) FROM publication_reads WHERE "
                           "partner_id = $1 AND read_at >= $2) as current, "
                           "(SELECT COUNT(*) FROM publication_reads WHERE "
                           "partner_id = $1 AND read_at >= $3 AND read_at < "
                           "$2) as previous";
    auto readsRes = co_await dbClient->execSqlCoro(
        readsSql, partnerId, periodStart, prevPeriodStart);
    long currentReads = readsRes[0]["current"].as<long>();
    long prevReads = readsRes[0]["previous"].as<long>();

    // 2. Avg Session Duration
    std::string sessionSql =
        "SELECT "
        "(SELECT AVG(duration_seconds) FROM user_sessions WHERE partner_id = "
        "$1 AND session_start >= $2) as current, "
        "(SELECT AVG(duration_seconds) FROM user_sessions WHERE partner_id = "
        "$1 AND session_start >= $3 AND session_start < $2) as previous";
    auto sessionRes = co_await dbClient->execSqlCoro(
        sessionSql, partnerId, periodStart, prevPeriodStart);
    double currentAvgSec = sessionRes[0]["current"].isNull()
                               ? 0.0
                               : sessionRes[0]["current"].as<double>();
    double prevAvgSec = sessionRes[0]["previous"].isNull()
                            ? 0.0
                            : sessionRes[0]["previous"].as<double>();

    // 3. New Members Onboarded
    std::string membersSql =
        "SELECT "
        "(SELECT COUNT(*) FROM users WHERE partner_id = $1 AND created_at >= "
        "$2) as current, "
        "(SELECT COUNT(*) FROM users WHERE partner_id = $1 AND created_at >= "
        "$3 AND created_at < $2) as previous";
    auto membersRes = co_await dbClient->execSqlCoro(
        membersSql, partnerId, periodStart, prevPeriodStart);
    long currentMembers = membersRes[0]["current"].as<long>();
    long prevMembers = membersRes[0]["previous"].as<long>();

    // 4. Active Readers
    std::string activeReadersSql =
        "SELECT "
        "(SELECT COUNT(DISTINCT user_id) FROM publication_reads WHERE "
        "partner_id = $1 AND read_at >= $2) as current, "
        "(SELECT COUNT(DISTINCT user_id) FROM publication_reads WHERE "
        "partner_id = $1 AND read_at >= $3 AND read_at < $2) as previous";
    auto activeRes = co_await dbClient->execSqlCoro(
        activeReadersSql, partnerId, periodStart, prevPeriodStart);
    long currentActive = activeRes[0]["current"].as<long>();
    long prevActive = activeRes[0]["previous"].as<long>();

    auto calculateChange = [](long current, long previous) -> double {
      if (previous == 0)
        return current > 0 ? 100.0 : 0.0;
      return ((double)(current - previous) / previous) * 100.0;
    };

    auto formatDuration = [](double seconds) -> std::string {
      int s = (int)seconds;
      int m = s / 60;
      s = s % 60;
      return std::to_string(m) + "m " + std::to_string(s) + "s";
    };

    gnp::dto::BaseApiResponse response;
    response.success = true;
    response.message = "Engagement report retrieved successfully";

    Json::Value result;

    Json::Value totalReads;
    totalReads["value"] =
        std::to_string(currentReads / 1000.0).substr(0, 4) + "k";
    if (currentReads < 1000)
      totalReads["value"] = (Json::UInt64)currentReads;
    totalReads["change"] = calculateChange(currentReads, prevReads);
    result["totalReads"] = totalReads;

    Json::Value avgDuration;
    avgDuration["value"] = formatDuration(currentAvgSec);
    avgDuration["change"] =
        calculateChange((long)currentAvgSec, (long)prevAvgSec);
    result["avgSessionDuration"] = avgDuration;

    Json::Value newMembers;
    newMembers["value"] = (Json::UInt64)currentMembers;
    newMembers["change"] = calculateChange(currentMembers, prevMembers);
    result["newMembersOnboarded"] = newMembers;

    Json::Value activeReaders;
    activeReaders["value"] = (Json::UInt64)currentActive;
    activeReaders["change"] = calculateChange(currentActive, prevActive);
    result["activeReaders"] = activeReaders;

    response.result = result;
    co_return response;

  } catch (const DrogonDbException &e) {
    gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to retrieve engagement report";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<::gnp::dto::BaseApiResponse> CommercialPartnerService::getPartnerAnalyticsCharts(
    const std::string &partnerId, const std::string &period) {
  auto dbClient = drogon::app().getDbClient();
  try {
    int days = (period == "30d") ? 30 : 7;
    auto now = trantor::Date::now();
    auto periodStart = now.after(-days * 24 * 3600);

    // 1. Engagement Over Time (Reads by Day)
    std::string timeSql = "SELECT DATE(read_at) as read_date, COUNT(*) as "
                          "read_count FROM publication_reads "
                          "WHERE partner_id = $1 AND read_at >= $2 "
                          "GROUP BY read_date ORDER BY read_date ASC";
    auto timeRes =
        co_await dbClient->execSqlCoro(timeSql, partnerId, periodStart);

    Json::Value engagementOverTime = Json::arrayValue;
    for (const auto &row : timeRes) {
      Json::Value item;
      item["date"] = row["read_date"].as<std::string>();
      item["readsCount"] = (Json::UInt64)row["read_count"].as<long>();
      engagementOverTime.append(item);
    }

    // 2. Top Publications
    std::string topPubsSql =
        "SELECT p.name, COUNT(pr.id) as read_count "
        "FROM publication_reads pr "
        "JOIN publications p ON pr.publication_id = p.id "
        "WHERE pr.partner_id = $1 AND pr.read_at >= $2 "
        "GROUP BY p.name ORDER BY read_count DESC LIMIT 5";
    auto topRes =
        co_await dbClient->execSqlCoro(topPubsSql, partnerId, periodStart);

    // Total reads in period for % calculation
    long totalReads = 0;
    for (const auto &row : topRes)
      totalReads += row["read_count"].as<long>();

    Json::Value topPublications = Json::arrayValue;
    for (const auto &row : topRes) {
      Json::Value item;
      item["title"] = row["name"].as<std::string>();
      long reads = row["read_count"].as<long>();
      item["reads"] = (Json::UInt64)reads;
      item["percentage"] =
          totalReads > 0 ? (int)((double)reads / totalReads * 100) : 0;
      topPublications.append(item);
    }

    dto::BaseApiResponse response;
    response.success = true;
    response.message = "Analytics charts retrieved successfully";

    Json::Value result;
    result["engagementOverTime"] = engagementOverTime;
    result["topPublications"] = topPublications;

    response.result = result;
    co_return response;

  } catch (const DrogonDbException &e) {
    gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to retrieve analytics charts";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}


  drogon::Task<::gnp::dto::BaseApiResponse> CommercialPartnerService::getPartnerInvoiceGenerationReport(const gnp::dto::ReportDto &dto) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<CommercialPartners> partnerMapper(dbClient);

  try {
    // Validate partner ID
    if (dto.getPartnerId().empty()) {
      gnp::dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "Partner ID is required";
      errorResponse.error["code"] = constants::ERR_VALIDATION;
      co_return errorResponse;
    }

    auto partner = co_await partnerMapper.findByPrimaryKey(dto.getPartnerId());

    // Deterministic invoice number: INV-GNP-<year>-<NNNN> (year-scoped sequence).
    const std::string yearStr =
        trantor::Date::now().toCustomFormattedString("%Y");
    auto seqResult = co_await dbClient->execSqlCoro(
        "SELECT COUNT(*) AS cnt FROM partner_invoices "
        "WHERE partner_id = $1 AND created_at >= $2::date "
        "  AND created_at < ($2::date + INTERVAL '1 year')",
        dto.getPartnerId(), yearStr + "-01-01");
    const long long sequence =
        seqResult[0]["cnt"].isNull() ? 1 : seqResult[0]["cnt"].as<long long>() + 1;
    char invoiceNoBuf[64];
    std::snprintf(invoiceNoBuf, sizeof(invoiceNoBuf), "INV-GNP-%s-%04lld",
                  yearStr.c_str(), sequence);
    const std::string invoiceNo(invoiceNoBuf);

    // Query partner_invoices table with aggregated values in the billing window
    std::string sql =
        "SELECT "
        "SUM(invoice_amount) as total_invoice_amount, "
        "SUM(balance) as total_balance "
        "FROM partner_invoices "
        "WHERE partner_id = $1 AND created_at >= $2 AND created_at <= $3";

    auto result = co_await dbClient->execSqlCoro(sql, dto.getPartnerId(), dto.getStartDate(), dto.getEndDate());

    gnp::dto::BaseApiResponse response;
    response.success = true;
    response.message = "Invoice generation report retrieved successfully";

    Json::Value invoiceData;

    invoiceData["invoiceNo"] = invoiceNo;
    invoiceData["date"] = trantor::Date::now().toCustomFormattedString("%d/%m/%Y");
    invoiceData["dueDate"] = trantor::Date::now().after(14 * 24 * 3600).toCustomFormattedString("%d/%m/%Y");

    // Billed-to information (§2.3 Endpoint 1.3 payload contract)
    invoiceData["partnerId"] = dto.getPartnerId();
    invoiceData["partnerName"] = partner.getValueOfName();
    invoiceData["partnerAddress"] = "";
    invoiceData["partnerEmail"] = partner.getValueOfBillingEmail();
    invoiceData["billedTo"]["name"] = partner.getValueOfName();
    invoiceData["billedTo"]["email"] = partner.getValueOfBillingEmail();

    invoiceData["billingPeriod"] = dto.getStartDate() + " - " + dto.getEndDate();

    // Extract aggregated values
    double totalInvoiceAmount = result[0]["total_invoice_amount"].isNull() ? 0.0 : result[0]["total_invoice_amount"].as<double>();
    double totalBalance = result[0]["total_balance"].isNull() ? 0.0 : result[0]["total_balance"].as<double>();

    // Parse the contracted per-seat rate used to derive line quantities.
    double unitPrice = 0.0;
    try {
      unitPrice = std::stod(partner.getValueOfCostPerHead());
    } catch (...) {
      unitPrice = 0.0;
    }

    // Seat count for the lead line item.
    auto seatResult = co_await dbClient->execSqlCoro(
        "SELECT COUNT(*) AS seats FROM users "
        "WHERE partner_id = $1 AND is_active = TRUE",
        dto.getPartnerId());
    double seatCount = seatResult[0]["seats"].isNull()
                           ? 0.0
                           : (double)seatResult[0]["seats"].as<long long>();

    Json::Value items = Json::arrayValue;

    if (totalInvoiceAmount == 0) {
      Json::Value emptyItem;
      emptyItem["description"] = "No invoices found for this period";
      emptyItem["details"] = "Adjust the billing window and try again";
      emptyItem["quantity"] = 0;
      emptyItem["unitPrice"] = 0;
      emptyItem["amount"] = 0.0;
      items.append(emptyItem);
    } else {
      Json::Value seatsItem;
      seatsItem["description"] =
          "Corporate Digital Newsstand Seats — " + partner.getValueOfName();
      seatsItem["details"] = std::to_string((long long)seatCount) +
                             " active employee seats × contracted enterprise rate";
      seatsItem["quantity"] = seatCount;
      seatsItem["unitPrice"] = unitPrice;
      seatsItem["amount"] = totalInvoiceAmount;
      items.append(seatsItem);
    }

    invoiceData["items"] = items;

    // ---- Ghana statutory tax formula ----------------------------------
    // totalDue = subtotal + vat(15%) + getflNhil(5%) - discount
    const double subtotal = totalInvoiceAmount;
    const double vat = subtotal * 0.15;
    const double getflNhil = subtotal * 0.05;
    const double discount = subtotal * 0.05;
    const double totalDue = subtotal + vat + getflNhil - discount;

    invoiceData["subtotal"] = subtotal;
    invoiceData["vat"] = vat;
    invoiceData["getflNhil"] = getflNhil;
    invoiceData["discount"] = discount;
    invoiceData["totalDue"] = totalDue;
    invoiceData["totalBalance"] = totalBalance;
    invoiceData["outstandingBalance"] = totalInvoiceAmount - totalBalance;

    // ---- Billing charts (§2.3 Endpoint 1.3 `charts`) ----------------------
    Json::Value charts = Json::arrayValue;

    {
      // partner_billing_comparison — net subtotal vs statutory taxes per partner
      auto cmpResult = co_await dbClient->execSqlCoro(
          "SELECT COALESCE(cp.name, pi.partner_name, 'Unassigned') AS partner_name, "
          "       COALESCE(SUM(pi.invoice_amount::NUMERIC), 0) AS subtotal "
          "FROM partner_invoices pi "
          "LEFT JOIN commercial_partners cp ON cp.id = pi.partner_id "
          "WHERE pi.created_at >= $1 AND pi.created_at <= $2 "
          "GROUP BY 1 ORDER BY subtotal DESC LIMIT 5",
          dto.getStartDate(), dto.getEndDate());

      Json::Value labels = Json::arrayValue;
      Json::Value netSeries = Json::arrayValue;
      Json::Value taxSeries = Json::arrayValue;
      for (const auto &row : cmpResult) {
        labels.append(row["partner_name"].asString());
        double sub = row["subtotal"].isNull() ? 0.0 : row["subtotal"].as<double>();
        netSeries.append(sub);
        taxSeries.append(sub * 0.20);
      }

      Json::Value chart;
      chart["id"] = "partner_billing_comparison";
      chart["title"] = "Partner Invoice Totals (Net vs Statutory Taxes)";
      chart["type"] = "stacked_bar";
      chart["labels"] = labels;
      chart["yAxisLabel"] = "GHS";
      Json::Value series = Json::arrayValue;
      Json::Value netObj;
      netObj["name"] = "Net Subtotal";
      netObj["data"] = netSeries;
      series.append(netObj);
      Json::Value taxObj;
      taxObj["name"] = "VAT + GETFund/NHIL (20%)";
      taxObj["data"] = taxSeries;
      series.append(taxObj);
      chart["series"] = series;
      charts.append(chart);
    }

    {
      // invoice_collection_status — receivables aging breakdown
      auto agingResult = co_await dbClient->execSqlCoro(
          "SELECT "
          "COALESCE(SUM(pi.balance::NUMERIC) FILTER (WHERE pi.status = 'Paid'), 0) AS paid_on_time, "
          "COALESCE(SUM(pi.balance::NUMERIC) FILTER (WHERE pi.status <> 'Paid' "
          "  AND pi.due_date >= NOW()), 0) AS current_due, "
          "COALESCE(SUM(pi.balance::NUMERIC) FILTER (WHERE pi.status <> 'Paid' "
          "  AND pi.due_date < NOW() AND pi.due_date >= NOW() - INTERVAL '30 days'), 0) AS overdue_30, "
          "COALESCE(SUM(pi.balance::NUMERIC) FILTER (WHERE pi.status <> 'Paid' "
          "  AND pi.due_date < NOW() - INTERVAL '30 days'), 0) AS overdue_older "
          "FROM partner_invoices pi "
          "WHERE pi.created_at >= $1 AND pi.created_at <= $2",
          dto.getStartDate(), dto.getEndDate());

      Json::Value itemsJson = Json::arrayValue;
      auto addAging = [&itemsJson](const char *name, double value) {
        Json::Value item;
        item["name"] = name;
        item["value"] = value;
        itemsJson.append(item);
      };
      addAging("Paid on Time", agingResult[0]["paid_on_time"].isNull()
                                  ? 0.0
                                  : agingResult[0]["paid_on_time"].as<double>());
      addAging("Current (Due < 14d)", agingResult[0]["current_due"].isNull()
                                          ? 0.0
                                          : agingResult[0]["current_due"].as<double>());
      addAging("Overdue (15–30d)", agingResult[0]["overdue_30"].isNull()
                                       ? 0.0
                                       : agingResult[0]["overdue_30"].as<double>());
      addAging("Overdue (30d+)", agingResult[0]["overdue_older"].isNull()
                                     ? 0.0
                                     : agingResult[0]["overdue_older"].as<double>());

      Json::Value chart;
      chart["id"] = "invoice_collection_status";
      chart["title"] = "Receivables Aging Breakdown";
      chart["type"] = "donut";
      chart["items"] = itemsJson;
      charts.append(chart);
    }

    invoiceData["charts"] = charts;

    response.result = invoiceData;
    co_return response;

  } catch (const DrogonDbException &e) {
    gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to retrieve invoice generation report";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}




  drogon::Task<::gnp::dto::BaseApiResponse> CommercialPartnerService::sendPartnerInvoiceByMail(const gnp::dto::ReportDto &dto) {
  LOG_INFO << "[sendPartnerInvoiceByMail] START — partnerId=" << dto.getPartnerId()
           << " startDate=" << dto.getStartDate() << " endDate=" << dto.getEndDate();

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<CommercialPartners> partnerMapper(dbClient);

  try {
    // STEP 1: Validate partner ID
    LOG_INFO << "[sendPartnerInvoiceByMail] STEP 1 — Validating partner ID";
    if (dto.getPartnerId().empty()) {
      LOG_ERROR << "[sendPartnerInvoiceByMail] STEP 1 FAILED — Partner ID is empty";
      gnp::dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "Partner ID is required";
      errorResponse.error["code"] = constants::ERR_VALIDATION;
      co_return errorResponse;
    }
    LOG_INFO << "[sendPartnerInvoiceByMail] STEP 1 — Partner ID validated successfully";

    // STEP 2: Fetch partner details
    LOG_INFO << "[sendPartnerInvoiceByMail] STEP 2 — Fetching partner details for partnerId=" << dto.getPartnerId();
    auto partner = co_await partnerMapper.findByPrimaryKey(dto.getPartnerId());
    LOG_INFO << "[sendPartnerInvoiceByMail] STEP 2 — Partner found: name=" << partner.getValueOfName()
             << " billingEmail=" << partner.getValueOfBillingEmail()
             << " costPerHead=" << partner.getValueOfCostPerHead();

    // STEP 3: Query invoice data
    LOG_INFO << "[sendPartnerInvoiceByMail] STEP 3 — Querying invoice data from partner_invoices table";
    std::string sql =
        "SELECT "
        "SUM(invoice_amount) as total_invoice_amount, "
        "SUM(balance) as total_balance "
        "FROM partner_invoices "
        "WHERE partner_id = $1 AND created_at >= $2 AND created_at <= $3";

    auto result = co_await dbClient->execSqlCoro(sql, dto.getPartnerId(), dto.getStartDate(), dto.getEndDate());
    LOG_INFO << "[sendPartnerInvoiceByMail] STEP 3 — Query executed, result rows=" << result.size();

    // STEP 4: Extract aggregated values
    LOG_INFO << "[sendPartnerInvoiceByMail] STEP 4 — Extracting aggregated values";
    double totalInvoiceAmount = result[0]["total_invoice_amount"].isNull() ? 0.0 : result[0]["total_invoice_amount"].as<double>();
    double totalBalance = result[0]["total_balance"].isNull() ? 0.0 : result[0]["total_balance"].as<double>();
    LOG_INFO << "[sendPartnerInvoiceByMail] STEP 4 — totalInvoiceAmount=" << totalInvoiceAmount
             << " totalBalance=" << totalBalance;

    // STEP 5: Parse unit price
    LOG_INFO << "[sendPartnerInvoiceByMail] STEP 5 — Parsing unit price";
    double unitPrice = 0.0;
    try {
      unitPrice = std::stod(partner.getValueOfCostPerHead());
      LOG_INFO << "[sendPartnerInvoiceByMail] STEP 5 — unitPrice=" << unitPrice;
    } catch (const std::exception &e) {
      LOG_ERROR << "[sendPartnerInvoiceByMail] STEP 5 — Failed to parse unitPrice: " << e.what();
      unitPrice = 0.0;
    }

    // STEP 6: Calculate quantity
    LOG_INFO << "[sendPartnerInvoiceByMail] STEP 6 — Calculating quantity";
    int quantity = 0;
    if (unitPrice > 0 && totalInvoiceAmount > 0) {
      quantity = (int)std::round(totalInvoiceAmount / unitPrice);
      LOG_INFO << "[sendPartnerInvoiceByMail] STEP 6 — quantity=" << quantity;
    } else {
      LOG_WARN << "[sendPartnerInvoiceByMail] STEP 6 — unitPrice=" << unitPrice
               << " or totalInvoiceAmount=" << totalInvoiceAmount << " is zero, quantity remains 0";
    }

    // STEP 7: Generate invoice data
    LOG_INFO << "[sendPartnerInvoiceByMail] STEP 7 — Generating invoice data";
    std::string yearStr = trantor::Date::now().toCustomFormattedString("%Y");
    auto seqResult = co_await dbClient->execSqlCoro(
        "SELECT COUNT(*) AS cnt FROM partner_invoices "
        "WHERE partner_id = $1 AND created_at >= $2::date "
        "  AND created_at < ($2::date + INTERVAL '1 year')",
        dto.getPartnerId(), yearStr + "-01-01");
    long long sequence =
        seqResult[0]["cnt"].isNull() ? 1 : seqResult[0]["cnt"].as<long long>() + 1;
    char invoiceNoBuf[64];
    std::snprintf(invoiceNoBuf, sizeof(invoiceNoBuf), "INV-GNP-%s-%04lld",
                  yearStr.c_str(), sequence);
    std::string invoiceNo(invoiceNoBuf);
    std::string dateStr = trantor::Date::now().toCustomFormattedString("%d/%m/%Y");
    std::string dueDateStr = trantor::Date::now().after(14 * 24 * 3600).toCustomFormattedString("%d/%m/%Y");
    std::string billingPeriod = dto.getStartDate() + " - " + dto.getEndDate();
    LOG_INFO << "[sendPartnerInvoiceByMail] STEP 7 — invoiceNo=" << invoiceNo
             << " dateStr=" << dateStr << " dueDateStr=" << dueDateStr
             << " billingPeriod=" << billingPeriod;

    // STEP 8: Calculate VAT and total (Ghana statutory formula:
    // totalDue = subtotal + vat(15%) + getflNhil(5%) - discount)
    LOG_INFO << "[sendPartnerInvoiceByMail] STEP 8 — Calculating VAT and total due";
    double subtotal = totalInvoiceAmount;
    double vat = subtotal * 0.15;
    double getflNhil = subtotal * 0.05;
    double discount = subtotal * 0.05;
    double totalDue = subtotal + vat + getflNhil - discount;
    LOG_INFO << "[sendPartnerInvoiceByMail] STEP 8 — subtotal=" << subtotal
             << " vat=" << vat << " getflNhil=" << getflNhil
             << " discount=" << discount << " totalDue=" << totalDue;

    // STEP 9: Build email HTML
    LOG_INFO << "[sendPartnerInvoiceByMail] STEP 9 — Building email HTML body";
    auto formatCurrency = [](double amount) -> std::string {
      std::ostringstream ss;
      ss << std::fixed << std::setprecision(2) << amount;
      return ss.str();
    };

    std::string emailBody = R"html(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<style>
  * { margin: 0; padding: 0; box-sizing: border-box; }
  body {
    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
    background-color: #f3f4f6;
    padding: 40px 20px;
  }
  .container {
    max-width: 800px;
    margin: 0 auto;
    background: #ffffff;
    border-radius: 12px;
    box-shadow: 0 4px 6px -1px rgba(0,0,0,0.1), 0 2px 4px -1px rgba(0,0,0,0.06);
    overflow: hidden;
  }
  .header {
    background: #ffffff;
    padding: 30px 40px;
    border-bottom: 2px solid #e5e7eb;
    display: flex;
    justify-content: space-between;
    align-items: center;
  }
  .header-left {
    display: flex;
    align-items: center;
    gap: 12px;
  }
  .logo {
    font-size: 28px;
    font-weight: 700;
    color: #1f2937;
    letter-spacing: -0.5px;
  }
  .logo span {
    color: #dc2626;
  }
  .invoice-title {
    font-size: 14px;
    font-weight: 600;
    color: #6b7280;
    letter-spacing: 1px;
    text-transform: uppercase;
  }
  .invoice-title h2 {
    font-size: 32px;
    font-weight: 700;
    color: #1f2937;
    letter-spacing: 0;
    margin-top: 2px;
  }
  .body-content {
    padding: 30px 40px 40px;
  }
  .info-grid {
    display: flex;
    justify-content: space-between;
    margin-bottom: 30px;
    padding-bottom: 20px;
    border-bottom: 1px solid #e5e7eb;
  }
  .info-group {
    flex: 1;
  }
  .info-group:last-child {
    text-align: right;
  }
  .info-label {
    font-size: 12px;
    font-weight: 600;
    color: #6b7280;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    margin-bottom: 4px;
  }
  .info-value {
    font-size: 16px;
    font-weight: 600;
    color: #1f2937;
  }
  .info-value.sub {
    font-weight: 400;
    color: #4b5563;
  }
  .billed-to {
    margin-bottom: 30px;
    padding-bottom: 20px;
    border-bottom: 1px solid #e5e7eb;
  }
  .billed-to .label {
    font-size: 12px;
    font-weight: 600;
    color: #6b7280;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    margin-bottom: 4px;
  }
  .billed-to .name {
    font-size: 18px;
    font-weight: 700;
    color: #1f2937;
  }
  .billed-to .email {
    font-size: 14px;
    color: #6b7280;
  }
  .billing-period {
    margin-bottom: 20px;
  }
  .billing-period .label {
    font-size: 12px;
    font-weight: 600;
    color: #6b7280;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    margin-bottom: 4px;
  }
  .billing-period .value {
    font-size: 16px;
    font-weight: 500;
    color: #1f2937;
  }
  table {
    width: 100%;
    border-collapse: collapse;
    margin: 20px 0 30px;
  }
  thead {
    background-color: #f9fafb;
  }
  th {
    text-align: left;
    padding: 12px 16px;
    font-size: 12px;
    font-weight: 600;
    color: #6b7280;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    border-bottom: 2px solid #e5e7eb;
  }
  td {
    padding: 14px 16px;
    font-size: 14px;
    color: #1f2937;
    border-bottom: 1px solid #f3f4f6;
  }
  .text-right {
    text-align: right;
  }
  .font-mono {
    font-family: 'Courier New', monospace;
  }
  .summary {
    max-width: 320px;
    margin-left: auto;
    margin-top: 20px;
  }
  .summary-row {
    display: flex;
    justify-content: space-between;
    padding: 8px 0;
    font-size: 14px;
    color: #4b5563;
  }
  .summary-row.total {
    border-top: 2px solid #1f2937;
    margin-top: 4px;
    padding-top: 14px;
    font-size: 18px;
    font-weight: 700;
    color: #1f2937;
  }
  .summary-row .label {
    font-weight: 500;
  }
  .summary-row .value {
    font-weight: 600;
  }
  .summary-row.total .value {
    color: #dc2626;
  }
  .footer {
    background: #f9fafb;
    padding: 30px 40px;
    border-top: 1px solid #e5e7eb;
  }
  .footer-title {
    font-size: 14px;
    font-weight: 700;
    color: #1f2937;
    margin-bottom: 10px;
  }
  .footer p {
    font-size: 14px;
    color: #4b5563;
    line-height: 1.6;
    margin-bottom: 4px;
  }
  .footer .highlight {
    font-weight: 600;
    color: #1f2937;
  }
  .footer .contact {
    margin-top: 10px;
    color: #6b7280;
    font-size: 13px;
  }
  .footer .contact a {
    color: #dc2626;
    text-decoration: none;
  }
  .footer .contact a:hover {
    text-decoration: underline;
  }
  @media (max-width: 600px) {
    .header {
      flex-direction: column;
      gap: 12px;
      text-align: center;
      padding: 20px;
    }
    .info-grid {
      flex-direction: column;
      gap: 12px;
    }
    .info-group:last-child {
      text-align: left;
    }
    .body-content {
      padding: 20px;
    }
    .footer {
      padding: 20px;
    }
    .summary {
      max-width: 100%;
    }
  }
</style>
</head>
<body>

<div class="container">

  <!-- HEADER -->
  <div class="header">
    <div class="header-left">
      <span class="logo">GRAPHIC <span>NEWS</span> PLUS</span>
    </div>
    <div class="invoice-title">
      <div>INVOICE</div>
      <h2>#)html" + invoiceNo + R"html(</h2>
    </div>
  </div>

  <!-- BODY -->
  <div class="body-content">

    <!-- Info Grid -->
    <div class="info-grid">
      <div class="info-group">
        <div class="info-label">Invoice Date</div>
        <div class="info-value">)html" + dateStr + R"html(</div>
      </div>
      <div class="info-group">
        <div class="info-label">Due Date</div>
        <div class="info-value">)html" + dueDateStr + R"html(</div>
      </div>
      <div class="info-group">
        <div class="info-label">Invoice No</div>
        <div class="info-value">)html" + invoiceNo + R"html(</div>
      </div>
    </div>

    <!-- Billed To -->
    <div class="billed-to">
      <div class="label">Billed To</div>
      <div class="name">)html" + partner.getValueOfName() + R"html(</div>
      <div class="email">)html" + partner.getValueOfBillingEmail() + R"html(</div>
    </div>

    <!-- Billing Period -->
    <div class="billing-period">
      <div class="label">Billing Period</div>
      <div class="value">)html" + billingPeriod + R"html(</div>
    </div>

    <!-- Table -->
    <table>
      <thead>
        <tr>
          <th>Description</th>
          <th style="text-align: right;">Qty / Imps</th>
          <th style="text-align: right;">Unit Price ($)</th>
          <th style="text-align: right;">Amount ($)</th>
        </tr>
      </thead>
      <tbody>
        <tr>
          <td>Invoice for new and renewed subscriptions</td>
          <td class="text-right font-mono">)html" + std::to_string(quantity) + R"html(</td>
          <td class="text-right font-mono">)html" + formatCurrency(unitPrice) + R"html(</td>
          <td class="text-right font-mono">)html" + formatCurrency(totalInvoiceAmount) + R"html(</td>
        </tr>
      </tbody>
    </table>

    <!-- Summary -->
    <div class="summary">
      <div class="summary-row">
        <span class="label">Subtotal</span>
        <span class="value">GHS )html" + formatCurrency(subtotal) + R"html(</span>
      </div>
      <div class="summary-row">
        <span class="label">VAT (15%)</span>
        <span class="value">GHS )html" + formatCurrency(vat) + R"html(</span>
      </div>
      <div class="summary-row">
        <span class="label">GETFund / NHIL (5%)</span>
        <span class="value">GHS )html" + formatCurrency(getflNhil) + R"html(</span>
      </div>
      <div class="summary-row">
        <span class="label">Discount</span>
        <span class="value">GHS )html" + formatCurrency(discount) + R"html(</span>
      </div>
      <div class="summary-row total">
        <span class="label">Total Due</span>
        <span class="value">GHS )html" + formatCurrency(totalDue) + R"html(</span>
      </div>
    </div>

  </div>

  <!-- FOOTER -->
  <div class="footer">
    <div class="footer-title">Payment Instructions:</div>
    <p>Please make payment to Graphic Communications Group Ltd. via Bank Transfer.</p>
    <p><span class="highlight">Bank:</span> Ghana Commercial Bank (GCB)</p>
    <p><span class="highlight">Account No:</span> 3201001382531</p>
    <p><span class="highlight">Branch:</span> High Street</p>
    <p class="contact">
      If you have any questions concerning this invoice, please contact
      <a href="mailto:billing@graphic.com.gh">billing@graphic.com.gh</a>
    </p>
  </div>

</div>

</body>
</html>)html";

    LOG_INFO << "[sendPartnerInvoiceByMail] STEP 9 — Email HTML body built, size=" << emailBody.size() << " bytes";

    // STEP 10: Get plugins and services
    LOG_INFO << "[sendPartnerInvoiceByMail] STEP 10 — Getting GnpServicePlugin and EmailService";
    auto plugin = drogon::app().getPlugin<plugins::GnpServicePlugin>();
    auto &emailService = plugin->getEmailService();
    LOG_INFO << "[sendPartnerInvoiceByMail] STEP 10 — Plugin and EmailService obtained successfully";

    // STEP 11: Prepare email DTO
    LOG_INFO << "[sendPartnerInvoiceByMail] STEP 11 — Preparing SendEmailDto";
    dto::SendEmailDto emailDto;
    std::string recipientEmail = gnp::utils::StringUtils::trim(partner.getValueOfBillingEmail());
    if (recipientEmail.empty()) {
      recipientEmail = gnp::utils::StringUtils::trim(partner.getValueOfContactEmail());
    }
    emailDto.setTo(recipientEmail);
    emailDto.setSubject("Invoice " + invoiceNo + " - Graphic News Plus");
    emailDto.setBody(emailBody);
    LOG_INFO << "[sendPartnerInvoiceByMail] STEP 11 — Email DTO prepared: to=" << recipientEmail
             << " subject=" << emailDto.getSubject();

    // STEP 12: Send email
    LOG_INFO << "[sendPartnerInvoiceByMail] STEP 12 — Sending email asynchronously";
    try {
      co_await emailService.sendEmailAsync(emailDto);
      LOG_INFO << "[sendPartnerInvoiceByMail] STEP 12 — Email sent successfully";
    } catch (const std::exception &e) {
      LOG_ERROR << "[sendPartnerInvoiceByMail] STEP 12 FAILED — Email sending failed: " << e.what();
      gnp::dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "Failed to send invoice email";
      errorResponse.error["code"] = constants::ERR_UNSUPPORTED_OPERATION;
      errorResponse.error["detail"] = e.what();
      co_return errorResponse;
    }

    // STEP 13: Build success response (§2.3 Endpoint 1.3b contract:
    // { sent, invoiceNo, recipientEmail, dispatchedAt })
    LOG_INFO << "[sendPartnerInvoiceByMail] STEP 13 — Building success response";
    gnp::dto::BaseApiResponse response;
    response.success = true;
    response.message = "Invoice sent successfully to " + recipientEmail;
    response.result["sent"] = true;
    response.result["invoiceNo"] = invoiceNo;
    response.result["recipientEmail"] = recipientEmail;
    response.result["sentTo"] = recipientEmail;
    response.result["dispatchedAt"] = trantor::Date::now().toDbString();
    response.result["totalAmount"] = totalDue;

    // Report telemetry: privileged action audit (security_audit_logs)
    co_await reporting::recordSecurityAudit(
        "", "billing", "INVOICE_EMAIL_DISPATCHED", "invoices", invoiceNo, "Active");

    LOG_INFO << "[sendPartnerInvoiceByMail] COMPLETED — invoiceNo=" << invoiceNo
             << " sentTo=" << recipientEmail
             << " totalAmount=" << totalDue;
    co_return response;

  } catch (const DrogonDbException &e) {
    LOG_ERROR << "[sendPartnerInvoiceByMail] DATABASE EXCEPTION — " << e.base().what()
              << " (partnerId=" << dto.getPartnerId() << ")";
    gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to send invoice email";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  } catch (const std::exception &e) {
    LOG_ERROR << "[sendPartnerInvoiceByMail] UNHANDLED EXCEPTION — " << e.what()
              << " (partnerId=" << dto.getPartnerId() << ")";
    gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to send invoice email";
    errorResponse.error["code"] = constants::ERR_UNSUPPORTED_OPERATION;
    errorResponse.error["detail"] = e.what();
    co_return errorResponse;
  }
}



drogon::Task<dto::BaseApiResponse> CommercialPartnerService::bulkUploadSubscribersJson(
    const std::string partnerId, const Json::Value &jsonArray) {

  dto::BaseApiResponse response;
  int successCount = 0;
  int failureCount = 0;
  Json::Value duplicateRows(Json::arrayValue);

  for (const auto &item : jsonArray) {
    if (!item.isObject())
      continue;

    dto::CreatePartnerSubscriberDto dto;
    dto.setPartnerId(partnerId);
    dto.setFirstName(item.get("FirstName", "").asString());
    dto.setLastName(item.get("LastName", "").asString());
    dto.setEmail(item.get("Email", "").asString());
    dto.setPhoneNumber(item.get("PhoneNumber", "").asString());

    if (dto.getEmail().empty() || dto.getFirstName().empty()) {
      failureCount++;
      continue;
    }

    auto res = co_await createPartnerSubscriber(dto);
    if (res.success) {
      successCount++;
    } else {
      failureCount++;
      duplicateRows.append(item);
    }
  }

  response.success = true;
  response.message = "Bulk upload completed.";
  response.result["successCount"] = successCount;
  response.result["failureCount"] = failureCount;
  response.result["totalProcessed"] = successCount + failureCount;
  response.result["duplicates"] = duplicateRows;

  co_return response;
}

drogon::Task<::gnp::dto::BaseApiResponse> CommercialPartnerService::resetSubscriberPasswords(const std::string &partnerId, const std::vector<std::string> &exemptedEmails) {
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mp(dbClient);

  try {
    Criteria searchCriteria = Criteria(Users::Cols::_partner_id, CompareOperator::EQ, partnerId) &&
                              Criteria(Users::Cols::_is_partner_admin_user, CompareOperator::EQ, false);
    
    if (!exemptedEmails.empty()) {
      searchCriteria = searchCriteria && Criteria(Users::Cols::_email, CompareOperator::NotIn, exemptedEmails);
    }
    
    auto users = co_await mp.findBy(searchCriteria);
    
    drogon::async_run([users]() -> drogon::Task<void> {
      try {
        auto dbClient = drogon::app().getDbClient();
        CoroMapper<Users> bg_mp(dbClient);
        auto plugin = drogon::app().getPlugin<plugins::GnpServicePlugin>();
        auto &emailService = plugin->getEmailService();

        for (auto user : users) {
          try {
            std::string newPassword = utils::PasswordUtils::generateRandomPassword(8);
            user.setPasswordHash(bcrypt::generateHash(newPassword));
            
            co_await bg_mp.update(user);
            
            // send email
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
                  <p>Hello )html" + user.getValueOfFirstName() + R"html(,</p>
                  <p>Your password for Graphic News Plus has been reset by your organization.</p>
                  <p>Below are your new login credentials:</p>
                  <div class="credentials">
                    <div class="credential-item">
                      <div class="credential-label">Username (Email):</div>
                      <div class="credential-value">)html" + user.getValueOfEmail() + R"html(</div>
                    </div>
                    <div class="credential-item">
                      <div class="credential-label">New Password:</div>
                      <div class="credential-value">)html" + newPassword + R"html(</div>
                    </div>
                  </div>
                  <p>Please keep these credentials secure and change your password after your next login.</p>
                </div>
                <div class="footer">
                  &copy; )html" + trantor::Date::now().toCustomFormattedString("%Y") + R"html( Graphic News Plus. All rights reserved.
                </div>
              </div>
              </body>
              </html>
            )html";
            
            emailDto.setBody(emailBody);
            co_await emailService.sendEmailAsync(emailDto);
          } catch (const std::exception& e) {
            LOG_ERROR << "Failed to process password reset for " << user.getValueOfEmail() << ": " << e.what();
          }
        }
      } catch (const std::exception& e) {
        LOG_ERROR << "Background task for password reset failed: " << e.what();
      }
    });

    ::gnp::dto::BaseApiResponse response;
    response.success = true;
    response.message = "Password reset has been initiated for " + std::to_string(users.size()) + " subscribers.";
    co_return response;

  } catch (const drogon::orm::DrogonDbException &e) {
    ::gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Database error while resetting passwords.";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}


drogon::Task<::gnp::dto::BaseApiResponse> CommercialPartnerService::resetSubscriberPasswordByUserId(const std::string &partnerId, const std::string &userId) {
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> mp(dbClient);

  try {
    auto user = co_await mp.findOne(Criteria(Users::Cols::_id, CompareOperator::EQ, userId) && Criteria(Users::Cols::_partner_id, CompareOperator::EQ, partnerId));

    // Capture 'this' in the lambda to call member functions
    drogon::async_run([this, user]() -> drogon::Task<void> {
      try {
        auto dbClient = drogon::app().getDbClient();
        CoroMapper<Users> bg_mp(dbClient);
        auto plugin = drogon::app().getPlugin<plugins::GnpServicePlugin>();
        auto &emailService = plugin->getEmailService();
        auto &hubtelSmsApi = plugin->getHubtelSmsApi();

        try {
          std::string newPassword = utils::PasswordUtils::generateRandomPassword(8);
          auto userToUpdate = user; // Copy to modify
          userToUpdate.setPasswordHash(bcrypt::generateHash(newPassword));

          co_await bg_mp.update(userToUpdate);

          // Send notification (email or SMS based on email type)
          std::string userEmail = userToUpdate.getValueOfEmail();
          std::string userPhone = userToUpdate.getValueOfPhoneNumber();
          std::string firstName = userToUpdate.getValueOfFirstName();

          // Now 'this' is captured, so we can call isSystemGeneratedEmail
          if (this->isSystemGeneratedEmail(userEmail)) {
            // Send SMS with new password
            std::string message = "Your Graphic News Plus password has been reset. "
                                  "Your new password is: " + newPassword +
                                  ". Please login at https://new.graphicnewsplus.com";
            co_await hubtelSmsApi.sendSms(userPhone, message);
            LOG_INFO << "[resetSubscriberPasswordByUserId] Password reset SMS sent to: " << userPhone;
          } else {
            // Send email with new credentials
            dto::SendEmailDto emailDto;
            emailDto.setTo(gnp::utils::StringUtils::trim(userEmail));
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
                  <p>Hello )html" + firstName + R"html(,</p>
                  <p>Your password for Graphic News Plus has been reset by your organization.</p>
                  <p>Below are your new login credentials:</p>
                  <div class="credentials">
                    <div class="credential-item">
                      <div class="credential-label">Username (Email):</div>
                      <div class="credential-value">)html" + userEmail + R"html(</div>
                    </div>
                    <div class="credential-item">
                      <div class="credential-label">New Password:</div>
                      <div class="credential-value">)html" + newPassword + R"html(</div>
                    </div>
                  </div>
                  <p>Please keep these credentials secure and change your password after your next login.</p>
                </div>
                <div class="footer">
                  &copy; )html" + trantor::Date::now().toCustomFormattedString("%Y") + R"html( Graphic News Plus. All rights reserved.
                </div>
              </div>
              </body>
              </html>
            )html";

            emailDto.setBody(emailBody);
            co_await emailService.sendEmailAsync(emailDto);
          }
        } catch (const std::exception& e) {
          LOG_ERROR << "Failed to process password reset for " << user.getValueOfEmail() << ": " << e.what();
        }
      } catch (const std::exception& e) {
        LOG_ERROR << "Background task for password reset failed: " << e.what();
      }
    });

    dto::BaseApiResponse response;
    response.success = true;
    response.message = "Password reset has been initiated for user.";
    co_return response;

  } catch (const drogon::orm::UnexpectedRows &e) {
    ::gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "User not found.";
    errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    co_return errorResponse;
  } catch (const drogon::orm::DrogonDbException &e) {
    ::gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Database error while resetting password.";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}



drogon::Task<::gnp::dto::BaseApiResponse> CommercialPartnerService::activateDeactivatePartnerSubscriber(const dto::ActivateDeactivateSubscriberDto &dto) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Users> userMapper(dbClient);
  CoroMapper<UserSubscriptions> subMapper(dbClient);
  CoroMapper<CommercialPartners> partnerMapper(dbClient);

  try {
    // 1. Verify user exists and belongs to the partner
    auto user = co_await userMapper.findOne(
        Criteria(Users::Cols::_id, CompareOperator::EQ, dto.getSubscriberId()) &&
        Criteria(Users::Cols::_partner_id, CompareOperator::EQ, dto.getPartnerId()));

    // 2. Get partner details for quota management and email
    auto partner = co_await partnerMapper.findByPrimaryKey(dto.getPartnerId());

    // 3. Check current user status
    bool currentStatus = user.getValueOfIsActive();
    bool targetStatus = (dto.getStatus() == "active");

    // 4. If status is already the same, return early
    if (currentStatus == targetStatus) {
      dto::BaseApiResponse response;
      response.success = true;
      response.message = "Subscriber is already " + dto.getStatus();
      response.result["userId"] = dto.getSubscriberId();
      response.result["status"] = dto.getStatus();
      response.result["action"] = "No change";
      co_return response;
    }

    // 5. Activate or deactivate based on target status
    if (targetStatus) {
      // --- ACTIVATION PATH ---

      // 5a. Check if there's available quota before activating
      if (partner.getValueOfRemainingQuota() <= 0) {
        dto::BaseApiResponse errorResponse;
        errorResponse.success = false;
        errorResponse.message = "Cannot activate subscriber: No remaining quota available";
        errorResponse.error["code"] = constants::ERR_QUOTA_EXCEEDED;
        errorResponse.error["detail"] = "Partner has no remaining subscriber slots";
        errorResponse.error["remainingQuota"] = partner.getValueOfRemainingQuota();
        co_return errorResponse;
      }

      // 5b. Activate the user
      user.setIsActive(true);
      co_await userMapper.update(user);

      // 5c. Check if user has an existing subscription, if not create one
      auto subscriptions = co_await subMapper.findBy(
          Criteria(UserSubscriptions::Cols::_user_id, CompareOperator::EQ, dto.getSubscriberId()) &&
          Criteria(UserSubscriptions::Cols::_partner_id, CompareOperator::EQ, dto.getPartnerId()));

      if (subscriptions.empty()) {
        // Create a default subscription if none exists
        CoroMapper<SubscriptionPlans> planMapper(dbClient);
        auto plans = co_await planMapper.findBy(
            Criteria(SubscriptionPlans::Cols::_id, CompareOperator::EQ,
                     partner.getValueOfDefaultSubscriptionPlanId()));

        if (!plans.empty()) {
          auto& plan = plans[0];
          UserSubscriptions newSub;
          newSub.setUserId(dto.getSubscriberId());
          newSub.setPartnerId(dto.getPartnerId());
          newSub.setSubscriptionPlanId(plan.getValueOfId());
          newSub.setSubscriptionPlanDescription(plan.getValueOfName());
          newSub.setEmail(user.getValueOfEmail());
          newSub.setStartDate(partner.getValueOfSubscriptionStartDate());
          newSub.setEndDate(partner.getValueOfSubscriptionEndDate());
          newSub.setIsActive(true);
          newSub.setCreatedAt(trantor::Date::now());
          newSub.setSubscriptionIdentifier(utils::IdGeneratorUtils::generateRandomSixDigit());
          co_await subMapper.insert(newSub);
        }
      } else {
        // Reactivate existing subscription
        auto sub = subscriptions[0];
        sub.setIsActive(true);
        co_await subMapper.update(sub);
      }

      // 5d. Update partner quota (reduce remaining quota)
      partner.setRemainingQuota(partner.getValueOfRemainingQuota() - 1);
      co_await partnerMapper.update(partner);

      LOG_INFO << "[activateDeactivatePartnerSubscriber] Activated subscriber — userId: "
               << dto.getSubscriberId() << ", partnerId: " << dto.getPartnerId();

      // 5e. Send notification (email or SMS based on email type)
      std::string userEmail = user.getValueOfEmail();
      if (isSystemGeneratedEmail(userEmail)) {
        // Send SMS activation notification
        std::string message = "Your Graphic News Plus account has been activated. "
                              "You can now log in at https://new.graphicnewsplus.com";
        co_await sendSmsNotification(user.getValueOfPhoneNumber(), message);
        LOG_INFO << "[activateDeactivatePartnerSubscriber] Activation SMS sent to: "
                 << user.getValueOfPhoneNumber();
      } else {
        // Send email activation notification
        co_await sendActivationEmail(user, partner);
      }

      dto::BaseApiResponse response;
      response.success = true;
      response.message = "Subscriber activated successfully";
      response.result["userId"] = dto.getSubscriberId();
      response.result["status"] = "Active";
      response.result["action"] = "Activated";
      response.result["remainingQuota"] = partner.getValueOfRemainingQuota();
      co_return response;

    } else {
      // --- DEACTIVATION PATH ---

      // 5a. Deactivate the user
      user.setIsActive(false);
      co_await userMapper.update(user);

      // 5b. Deactivate all active subscriptions
      auto subscriptions = co_await subMapper.findBy(
          Criteria(UserSubscriptions::Cols::_user_id, CompareOperator::EQ, dto.getSubscriberId()) &&
          Criteria(UserSubscriptions::Cols::_is_active, CompareOperator::EQ, true));

      for (auto& sub : subscriptions) {
        sub.setIsActive(false);
        co_await subMapper.update(sub);
      }

      // 5c. Update partner quota (increase remaining quota)
      partner.setRemainingQuota(partner.getValueOfRemainingQuota() + 1);
      co_await partnerMapper.update(partner);

      LOG_INFO << "[activateDeactivatePartnerSubscriber] Deactivated subscriber — userId: "
               << dto.getSubscriberId() << ", partnerId: " << dto.getPartnerId();

      // 5d. Send notification (email or SMS based on email type)
      std::string userEmail = user.getValueOfEmail();
      if (isSystemGeneratedEmail(userEmail)) {
        // Send SMS deactivation notification
        std::string message = "Your Graphic News Plus account has been deactivated. "
                              "Please contact your organization administrator for assistance.";
        co_await sendSmsNotification(user.getValueOfPhoneNumber(), message);
        LOG_INFO << "[activateDeactivatePartnerSubscriber] Deactivation SMS sent to: "
                 << user.getValueOfPhoneNumber();
      } else {
        // Send email deactivation notification
        co_await sendDeactivationEmail(user, partner);
      }

      dto::BaseApiResponse response;
      response.success = true;
      response.message = "Subscriber deactivated successfully";
      response.result["userId"] = dto.getSubscriberId();
      response.result["status"] = "Inactive";
      response.result["action"] = "Deactivated";
      response.result["remainingQuota"] = partner.getValueOfRemainingQuota();
      co_return response;
    }

  } catch (const drogon::orm::UnexpectedRows &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Subscriber not found or does not belong to this partner";
    errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    errorResponse.error["detail"] = e.what();
    co_return errorResponse;
  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Database error while updating subscriber status";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}



// Helper method to send activation email
drogon::Task<void> CommercialPartnerService::sendActivationEmail(const drogon_model::Gnp::Users& user, const drogon_model::Gnp::CommercialPartners& partner) {

  auto plugin = drogon::app().getPlugin<plugins::GnpServicePlugin>();
  auto &emailService = plugin->getEmailService();

  dto::SendEmailDto emailDto;
  emailDto.setTo(gnp::utils::StringUtils::trim(user.getValueOfEmail()));
  emailDto.setSubject("Graphic News Plus - Account Activated");

  std::string fullName = user.getValueOfFirstName();
  if (!user.getValueOfLastName().empty()) {
    fullName += " " + user.getValueOfLastName();
  }

  std::string emailBody = R"html(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<style>
  body {
    margin: 0;
    padding: 0;
    background-color: #f4f6f8;
    font-family: 'Segoe UI', Arial, sans-serif;
  }
  .wrapper {
    width: 100%;
    padding: 30px 0;
  }
  .container {
    max-width: 600px;
    margin: 0 auto;
    background: #ffffff;
    border-radius: 10px;
    overflow: hidden;
    box-shadow: 0 8px 20px rgba(0,0,0,0.08);
  }
  .header {
    background: linear-gradient(135deg, #22C55E, #16A34A);
    color: #ffffff;
    padding: 30px 20px;
    text-align: center;
  }
  .header h1 {
    margin: 0;
    font-size: 24px;
  }
  .header .subtitle {
    margin: 8px 0 0;
    font-size: 16px;
    opacity: 0.9;
  }
  .content {
    padding: 30px;
    color: #333;
    line-height: 1.6;
  }
  .status-badge {
    display: inline-block;
    padding: 8px 20px;
    background-color: #22C55E;
    color: #ffffff;
    border-radius: 20px;
    font-weight: 600;
    font-size: 14px;
    margin: 10px 0;
  }
  .info-box {
    background: #f0fdf4;
    border: 1px solid #bbf7d0;
    padding: 20px;
    border-radius: 8px;
    margin: 20px 0;
  }
  .info-box .label {
    font-weight: 600;
    color: #16A34A;
  }
  .cta {
    text-align: center;
    margin: 30px 0 15px;
  }
  .btn {
    display: inline-block;
    padding: 12px 25px;
    background-color: #22C55E;
    color: #ffffff;
    text-decoration: none;
    border-radius: 6px;
    font-weight: 600;
    font-size: 14px;
  }
  .btn:hover {
    background-color: #16A34A;
  }
  .footer {
    background: #f4f6f8;
    color: #888;
    text-align: center;
    padding: 20px;
    font-size: 12px;
  }
</style>
</head>
<body>
<div class="wrapper">
  <div class="container">
    <div class="header">
      <h1>✅ Account Activated</h1>
      <div class="subtitle">Graphic News Plus</div>
    </div>
    <div class="content">
      <p>Hello )html" + fullName + R"html(,</p>

      <p>We are pleased to inform you that your Graphic News Plus account has been <strong>activated</strong>.</p>

      <div style="text-align: center;">
        <span class="status-badge">● ACTIVE</span>
      </div>

      <div class="info-box">
        <p style="margin: 0;">
          <span class="label">Organization:</span> )html" + partner.getValueOfName() + R"html(<br>
          <span class="label">Email:</span> )html" + user.getValueOfEmail() + R"html(<br>
          <span class="label">Status:</span> <span style="color: #16A34A; font-weight: 600;">Active</span>
        </p>
      </div>

      <p>You can now log in to your account and access all the premium content available on our platform.</p>

      <div class="cta">
        <a href="https://new.graphicnewsplus.com?al=t" style="color:#ffffff !important; text-decoration:none;" class="btn">
          Login to Your Account
        </a>
      </div>

      <p style="font-size:13px; color:#777;">
        If the button above doesn't work, copy and paste this link into your browser:<br>
        <a href="https://new.graphicnewsplus.com?al=t" style="color: #22C55E;">
          https://new.graphicnewsplus.com?al=t
        </a>
      </p>
    </div>
    <div class="footer">
      <p>&copy; )html" + trantor::Date::now().toCustomFormattedString("%Y") + R"html( Graphic News Plus</p>
      <p>All rights reserved.</p>
    </div>
  </div>
</div>
</body>
</html>
)html";

  emailDto.setBody(emailBody);
  co_await emailService.sendEmailAsync(emailDto);

  LOG_INFO << "[sendActivationEmail] Activation email sent to: " << user.getValueOfEmail();
}

// Helper method to send deactivation email
drogon::Task<void> CommercialPartnerService::sendDeactivationEmail(const drogon_model::Gnp::Users& user, const drogon_model::Gnp::CommercialPartners& partner) {

  auto plugin = drogon::app().getPlugin<plugins::GnpServicePlugin>();
  auto &emailService = plugin->getEmailService();

  dto::SendEmailDto emailDto;
  emailDto.setTo(gnp::utils::StringUtils::trim(user.getValueOfEmail()));
  emailDto.setSubject("Graphic News Plus - Account Deactivated");

  std::string fullName = user.getValueOfFirstName();
  if (!user.getValueOfLastName().empty()) {
    fullName += " " + user.getValueOfLastName();
  }

  std::string emailBody = R"html(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<style>
  body {
    margin: 0;
    padding: 0;
    background-color: #f4f6f8;
    font-family: 'Segoe UI', Arial, sans-serif;
  }
  .wrapper {
    width: 100%;
    padding: 30px 0;
  }
  .container {
    max-width: 600px;
    margin: 0 auto;
    background: #ffffff;
    border-radius: 10px;
    overflow: hidden;
    box-shadow: 0 8px 20px rgba(0,0,0,0.08);
  }
  .header {
    background: linear-gradient(135deg, #EF4444, #DC2626);
    color: #ffffff;
    padding: 30px 20px;
    text-align: center;
  }
  .header h1 {
    margin: 0;
    font-size: 24px;
  }
  .header .subtitle {
    margin: 8px 0 0;
    font-size: 16px;
    opacity: 0.9;
  }
  .content {
    padding: 30px;
    color: #333;
    line-height: 1.6;
  }
  .status-badge {
    display: inline-block;
    padding: 8px 20px;
    background-color: #EF4444;
    color: #ffffff;
    border-radius: 20px;
    font-weight: 600;
    font-size: 14px;
    margin: 10px 0;
  }
  .info-box {
    background: #fef2f2;
    border: 1px solid #fecaca;
    padding: 20px;
    border-radius: 8px;
    margin: 20px 0;
  }
  .info-box .label {
    font-weight: 600;
    color: #DC2626;
  }
  .reason-box {
    background: #f9fafb;
    border: 1px solid #e5e7eb;
    padding: 15px;
    border-radius: 8px;
    margin: 15px 0;
    font-size: 14px;
    color: #4b5563;
  }
  .footer {
    background: #f4f6f8;
    color: #888;
    text-align: center;
    padding: 20px;
    font-size: 12px;
  }
  .contact-link {
    color: #DC2626;
    text-decoration: none;
  }
  .contact-link:hover {
    text-decoration: underline;
  }
</style>
</head>
<body>
<div class="wrapper">
  <div class="container">
    <div class="header">
      <h1>⛔ Account Deactivated</h1>
      <div class="subtitle">Graphic News Plus</div>
    </div>
    <div class="content">
      <p>Hello )html" + fullName + R"html(,</p>

      <p>We regret to inform you that your Graphic News Plus account has been <strong>deactivated</strong>.</p>

      <div style="text-align: center;">
        <span class="status-badge">● INACTIVE</span>
      </div>

      <div class="info-box">
        <p style="margin: 0;">
          <span class="label">Organization:</span> )html" + partner.getValueOfName() + R"html(<br>
          <span class="label">Email:</span> )html" + user.getValueOfEmail() + R"html(<br>
          <span class="label">Status:</span> <span style="color: #DC2626; font-weight: 600;">Inactive</span>
        </p>
      </div>

      <div class="reason-box">
        <strong>What does this mean?</strong><br>
        <span style="color: #6b7280;">You will not be able to log in to Graphic News Plus until your account is reactivated by your organization administrator.</span>
      </div>

      <p>If you believe this was done in error, please contact your organization administrator:</p>

      <p style="background: #f9fafb; padding: 10px 15px; border-radius: 6px; font-size: 14px;">
        <strong>Organization:</strong> )html" + partner.getValueOfName() + R"html(<br>
        <strong>Contact Email:</strong> <a href="mailto:)"html" + partner.getValueOfBillingEmail() + R"html(" style="color: #DC2626; text-decoration: none;">)html" + partner.getValueOfBillingEmail() + R"html(</a>
      </p>

      <p style="font-size:13px; color:#777; margin-top: 20px;">
        If you have any questions, please contact our support team at
        <a href="mailto:support@graphicnewsplus.com" style="color: #DC2626;">support@graphicnewsplus.com</a>
      </p>
    </div>
    <div class="footer">
      <p>&copy; )html" + trantor::Date::now().toCustomFormattedString("%Y") + R"html( Graphic News Plus</p>
      <p>All rights reserved.</p>
    </div>
  </div>
</div>
</body>
</html>
)html";

  emailDto.setBody(emailBody);
  co_await emailService.sendEmailAsync(emailDto);

  LOG_INFO << "[sendDeactivationEmail] Deactivation email sent to: " << user.getValueOfEmail();
}


 bool CommercialPartnerService::isSystemGeneratedEmail(const std::string& email) const {
  // Check if email matches pattern: [phoneNumber]@graphicnewsplus.com.gh
  // Example: 0555305007@graphicnewsplus.com.gh

  std::string trimmedEmail = utils::StringUtils::trim(email);

  // Check if it ends with @graphicnewsplus.com.gh
  std::string domain = "@graphicnewsplus.com.gh";
  if (trimmedEmail.length() <= domain.length()) {
    return false;
  }

  std::string emailDomain = trimmedEmail.substr(trimmedEmail.length() - domain.length());
  if (emailDomain != domain) {
    return false;
  }

  // Check if the part before @ is a valid phone number (starts with 0 and contains only digits)
  std::string phonePart = trimmedEmail.substr(0, trimmedEmail.length() - domain.length());
  if (phonePart.empty() || phonePart[0] != '0') {
    return false;
  }

  // Check if all characters are digits
  for (char c : phonePart) {
    if (!std::isdigit(c)) {
      return false;
    }
  }

  return true;
}

 drogon::Task<void> CommercialPartnerService::sendSmsNotification(const std::string& phoneNumber, const std::string& message) {

  try {
    auto plugin = drogon::app().getPlugin<plugins::GnpServicePlugin>();
    auto &hubtelSmsApi = plugin->getHubtelSmsApi();

    co_await hubtelSmsApi.sendSms(phoneNumber, message);
    LOG_INFO << "[sendSmsNotification] SMS sent to: " << phoneNumber;
  } catch (const std::exception& e) {
    LOG_ERROR << "[sendSmsNotification] Failed to send SMS to " << phoneNumber << ": " << e.what();
  }
}


} // namespace gnp::services
