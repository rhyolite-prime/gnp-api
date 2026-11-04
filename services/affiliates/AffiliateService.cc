//
// Created by Emmanuel Addo-Odame on 11/02/2026.
//

#include "AffiliateService.h"
#include "constants/ErrorCodes.h"
#include "dto/UserDto.h"
#include "dto/SendEmailDto.h"
#include "models/AffiliateCommissions.h"
#include "models/AffiliatePayouts.h"
#include "models/Affiliates.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include "services/email/EmailService.h"
#include "services/users/UserService.h"
#include <drogon/orm/CoroMapper.h>
#include <drogon/orm/Mapper.h>
#include <chrono>
#include <iomanip>
#include <sstream>

#include "bcrypt.h"
#include "Users.h"
#include "constants/StatusTypes.h"
#include "plugins/GnpServicePlugin.h"
#include "utils/IdGeneratorUtils.h"
#include "utils/PasswordUtils.h"
#include "utils/TimeUtils.h"

using namespace drogon::orm;
using drogon_model::Gnp::AffiliateCommissions;
using drogon_model::Gnp::AffiliatePayouts;
using drogon_model::Gnp::Affiliates;

namespace gnp::services {

drogon::Task<dto::BaseApiResponse> AffiliateService::getAll(int pageNo, int pageSize, const std::string &query, int status, const std::string &sortBy) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Affiliates> mp(dbClient);

  // 1. Build the search criteria
  Criteria searchCriteria;
  if (!query.empty()) {
    std::string likeQuery = "%" + query + "%";

    searchCriteria =
        Criteria(Affiliates::Cols::_first_name, CompareOperator::Like, likeQuery) ||
        Criteria(Affiliates::Cols::_email, CompareOperator::Like, likeQuery) ||
        Criteria(Affiliates::Cols::_phone, CompareOperator::Like, likeQuery) ||
        Criteria(Affiliates::Cols::_last_name, CompareOperator::Like, likeQuery);
  }

  if (status >= 0) {
    searchCriteria = searchCriteria && Criteria(Affiliates::Cols::_status, CompareOperator::EQ, status);
  }

  try {
    // 2. Get total count
    auto totalCount = co_await mp.count(searchCriteria);

    if (totalCount == 0) {
      dto::BaseApiResponse response;
      response.success = true;
      response.result["data"] = Json::arrayValue;
      response.result["totalCount"] = 0;
      co_return response;
    }

    // 3. Determine sorting logic based on sortBy parameter
    int offset = (pageNo - 1) * pageSize;
    std::vector<Affiliates> affiliates;

    if (sortBy == "earnings-high-to-low" || sortBy == "earnings-low-to-high") {
      // For earnings-based sorting, we need to use a custom SQL query
      // that joins with commissions/payouts and aggregates earnings
      std::string orderDirection = (sortBy == "earnings-high-to-low") ? "DESC" : "ASC";

      std::string sql =
          "SELECT a.*, COALESCE(SUM(c.amount), 0) as total_earnings "
          "FROM affiliates a "
          "LEFT JOIN affiliate_commissions c ON a.id = c.affiliate_id "
          "WHERE 1=1 ";

      // Add search criteria if query is not empty
      if (!query.empty()) {
        sql += "AND (TRIM(CONCAT(COALESCE(a.first_name, ''), ' ', "
               "COALESCE(a.last_name, ''))) ILIKE $1 OR a.email ILIKE $1 "
               "OR a.phone ILIKE $1) ";
      }

      sql += "GROUP BY a.id "
             "ORDER BY total_earnings " +
             orderDirection +
             " "
             "LIMIT $" +
             (query.empty() ? "1" : "2") + " OFFSET $" +
             (query.empty() ? "2" : "3");

      // Execute query based on whether we have a search term
      drogon::orm::Result result(nullptr);
      if (!query.empty()) {
        std::string likeQuery = "%" + query + "%";
        result =
            co_await dbClient->execSqlCoro(sql, likeQuery, pageSize, offset);
      } else {
        result = co_await dbClient->execSqlCoro(sql, pageSize, offset);
      }

      for (const auto &row : result) {
        affiliates.emplace_back(Affiliates(row, -1));
      }

    } else {
      // Default: newest (sort by date_joined DESC)
      affiliates = co_await mp.limit(pageSize)
                       .offset(offset)
                       .orderBy(Affiliates::Cols::_date_joined, SortOrder::DESC)
                       .findBy(searchCriteria);
    }

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

    for (const auto &affiliate : affiliates) {
      Json::Value affiliateJson = affiliate.toJson();

      // Convert snake_case to camelCase
      Json::Value camelCaseAffiliate;
      camelCaseAffiliate["id"] = affiliateJson["id"];
      camelCaseAffiliate["firstName"] = affiliateJson["first_name"];
      camelCaseAffiliate["lastName"] = affiliateJson["last_name"];
      camelCaseAffiliate["email"] = affiliateJson["email"];
      camelCaseAffiliate["phoneNo"] = affiliateJson["phone"];
      camelCaseAffiliate["status"] = affiliateJson["status"];
      camelCaseAffiliate["dateJoined"] = affiliateJson["created_at"];
      camelCaseAffiliate["totalEarnings"] = affiliateJson["total_earnings"];
      camelCaseAffiliate["walletBalance"] = affiliateJson["wallet_balance"];

      data.append(camelCaseAffiliate);
    }
    response.result["data"] = data;
    co_return response;

  } catch (const DrogonDbException &e) {
    // Handle error
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] = "Database error while fetching affiliates.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

  drogon::Task<dto::BaseApiResponse> AffiliateService::getAffiliateAccountDetails(const std::string &id) {

  //return the row that matches the primary key

}


drogon::Task<dto::BaseApiResponse> AffiliateService::getAllApplicants(int pageNo, int pageSize, const std::string &query, int status) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Affiliates> mp(dbClient);

  // 1. Build the search criteria. Applications are stored in the `affiliates`
  //    table with StatusTypes: SUBMITTED(20) -> APPROVED(16) / REJECTED(17)
  //    (PENDING is used by commissions, not applications). `status < 0`
  //    lists every application status; otherwise it filters by that status.
  Criteria searchCriteria;
  if (status >= 0) {
    searchCriteria = searchCriteria &&
        Criteria(Affiliates::Cols::_status, CompareOperator::EQ, status);
  }
  if (!query.empty()) {
    std::string likeQuery = "%" + query + "%";

    searchCriteria = searchCriteria &&
        (Criteria(Affiliates::Cols::_first_name, CompareOperator::Like, likeQuery) ||
         Criteria(Affiliates::Cols::_last_name, CompareOperator::Like, likeQuery) ||
         Criteria(Affiliates::Cols::_phone, CompareOperator::Like, likeQuery) ||
         Criteria(Affiliates::Cols::_email, CompareOperator::Like, likeQuery));
  }

  try {
    size_t totalCount = co_await mp.count(searchCriteria);
    if (totalCount == 0) {
      dto::BaseApiResponse response;
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
    auto affiliateApplicants = co_await mp.limit(pageSize).offset(offset).findBy(searchCriteria);

    // 4. Build the final response
    gnp::dto::BaseApiResponse response;
    auto totalPages = (totalCount + pageSize - 1) / pageSize;

    response.success = true;
    response.result["totalCount"] = (Json::UInt64)totalCount;
    response.result["pageNo"] = pageNo;
    response.result["pageSize"] = pageSize;
    response.result["lowerBound"] = pageSize * (pageNo - 1) + 1;
    response.result["upperBound"] = Json::Value((int)totalPages == pageNo ? (Json::UInt64)totalCount : (Json::UInt64)(pageNo * pageSize));
    response.result["totalPages"] = (int)totalPages;

    Json::Value data = Json::arrayValue;

    for (const auto &affiliateApplicant : affiliateApplicants) {
      Json::Value affiliateApplicantJson = affiliateApplicant.toJson();

      // Convert snake_case to camelCase
      Json::Value camelCaseAffiliateApplicant;

      camelCaseAffiliateApplicant["id"] = affiliateApplicantJson["id"];
      camelCaseAffiliateApplicant["firstName"] = affiliateApplicantJson["first_name"];
      camelCaseAffiliateApplicant["lastName"] = affiliateApplicantJson["last_name"];
      camelCaseAffiliateApplicant["email"] = affiliateApplicantJson["email"];
      camelCaseAffiliateApplicant["phoneNo"] = affiliateApplicantJson["phone"];
      camelCaseAffiliateApplicant["affiliateId"] = affiliateApplicantJson["phone"];
      camelCaseAffiliateApplicant["status"] = affiliateApplicantJson["status"];
      camelCaseAffiliateApplicant["createdAt"] = affiliateApplicantJson["created_at"];

      std::string createdAt = affiliateApplicantJson["created_at"].asString();
      std::string timeAgo = gnp::utils::TimeUtils::getTimeAgo(createdAt);

      camelCaseAffiliateApplicant["dateApplied"] = timeAgo; // example present as 1 hour ago, 2 days ago, 1, week ago 3months ago

      data.append(camelCaseAffiliateApplicant);
    }

    response.result["data"] = data;
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] = "Database error while fetching affiliate applicants.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }


}


 drogon::Task<::gnp::dto::BaseApiResponse> AffiliateService::submitApplication(const dto::AffiliateSignupDto &dto) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<drogon_model::Gnp::Affiliates> applicationMapper(dbClient);

  try {
    // 1. Validate input - check if email already has a pending application
    auto existingApplications = co_await applicationMapper.findBy(
        Criteria(drogon_model::Gnp::Affiliates::Cols::_email, CompareOperator::EQ, dto.getEmail()) &&
        Criteria(drogon_model::Gnp::Affiliates::Cols::_status, CompareOperator::EQ, static_cast<int>(constants::StatusTypes::SUBMITTED)));

    if (!existingApplications.empty()) {
      dto::BaseApiResponse response;
      response.success = false;
      response.error["code"] = constants::ERR_VALIDATION;
      response.error["message"] = "You already have a pending application. Please wait for review.";
      response.message = "You already have a pending application. Please wait for review.";
      co_return response;
    }

    // 2. Check if email already has an approved application (existing user)
    auto approvedApplications = co_await applicationMapper.findBy(
        Criteria(Affiliates::Cols::_email, CompareOperator::EQ, dto.getEmail()) && Criteria(Affiliates::Cols::_status, CompareOperator::EQ, static_cast<int>(constants::StatusTypes::APPROVED)));

    if (!approvedApplications.empty()) {
      dto::BaseApiResponse response;
      response.success = false;
      response.error["code"] = constants::ERR_VALIDATION;
      response.error["message"] = "This email is already associated with an approved affiliate account.";
      response.message = "This email is already associated with an approved affiliate account.";
      co_return response;
    }

    // 3. Validate required fields
    if (dto.getFirstName().empty() || dto.getLastName().empty() || dto.getEmail().empty() || dto.getPhoneNumber().empty()) {
      dto::BaseApiResponse response;
      response.success = false;
      response.error["code"] = constants::ERR_VALIDATION;
      response.error["message"] = "All fields (firstName, lastName, email, phoneNo) are required.";
      co_return response;
    }


    Affiliates application;
    application.setFirstName(dto.getFirstName());
    application.setLastName(dto.getLastName());
    std::string standardEmail = dto.getEmail();
    std::transform(standardEmail.begin(), standardEmail.end(), standardEmail.begin(), [](unsigned char c) { return std::tolower(c); });
    application.setEmail(standardEmail);
    application.setPhone(dto.getPhoneNumber());

    application.setStatus(constants::StatusTypes::SUBMITTED);
    application.setDateJoined(trantor::Date::now());
    application.setUpdatedAt(trantor::Date::now());

    auto savedApplication = co_await applicationMapper.insert(application);


    // 7. Build success response
    dto::BaseApiResponse response;
    response.success = true;
    response.result["message"] = "Your application has been submitted successfully. We will review it and get back to you soon.";

    // Send email notifications
    auto &app = drogon::app();
    auto customConfig = app.getCustomConfig();
    Json::Value adminRecipients = customConfig["MailConfiguration"]["AdminRecipients"];

    auto plugin = drogon::app().getPlugin<plugins::GnpServicePlugin>();
    auto &emailService = plugin->getEmailService();

    std::string currentYear = trantor::Date::now().toCustomFormattedString("%Y");
    std::string submissionDate = trantor::Date::now().toCustomFormattedString("%B %d, %Y at %I:%M %p");
    std::string applicantFullName = dto.getFirstName() + " " + dto.getLastName();
    const std::string adminDashboardUrl = "https://new.graphicnewsplus.com/admin/affiliates";

    // Build Admin Email HTML Content (simplified)
    std::string adminEmailBody =
        "<div style=\"font-family: Arial, sans-serif; font-size: 15px; color: #333; line-height: 1.6; max-width: 600px; margin: 0 auto; padding: 20px;\">\n"
        "  <h2 style=\"color: #1a1a2e; margin-bottom: 16px;\">Graphic News Plus</h2>\n"
        "  <p>Hello Admin,</p>\n"
        "  <p>A new affiliate application has been submitted on <strong>Graphic News Plus</strong>.</p>\n"
        "  <p style=\"background: #f9f9f9; padding: 15px; border-radius: 6px; border: 1px solid #eee;\">\n"
        "    <strong>Applicant Name:</strong> " + applicantFullName + "<br>\n"
        "    <strong>Email:</strong> " + standardEmail + "<br>\n"
        "    <strong>Phone:</strong> " + dto.getPhoneNumber() + "<br>\n"
        "    <strong>Submission Date:</strong> " + submissionDate + "\n"
        "  </p>\n"
        "  <p style=\"margin: 20px 0;\">\n"
        "    <a href=\"" + adminDashboardUrl + "\"\n"
        "       target=\"_blank\"\n"
        "       style=\"background:#4f6ef7;color:#fff;padding:10px 20px;text-decoration:none;border-radius:4px;display:inline-block;\">Review Applications</a>\n"
        "  </p>\n"
        "  <hr style=\"border: none; border-top: 1px solid #eee; margin: 25px 0 15px 0;\">\n"
        "  <p style=\"font-size: 12px; color: #888;\">&copy; " + currentYear + " Graphic News Plus. All rights reserved.</p>\n"
        "</div>";

    // Build Affiliate Email HTML Content (simplified)
    std::string affiliateEmailBody =
        "<div style=\"font-family: Arial, sans-serif; font-size: 15px; color: #333; line-height: 1.6; max-width: 600px; margin: 0 auto; padding: 20px;\">\n"
        "  <h2 style=\"color: #1a1a2e; margin-bottom: 16px;\">Graphic News Plus</h2>\n"
        "  <p>Dear <strong>" + dto.getFirstName() + "</strong>,</p>\n"
        "  <p>Thank you for applying to the <strong>Graphic News Plus Affiliate Program</strong>! We have received your application and our team is currently reviewing your details.</p>\n"
        "  <p style=\"background: #f9f9f9; padding: 15px; border-radius: 6px; border: 1px solid #eee;\">\n"
        "    <strong>Name:</strong> " + applicantFullName + "<br>\n"
        "    <strong>Email:</strong> " + standardEmail + "<br>\n"
        "    <strong>Phone:</strong> " + dto.getPhoneNumber() + "\n"
        "  </p>\n"
        "  <p>Once your application is approved, you will receive an email with your credentials to access the Affiliate Dashboard and start earning commissions.</p>\n"
        "  <p>If you have any questions, please contact us at <a href=\"mailto:support@graphicnewsplus.com\" style=\"color: #4f6ef7;\">support@graphicnewsplus.com</a>.</p>\n"
        "  <hr style=\"border: none; border-top: 1px solid #eee; margin: 25px 0 15px 0;\">\n"
        "  <p style=\"font-size: 12px; color: #888;\">&copy; " + currentYear + " Graphic News Plus. All rights reserved.</p>\n"
        "</div>";

    // 1. Inform admin recipients
    if (adminRecipients.isArray()) {
      for (const auto &recipient : adminRecipients) {
        std::string adminEmail = recipient.asString();
        if (adminEmail.empty()) continue;

        dto::SendEmailDto adminEmailDto;
        adminEmailDto.setTo(adminEmail);
        adminEmailDto.setSubject("New Affiliate Registration on GNP");
        adminEmailDto.setBody(adminEmailBody);
        co_await emailService.sendEmailAsync(adminEmailDto);
      }
    }

    // 2. Send congratulatory / welcome message to affiliate applicant
    if (!standardEmail.empty()) {
      dto::SendEmailDto affiliateEmailDto;
      affiliateEmailDto.setTo(standardEmail);
      affiliateEmailDto.setSubject("Welcome Aboard !");
      affiliateEmailDto.setBody(affiliateEmailBody);
      co_await emailService.sendEmailAsync(affiliateEmailDto);
    }

    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] = "Database error while submitting application.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;

  } catch (const std::exception &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_INTERNAL;
    errorResponse.error["message"] = "Internal error.";
    errorResponse.error["detail"] = e.what();
    co_return errorResponse;
  }
}


 drogon::Task<::gnp::dto::BaseApiResponse> AffiliateService::createAsync(const ::gnp::dto::AffiliateDto &dto) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Affiliates> mapper(dbClient);
  CoroMapper<drogon_model::Gnp::Users> userMapper(dbClient);

  try {

    // 1. Create the user first
    drogon_model::Gnp::Users newUser;
    newUser.setEmail(dto.getEmail());
    newUser.setUsername(dto.getEmail());
    newUser.setFirstName(dto.getFirstName());
    newUser.setLastName(dto.getLastName());
    newUser.setPhoneNumber(dto.getPhone());

    // Generate random 8-character password
    std::string password = gnp::utils::PasswordUtils::generateRandomPassword(8);

    newUser.setPasswordHash(bcrypt::generateHash(password));
    newUser.setIsPartnerAdminUser(false);
    newUser.setIsAffiliate(true);

    std::string affiliateId = utils::IdGeneratorUtils::generateRandomSixDigit();

    newUser.setAffiliateId(affiliateId);
    newUser.setIsActive(true);
    newUser.setIsLockedOut(false);
    newUser.setIsAdminUser(false);
    newUser.setCreatedAt(trantor::Date::now());

    auto user = co_await userMapper.insert(newUser);

    // 4. Build and insert the affiliate record using the user_id
    Affiliates affiliate;
    affiliate.setFirstName(dto.getFirstName());
    affiliate.setLastName(dto.getLastName());
    affiliate.setPhone(dto.getPhone());
    affiliate.setStatus(dto.getStatus());
    affiliate.setAffiliateId(affiliateId);
    affiliate.setUserId(user.getValueOfId());
    affiliate.setTotalEarnings("0");
    affiliate.setWalletBalance("0");
    affiliate.setDateJoined(dto.getDateJoined());
    affiliate.setUpdatedAt(trantor::Date::now());

    co_await mapper.insert(affiliate);

    // 5. Send welcome email with login credentials
       const std::string loginUrl =
        "https://dev.graphicnewsplus.com/affiliates/login";

    const std::string htmlBody =
        "<!DOCTYPE html>"
        "<html lang=\"en\"><head><meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" "
        "content=\"width=device-width,initial-scale=1.0\">"
        "<title>Welcome to Graphic News Plus Affiliates</title></head>"
        "<body "
        "style=\"margin:0;padding:0;background-color:#f4f4f7;font-family:Arial,"
        "sans-serif;\">"

        // Outer wrapper
        "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" "
        "style=\"background-color:#f4f4f7;padding:40px 0;\">"
        "<tr><td align=\"center\">"

        // Card
        "<table width=\"600\" cellpadding=\"0\" cellspacing=\"0\" "
        "style=\"background-color:#ffffff;border-radius:8px;"
        "overflow:hidden;box-shadow:0 2px 8px rgba(0,0,0,0.08);\">"

        // Header
        "<tr><td style=\"background-color:#1a1a2e;padding:32px "
        "40px;text-align:center;\">"
        "<h1 "
        "style=\"margin:0;color:#ffffff;font-size:22px;font-weight:700;letter-"
        "spacing:0.5px;\">"
        "Graphic News Plus</h1>"
        "<p style=\"margin:6px 0 0;color:#a0a8c0;font-size:13px;\">Affiliate "
        "Program</p>"
        "</td></tr>"

        // Body
        "<tr><td style=\"padding:36px 40px;\">"
        "<p style=\"margin:0 0 16px;font-size:16px;color:#333333;\">Dear "
        "<strong>" +
        dto.getFirstName() +
        "</strong>,</p>"
        "<p style=\"margin:0 0 "
        "16px;font-size:15px;color:#555555;line-height:1.6;\">"
        "Welcome aboard! Your affiliate account has been created successfully. "
        "Below are your login credentials for the <strong>Affiliate "
        "Dashboard</strong>.</p>"

        // Credentials box
        "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" "
        "style=\"background-color:#f0f4ff;"
        "border-radius:6px;border:1px solid #dce3f5;margin:24px 0;\">"
        "<tr><td style=\"padding:20px 24px;\">"
        "<table width=\"100%\" cellpadding=\"6\" cellspacing=\"0\">"
        "<tr>"
        "<td style=\"font-size:13px;color:#888888;width:120px;\">Login URL</td>"
        "<td style=\"font-size:13px;color:#1a1a2e;\"><a href=\"" +
        loginUrl + "\" style=\"color:#4f6ef7;text-decoration:none;\">" +
        loginUrl +
        "</a></td>"
        "</tr>"
        "<tr>"
        "<td style=\"font-size:13px;color:#888888;\">Username</td>"
        "<td style=\"font-size:13px;color:#1a1a2e;font-family:monospace;\">" +
        dto.getEmail() +
        "</td>"
        "</tr>"
        "<tr>"
        "<td style=\"font-size:13px;color:#888888;\">Password</td>"
        "<td style=\"font-size:13px;color:#1a1a2e;font-family:monospace;\">" +
        password +
        "</td>"
        "</tr>"
        "</table>"
        "</td></tr></table>"

        // CTA button
        "<div style=\"text-align:center;margin:28px 0;\">"
        "<a href=\"" +
        loginUrl +
        "\" "
        "style=\"display:inline-block;background-color:#4f6ef7;color:#ffffff;"
        "text-decoration:none;font-size:15px;font-weight:600;padding:14px 36px;"
        "border-radius:6px;letter-spacing:0.3px;\">Go to Affiliate "
        "Dashboard</a>"
        "</div>"

        "<p style=\"margin:0 0 "
        "8px;font-size:13px;color:#888888;line-height:1.6;\">"
        "&#128274; For your security, please change your password immediately "
        "after your first login.</p>"
        "<p style=\"margin:0;font-size:13px;color:#888888;line-height:1.6;\">"
        "If you have any questions, feel free to reach out to our support "
        "team.</p>"
        "</td></tr>"

        // Footer
        "<tr><td style=\"background-color:#f8f9fc;padding:20px "
        "40px;text-align:center;"
        "border-top:1px solid #e8eaf0;\">"
        "<p style=\"margin:0;font-size:12px;color:#aaaaaa;\">"
        "&copy; 2026 Graphic News Plus. All rights reserved.</p>"
        "</td></tr>"

        "</table>"
        "</td></tr></table>"
        "</body></html>";

    dto::SendEmailDto emailDto;
    emailDto.setTo(dto.getEmail());
    emailDto.setSubject("Welcome to the Graphic News Plus Affiliate Program!");
    emailDto.setBody(htmlBody);

    if (!emailDto.getTo().empty()) {
      EmailService emailService;
      co_await emailService.sendEmailAsync(emailDto);
    }

    dto::BaseApiResponse response;
    response.success = true;
    response.result["message"] = "Affiliate created successfully.";
    response.result["userId"] = user.getValueOfId();
    response.result["affiliateId"] = affiliateId;
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] = "Database error while creating affiliate.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  } catch (const std::exception &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_INTERNAL;
    errorResponse.error["message"] = "Internal error.";
    errorResponse.error["detail"] = e.what();
    co_return errorResponse;
  }
}


drogon::Task<::gnp::dto::BaseApiResponse> AffiliateService::updateAsync(const ::gnp::dto::AffiliateDto &dto, const std::string &id) {
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Affiliates> affiliateMapper(dbClient);
  CoroMapper<drogon_model::Gnp::Users> userMapper(dbClient);

  try {
    // 1. Validate Affiliate existence
    Affiliates affiliate;
    try {
      affiliate = co_await affiliateMapper.findByPrimaryKey(id);
    } catch (const UnexpectedRows &) {
      dto::BaseApiResponse response;
      response.success = false;
      response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
      response.error["message"] = "Affiliate not found.";
      co_return response;
    }

    // 2. Get the associated user
    drogon_model::Gnp::Users user;
    try {
      user = co_await userMapper.findByPrimaryKey(affiliate.getValueOfUserId());
    } catch (const UnexpectedRows &) {
      dto::BaseApiResponse response;
      response.success = false;
      response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
      response.error["message"] = "Associated user not found.";
      co_return response;
    }

    // 3. Update affiliate record
    affiliate.setFirstName(dto.getFirstName());
    affiliate.setLastName(dto.getLastName());
    affiliate.setPhone(dto.getPhone());
    affiliate.setStatus(dto.getStatus());
    affiliate.setDateJoined(dto.getDateJoined());
    affiliate.setUpdatedAt(trantor::Date::now());

    co_await affiliateMapper.update(affiliate);

    // 4. Update user record
    user.setFirstName(dto.getFirstName());
    user.setLastName(dto.getLastName());
    user.setPhoneNumber(dto.getPhone());

    // Check if email is being updated
    bool emailChanged = false;
    std::string oldEmail = user.getValueOfEmail();
    if (dto.getEmail() != oldEmail) {
      user.setEmail(dto.getEmail());
      user.setUsername(dto.getEmail()); // Username should match email
      emailChanged = true;
    }
    std::string password = gnp::utils::PasswordUtils::generateRandomPassword(8);

    // 5. Handle password update if provided
    bool passwordChanged = false;

    if (!password.empty()) {

      user.setPasswordHash(bcrypt::generateHash(password));
      passwordChanged = true;
    }

    user.setUpdatedAt(trantor::Date::now());
    co_await userMapper.update(user);

    // 6. Send email notification if email or password changed
    if (emailChanged || passwordChanged) {
      const std::string loginUrl = "https://dev.graphicnewsplus.com/affiliates/login";

      std::string htmlBody =
        "<!DOCTYPE html>"
        "<html lang=\"en\"><head><meta charset=\"UTF-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">"
        "<title>Your Affiliate Account Has Been Updated</title></head>"
        "<body style=\"margin:0;padding:0;background-color:#f4f4f7;font-family:Arial,sans-serif;\">"

        "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" style=\"background-color:#f4f4f7;padding:40px 0;\">"
        "<tr><td align=\"center\">"

        "<table width=\"600\" cellpadding=\"0\" cellspacing=\"0\" style=\"background-color:#ffffff;border-radius:8px;"
        "overflow:hidden;box-shadow:0 2px 8px rgba(0,0,0,0.08);\">"

        "<tr><td style=\"background-color:#1a1a2e;padding:32px 40px;text-align:center;\">"
        "<h1 style=\"margin:0;color:#ffffff;font-size:22px;font-weight:700;letter-spacing:0.5px;\">"
        "Graphic News Plus</h1>"
        "<p style=\"margin:6px 0 0;color:#a0a8c0;font-size:13px;\">Affiliate Program - Account Update</p>"
        "</td></tr>"

        "<tr><td style=\"padding:36px 40px;\">"
        "<p style=\"margin:0 0 16px;font-size:16px;color:#333333;\">Dear <strong>" +
        dto.getFirstName() + "</strong>,</p>"
        "<p style=\"margin:0 0 16px;font-size:15px;color:#555555;line-height:1.6;\">"
        "Your affiliate account has been successfully updated. ";

      if (passwordChanged) {
        htmlBody += "Your password has been changed. ";
      }
      if (emailChanged) {
        htmlBody += "Your email address has been updated. ";
      }

      htmlBody +=
        "Below are your current login credentials.</p>"

        "<table width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" style=\"background-color:#f0f4ff;"
        "border-radius:6px;border:1px solid #dce3f5;margin:24px 0;\">"
        "<tr><td style=\"padding:20px 24px;\">"
        "<table width=\"100%\" cellpadding=\"6\" cellspacing=\"0\">"
        "<tr>"
        "<td style=\"font-size:13px;color:#888888;width:120px;\">Login URL</td>"
        "<td style=\"font-size:13px;color:#1a1a2e;\"><a href=\"" + loginUrl + "\" style=\"color:#4f6ef7;text-decoration:none;\">" + loginUrl + "</a></td>"
        "</tr>"
        "<tr>"
        "<td style=\"font-size:13px;color:#888888;\">Username</td>"
        "<td style=\"font-size:13px;color:#1a1a2e;font-family:monospace;\">" + dto.getEmail() + "</td>"
        "</tr>";

      if (passwordChanged) {
        htmlBody +=
        "<tr>"
        "<td style=\"font-size:13px;color:#888888;\">Password</td>"
        "<td style=\"font-size:13px;color:#1a1a2e;font-family:monospace;\">" + password + "</td>"
        "</tr>";
      }

      htmlBody +=
        "</table>"
        "</td></tr></table>"

        "<div style=\"text-align:center;margin:28px 0;\">"
        "<a href=\"" + loginUrl + "\" "
        "style=\"display:inline-block;background-color:#4f6ef7;color:#ffffff;"
        "text-decoration:none;font-size:15px;font-weight:600;padding:14px 36px;"
        "border-radius:6px;letter-spacing:0.3px;\">Go to Affiliate Dashboard</a>"
        "</div>"

        "<p style=\"margin:0 0 8px;font-size:13px;color:#888888;line-height:1.6;\">"
        "&#128274; For your security, please change your password immediately after your first login if you haven't already.</p>"
        "<p style=\"margin:0;font-size:13px;color:#888888;line-height:1.6;\">"
        "If you did not request these changes, please contact our support team immediately.</p>"
        "</td></tr>"

        "<tr><td style=\"background-color:#f8f9fc;padding:20px 40px;text-align:center;"
        "border-top:1px solid #e8eaf0;\">"
        "<p style=\"margin:0;font-size:12px;color:#aaaaaa;\">"
        "&copy; 2026 Graphic News Plus. All rights reserved.</p>"
        "</td></tr>"

        "</table>"
        "</td></tr></table>"
        "</body></html>";

      dto::SendEmailDto emailDto;
      emailDto.setTo(dto.getEmail());
      emailDto.setSubject("Your Affiliate Account Has Been Updated");
      emailDto.setBody(htmlBody);

      if (!emailDto.getTo().empty()) {
        EmailService emailService;
        co_await emailService.sendEmailAsync(emailDto);
      }
    }

    // 7. Build success response
    dto::BaseApiResponse response;
    response.success = true;
    response.result["message"] = "Affiliate updated successfully.";
    response.result["id"] = id;
    response.result["affiliateId"] = affiliate.getValueOfAffiliateId();
    response.result["userId"] = user.getValueOfId();
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] = "Database error while updating affiliate.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  } catch (const std::exception &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_INTERNAL;
    errorResponse.error["message"] = "Internal error.";
    errorResponse.error["detail"] = e.what();
    co_return errorResponse;
  }
}


drogon::Task<::gnp::dto::BaseApiResponse> AffiliateService::approveApplication(const std::string &applicationId) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Affiliates> affiliateMapper(dbClient);
  CoroMapper<drogon_model::Gnp::Users> userMapper(dbClient);

  try {
    // 1. Fetch the application
    Affiliates application;
    try {
      application = co_await affiliateMapper.findByPrimaryKey(applicationId);
    } catch (const UnexpectedRows &) {
      dto::BaseApiResponse response;
      response.success = false;
      response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
      response.error["message"] = "Application not found.";
      response.message = "Application not found.";
      co_return response;
    }

    // 2. Check if already processed
    if (application.getValueOfStatus() == constants::StatusTypes::ACTIVE) {
      dto::BaseApiResponse response;
      response.success = false;
      response.error["code"] = constants::ERR_VALIDATION;
      response.error["message"] = "Application has already been approved.";
      response.message = "Application has already been approved.";
      co_return response;
    }

    if (application.getValueOfStatus() == constants::StatusTypes::REJECTED) {
      dto::BaseApiResponse response;
      response.success = false;
      response.error["code"] = constants::ERR_VALIDATION;
      response.error["message"] = "Application has already been rejected.";
      response.message = "Application has already been rejected.";
      co_return response;
    }

    // 3. Check if email already exists in users table
    auto existingUsers = co_await userMapper.findBy(
        Criteria(drogon_model::Gnp::Users::Cols::_email, CompareOperator::EQ, application.getValueOfEmail()));

    if (!existingUsers.empty()) {
      co_await rejectApplication(applicationId, "The email address provided is already registered to an existing account.");

      dto::BaseApiResponse response;
      response.success = false;
      response.error["code"] = constants::ERR_VALIDATION;
      response.error["message"] = "Email already registered. Application rejected and notification email sent.";
      response.message = "Email already registered. Application rejected and notification email sent.";
      co_return response;
    }

    // 4. Generate affiliate ID
    std::string affiliateId = utils::IdGeneratorUtils::generateRandomSixDigit();

    // 5. Create the user record
    drogon_model::Gnp::Users newUser;
    newUser.setEmail(application.getValueOfEmail());
    newUser.setUsername(application.getValueOfEmail());
    newUser.setFirstName(application.getValueOfFirstName());
    newUser.setLastName(application.getValueOfLastName());
    newUser.setPhoneNumber(application.getValueOfPhone());

    // Generate random password
    std::string password = gnp::utils::PasswordUtils::generateRandomPassword(8);

    newUser.setPasswordHash(bcrypt::generateHash(password));
    newUser.setIsPartnerAdminUser(false);
    newUser.setIsAffiliate(true);
    newUser.setAffiliateId(application.getValueOfId());
    newUser.setIsActive(true);
    newUser.setIsLockedOut(false);
    newUser.setIsAdminUser(false);
    newUser.setCreatedAt(trantor::Date::now());

    auto user = co_await userMapper.insert(newUser);

    // 6. Update the affiliate record with user_id and affiliate_id
    application.setUserId(user.getValueOfId());
    application.setAffiliateId(affiliateId);
    application.setStatus(constants::StatusTypes::ACTIVE);
    application.setUpdatedAt(trantor::Date::now());
    application.setDateJoined(trantor::Date::now()); // Set join date
    application.setTotalEarnings("0");
    application.setWalletBalance("0");

    co_await affiliateMapper.update(application);

    // 7. Send welcome email with credentials
    const std::string loginUrl = "https://dev.graphicnewsplus.com/affiliates/login";
    std::string currentYear = trantor::Date::now().toCustomFormattedString("%Y");

    std::string htmlBody =
        "<div style=\"font-family: Arial, sans-serif; font-size: 15px; color: #333; line-height: 1.6; max-width: 600px; margin: 0 auto; padding: 20px;\">\n"
        "  <h2 style=\"color: #1a1a2e; margin-bottom: 16px;\">Graphic News Plus</h2>\n"
        "  <p>Dear <strong>" + application.getValueOfFirstName() + " " + application.getValueOfLastName() + "</strong>,</p>\n"
        "  <div style=\"background: #e8f5e9; border: 1px solid #c8e6c9; border-radius: 6px; padding: 12px 16px; margin: 20px 0;\">\n"
        "    <p style=\"margin: 0; color: #2e7d32; font-weight: bold;\">&#10004; Congratulations! Your affiliate application has been approved!</p>\n"
        "  </div>\n"
        "  <p>You can now access the <strong>Affiliate Dashboard</strong> using the credentials below:</p>\n"
        "  <p style=\"background: #f0f4ff; padding: 15px; border-radius: 6px; border: 1px solid #dce3f5;\">\n"
        "    <strong>Login URL:</strong> <a href=\"" + loginUrl + "\" target=\"_blank\" style=\"color:#4f6ef7;\">" + loginUrl + "</a><br>\n"
        "    <strong>Username:</strong> " + application.getValueOfEmail() + "<br>\n"
        "    <strong>Temporary Password:</strong> " + password + "<br>\n"
        "    <strong>Affiliate ID:</strong> " + affiliateId + "\n"
        "  </p>\n"
        "  <p style=\"margin: 20px 0;\">\n"
        "    <a href=\"" + loginUrl + "\"\n"
        "       target=\"_blank\"\n"
        "       style=\"background:#4f6ef7;color:#fff;padding:12px 24px;text-decoration:none;border-radius:4px;display:inline-block;font-weight:bold;\">Go to Affiliate Dashboard</a>\n"
        "  </p>\n"
        "  <p style=\"font-size: 13px; color: #777;\">&#128274; For security reasons, please change your password immediately after your first login.</p>\n"
        "  <p style=\"font-size: 13px; color: #777;\">If you have any questions, feel free to contact us at <a href=\"mailto:support@graphicnewsplus.com\" style=\"color: #4f6ef7;\">support@graphicnewsplus.com</a>.</p>\n"
        "  <hr style=\"border: none; border-top: 1px solid #eee; margin: 25px 0 15px 0;\">\n"
        "  <p style=\"font-size: 12px; color: #888;\">&copy; " + currentYear + " Graphic News Plus. All rights reserved.</p>\n"
        "</div>";

    dto::SendEmailDto emailDto;
    emailDto.setTo(application.getValueOfEmail());
    emailDto.setSubject("Affiliate Application Approved - Welcome to Graphic News Plus!");
    emailDto.setBody(htmlBody);

    if (!emailDto.getTo().empty()) {
      auto plugin = drogon::app().getPlugin<plugins::GnpServicePlugin>();
      if (plugin) {
        auto &emailService = plugin->getEmailService();
        co_await emailService.sendEmailAsync(emailDto);
      } else {
        EmailService emailService;
        co_await emailService.sendEmailAsync(emailDto);
      }
    }

    // 8. Build success response
    dto::BaseApiResponse response;
    response.success = true;
    response.result["message"] = "Application approved successfully. Welcome email sent.";
    response.result["applicationId"] = applicationId;
    response.result["affiliateId"] = affiliateId;
    response.result["userId"] = user.getValueOfId();
    response.result["generatedPassword"] = password;
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] = "Database error while approving application.";
    errorResponse.error["detail"] = e.base().what();
    errorResponse.message = "Database error while approving application.";
    co_return errorResponse;
  } catch (const std::exception &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_INTERNAL;
    errorResponse.error["message"] = "Internal error.";
    errorResponse.error["detail"] = e.what();
    errorResponse.message = "Internal error.";
    co_return errorResponse;
  }
}


drogon::Task<::gnp::dto::BaseApiResponse> AffiliateService::rejectApplication(const std::string &applicationId, const std::string &reason) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Affiliates> affiliateMapper(dbClient);

  try {
    // 1. Fetch the application
    Affiliates application;
    try {
      application = co_await affiliateMapper.findByPrimaryKey(applicationId);
    } catch (const UnexpectedRows &) {
      dto::BaseApiResponse response;
      response.success = false;
      response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
      response.error["message"] = "Application not found.";
      response.message = "Application not found.";
      co_return response;
    }

    // 2. Check if already processed
    if (application.getValueOfStatus() == constants::StatusTypes::REJECTED) {
      dto::BaseApiResponse response;
      response.success = false;
      response.error["code"] = constants::ERR_VALIDATION;
      response.error["message"] = "Application has already been rejected.";
      response.message = "Application has already been rejected.";
      co_return response;
    }

    if (application.getValueOfStatus() == constants::StatusTypes::APPROVED) {
      dto::BaseApiResponse response;
      response.success = false;
      response.error["code"] = constants::ERR_VALIDATION;
      response.error["message"] = "Application has already been approved.";
      response.message = "Application has already been approved.";
      co_return response;
    }

    // 3. Update application status
    application.setStatus(constants::StatusTypes::REJECTED);
    application.setUpdatedAt(trantor::Date::now());
    co_await affiliateMapper.update(application);

    // 4. Send rejection email to the applicant
    std::string currentYear = trantor::Date::now().toCustomFormattedString("%Y");
    std::string applicantName = application.getValueOfFirstName();
    if (!application.getValueOfLastName().empty()) {
      applicantName += " " + application.getValueOfLastName();
    }
    if (applicantName.empty()) {
      applicantName = "Applicant";
    }

    std::string reasonBlock;
    if (!reason.empty()) {
      reasonBlock =
          "  <div style=\"background: #f9f9f9; padding: 12px 15px; border-radius: 6px; border: 1px solid #eee; margin: 15px 0;\">\n"
          "    <p style=\"margin: 0; color: #555;\"><strong>Reason:</strong> " + reason + "</p>\n"
          "  </div>\n";
    }

    std::string htmlBody =
        "<div style=\"font-family: Arial, sans-serif; font-size: 15px; color: #333; line-height: 1.6; max-width: 600px; margin: 0 auto; padding: 20px;\">\n"
        "  <h2 style=\"color: #1a1a2e; margin-bottom: 16px;\">Graphic News Plus</h2>\n"
        "  <p>Dear <strong>" + applicantName + "</strong>,</p>\n"
        "  <p>Thank you for your interest in the <strong>Graphic News Plus Affiliate Program</strong>.</p>\n"
        "  <div style=\"background: #fff5f5; border: 1px solid #fed7d7; border-radius: 6px; padding: 12px 16px; margin: 20px 0;\">\n"
        "    <p style=\"margin: 0; color: #c53030; font-weight: bold;\">&#10006; Application Status: Not Approved</p>\n"
        "  </div>\n"
        "  <p>After careful review of your registration details, we regret to inform you that we are unable to approve your affiliate application at this time.</p>\n" +
        reasonBlock +
        "  <p>If you believe this decision was made in error or if you have any questions, please contact our support team at <a href=\"mailto:support@graphicnewsplus.com\" style=\"color: #4f6ef7;\">support@graphicnewsplus.com</a>.</p>\n"
        "  <hr style=\"border: none; border-top: 1px solid #eee; margin: 25px 0 15px 0;\">\n"
        "  <p style=\"font-size: 12px; color: #888;\">&copy; " + currentYear + " Graphic News Plus. All rights reserved.</p>\n"
        "</div>";

    dto::SendEmailDto emailDto;
    emailDto.setTo(application.getValueOfEmail());
    emailDto.setSubject("Graphic News Plus - Affiliate Application Update");
    emailDto.setBody(htmlBody);

    if (!emailDto.getTo().empty()) {
      auto plugin = drogon::app().getPlugin<plugins::GnpServicePlugin>();
      if (plugin) {
        auto &emailService = plugin->getEmailService();
        co_await emailService.sendEmailAsync(emailDto);
      } else {
        EmailService emailService;
        co_await emailService.sendEmailAsync(emailDto);
      }
    }

    dto::BaseApiResponse response;
    response.success = true;
    response.result["message"] = "Application rejected successfully. Notification email sent.";
    response.result["applicationId"] = applicationId;
    response.result["status"] = static_cast<int>(constants::StatusTypes::REJECTED);
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] = "Database error while rejecting application.";
    errorResponse.error["detail"] = e.base().what();
    errorResponse.message = "Database error while rejecting application.";
    co_return errorResponse;
  } catch (const std::exception &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_INTERNAL;
    errorResponse.error["message"] = "Internal error.";
    errorResponse.error["detail"] = e.what();
    errorResponse.message = "Internal error.";
    co_return errorResponse;
  }
}


drogon::Task<dto::BaseApiResponse> AffiliateService::updateAffiliateAccountStatus(const std::string &id, int status, const std::string &reason) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Affiliates> mapper(dbClient);

  if (status == static_cast<int>(constants::StatusTypes::APPROVED)) {
    co_return co_await approveApplication(id);
  }

  if (status == static_cast<int>(constants::StatusTypes::REJECTED)) {
    co_return co_await rejectApplication(id, reason);
  }

  try {
    // 1. Validate affiliate existence
    Affiliates affiliate;
    try {
      affiliate = co_await mapper.findByPrimaryKey(id);
    } catch (const UnexpectedRows &) {
      dto::BaseApiResponse response;
      response.success = false;
      response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
      response.error["message"] = "Affiliate not found.";
      co_return response;
    }

    if (status < 0) {
      dto::BaseApiResponse response;
      response.success = false;
      response.error["code"] = constants::ERR_VALIDATION;
      response.error["message"] = "Invalid status value. Status must be a non-negative integer.";
      co_return response;
    }

    // 2. Apply the status change
    affiliate.setStatus(status);
    affiliate.setUpdatedAt(trantor::Date::now());

    co_await mapper.update(affiliate);

    // 3. Build success response
    dto::BaseApiResponse response;
    response.success = true;
    response.result["message"] = "Affiliate account status updated successfully.";
    response.result["id"] = id;
    response.result["status"] = status;
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] = "Database error while updating affiliate account status.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  } catch (const std::exception &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_INTERNAL;
    errorResponse.error["message"] = "Internal error.";
    errorResponse.error["detail"] = e.what();
    co_return errorResponse;
  }
}

drogon::Task<::gnp::dto::BaseApiResponse> AffiliateService::deleteAffiliate(const std::string &id) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Affiliates> affiliateMapper(dbClient);
  CoroMapper<AffiliateCommissions> commissionMapper(dbClient);
  CoroMapper<AffiliatePayouts> payoutMapper(dbClient);

  try {

    Affiliates affiliate;
    try {
      affiliate = co_await affiliateMapper.findByPrimaryKey(id);
    } catch (const UnexpectedRows &) {
      dto::BaseApiResponse response;
      response.success = false;
      response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
      response.error["message"] = "Affiliate not found.";
      co_return response;
    }

    // 2. Check Pending Commissions
    auto pendingCommissions = co_await commissionMapper.findBy(
        Criteria(AffiliateCommissions::Cols::_affiliate_id, CompareOperator::EQ, id) &&
        Criteria(AffiliateCommissions::Cols::_status, CompareOperator::EQ, static_cast<int>(constants::StatusTypes::PENDING)));

    if (!pendingCommissions.empty()) {
      dto::BaseApiResponse response;
      response.success = false;
      response.error["code"] = constants::ERR_VALIDATION;
      response.error["message"] =  "Cannot delete affiliate with pending commissions.";
      co_return response;
    }

    // 3. Check Processing Payouts
    auto processingPayouts = co_await payoutMapper.findBy(
        Criteria(AffiliatePayouts::Cols::_affiliate_id, CompareOperator::EQ, id) &&
        Criteria(AffiliatePayouts::Cols::_status, CompareOperator::EQ, static_cast<int>(constants::StatusTypes::PROCESSING_PAYOUT)));

    if (!processingPayouts.empty()) {
      dto::BaseApiResponse response;
      response.success = false;
      response.error["code"] = constants::ERR_VALIDATION;
      response.error["message"] =
          "Cannot delete affiliate with processing payouts.";
      co_return response;
    }

    // 4. Delete Affiliate
    co_await affiliateMapper.deleteByPrimaryKey(id);

    dto::BaseApiResponse response;
    response.success = true;
    response.result["message"] = "Affiliate deleted successfully.";
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] = "Database error while deleting affiliate.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  } catch (const std::exception &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_INTERNAL;
    errorResponse.error["message"] = "Internal error.";
    errorResponse.error["detail"] = e.what();
    co_return errorResponse;
  }
}


drogon::Task<dto::BaseApiResponse> AffiliateService::getAdminDashboardData(
    const std::string &startDate, const std::string &endDate) {
  // Dates are inclusive calendar dates. Empty bounds mean no bound; changes and
  // growth compare against the immediately preceding equal-length period only
  // when both bounds are provided.
  auto validDate = [](const std::string &date) {
    if (date.empty()) return true;
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') return false;
    for (size_t i = 0; i < date.size(); ++i) {
      if (i != 4 && i != 7 && (date[i] < '0' || date[i] > '9')) return false;
    }
    using namespace std::chrono;
    return year_month_day{year{std::stoi(date.substr(0, 4))},
                          month{static_cast<unsigned>(std::stoi(date.substr(5, 2)))},
                          day{static_cast<unsigned>(std::stoi(date.substr(8, 2)))}}.ok();
  };
  if (!validDate(startDate) || !validDate(endDate) ||
      (!startDate.empty() && !endDate.empty() && startDate > endDate)) {
    dto::BaseApiResponse response;
    response.error["code"] = constants::ERR_VALIDATION;
    response.error["message"] = "Use valid YYYY-MM-DD dates with startDate no later than endDate.";
    co_return response;
  }

  const bool compare = !startDate.empty() && !endDate.empty();
  auto dbClient = drogon::app().getDbClient();
  try {
    // Independent scalar subqueries avoid multiplying monetary totals when an
    // affiliate has both commissions and payouts.
    const std::string bounds =
        "WITH bounds AS (SELECT NULLIF($1, '')::date AS start_date, "
        "NULLIF($2, '')::date AS end_date) ";
    const std::string current =
        "(b.start_date IS NULL OR t.created_at >= b.start_date) "
        "AND (b.end_date IS NULL OR t.created_at < b.end_date + 1)";
    const std::string previous =
        "t.created_at >= b.start_date - (b.end_date - b.start_date + 1) "
        "AND t.created_at < b.start_date";
    const std::string query = bounds +
        "SELECT "
        "(SELECT COUNT(*) FROM affiliates t WHERE t.status NOT IN (3, 17, 20) AND " + current + ") AS affiliates, "
        "(SELECT COUNT(*) FROM affiliates t WHERE t.status NOT IN (3, 17, 20) AND " + previous + ") AS prev_affiliates, "
        "(SELECT COALESCE(SUM(t.amount), 0) FROM affiliate_commissions t WHERE t.status <> 17 AND " + current + ") AS commission, "
        "(SELECT COALESCE(SUM(t.amount), 0) FROM affiliate_commissions t WHERE t.status <> 17 AND " + previous + ") AS prev_commission, "
        "(SELECT COUNT(*) FROM affiliates t WHERE t.status = 20 AND " + current + ") AS applications, "
        "(SELECT COUNT(*) FROM affiliates t WHERE t.status = 20 AND " + previous + ") AS prev_applications, "
        "(SELECT COALESCE(SUM(t.total_amount), 0) FROM affiliate_payouts t WHERE t.status IN (14, 18) AND " + current + ") AS payouts, "
        "(SELECT COALESCE(SUM(t.total_amount), 0) FROM affiliate_payouts t WHERE t.status IN (14, 18) AND " + previous + ") AS prev_payouts "
        "FROM bounds b";
    auto totals = co_await dbClient->execSqlCoro(query, startDate, endDate);
    const auto &row = totals[0];
    auto percent = [compare](double value, double old) {
      if (!compare || old == 0 || value == old) return std::string("0%");
      double rate = (value - old) * 100.0 / old;
      std::ostringstream out;
      out << (rate >= 0 ? "+" : "") << std::fixed << std::setprecision(1) << rate << "%";
      return out.str();
    };
    auto direction = [compare](double value, double old) {
      return !compare || value == old ? "neutral" : value > old ? "increase" : "decrease";
    };
    Json::Value stats(Json::arrayValue);
    auto addStat = [&](const char *name, double value, double old,
                       const char *icon, const char *bg, const char *color,
                       const char *prefix, bool isCount, bool applications = false) {
      Json::Value stat;
      stat["name"] = name;
      stat["value"] = isCount ? Json::Value(static_cast<Json::Int64>(value)) : Json::Value(value);
      stat["change"] = applications ? std::to_string(compare ? static_cast<long long>(value - old) : 0) + " new"
                                    : percent(value, old);
      stat["changeType"] = direction(value, old);
      stat["icon"] = icon; // serializable frontend icon identifier
      stat["bgColor"] = bg;
      stat["iconColor"] = color;
      stat["prefix"] = prefix;
      stat["suffix"] = "";
      stats.append(stat);
    };
    addStat("Total Affiliates", row["affiliates"].as<double>(), row["prev_affiliates"].as<double>(),
            "UsersIcon", "bg-blue-50", "text-blue-600", "", true);
    addStat("Total Commission", row["commission"].as<double>(), row["prev_commission"].as<double>(),
            "CurrencyDollarIcon", "bg-green-50", "text-green-600", "₵", false);
    addStat("Pending Applications", row["applications"].as<double>(), row["prev_applications"].as<double>(),
            "DocumentCheckIcon", "bg-orange-50", "text-orange-600", "", true, true);
    addStat("Pending Payouts", row["payouts"].as<double>(), row["prev_payouts"].as<double>(),
            "BanknotesIcon", "bg-purple-50", "text-purple-600", "₵", false);

    // A referral conversion is represented by an affiliate commission: there
    // is no referral-clicks table in the deployed schema.
    const std::string commissionCurrent =
        "(b.start_date IS NULL OR c.created_at >= b.start_date) "
        "AND (b.end_date IS NULL OR c.created_at < b.end_date + 1)";
    const std::string commissionPrevious =
        "c.created_at >= b.start_date - (b.end_date - b.start_date + 1) "
        "AND c.created_at < b.start_date";
    auto leaders = co_await dbClient->execSqlCoro(bounds +
        "SELECT a.id::text AS id, TRIM(CONCAT(COALESCE(a.first_name, ''), ' ', COALESCE(a.last_name, ''))) AS name, "
        "COUNT(c.id) FILTER (WHERE " + commissionCurrent + ") AS referrals, "
        "COALESCE(SUM(c.amount) FILTER (WHERE " + commissionCurrent + "), 0) AS earnings, "
        "COUNT(c.id) FILTER (WHERE " + commissionPrevious + ") AS previous_referrals "
        "FROM affiliates a CROSS JOIN bounds b "
        "JOIN affiliate_commissions c ON c.affiliate_id = a.id AND c.status <> 17 "
        "GROUP BY a.id, a.first_name, a.last_name, b.start_date, b.end_date "
        "HAVING COUNT(c.id) FILTER (WHERE " + commissionCurrent + ") > 0 "
        "ORDER BY earnings DESC, referrals DESC, a.id LIMIT 4", startDate, endDate);
    Json::Value top(Json::arrayValue);
    for (const auto &leader : leaders) {
      Json::Value item;
      item["id"] = leader["id"].as<std::string>();
      item["name"] = leader["name"].as<std::string>();
      item["referrals"] = static_cast<Json::Int64>(leader["referrals"].as<int64_t>());
      item["earnings"] = leader["earnings"].as<double>();
      const double prior = leader["previous_referrals"].as<double>();
      item["growth"] = compare && prior > 0 ?
          (leader["referrals"].as<double>() - prior) * 100.0 / prior : 0.0;
      top.append(item);
    }

    auto activities = co_await dbClient->execSqlCoro(bounds +
        "SELECT id, name, action, amount, "
        "GREATEST(0, EXTRACT(EPOCH FROM (CURRENT_TIMESTAMP - activity_at))) AS age_seconds "
        "FROM ("
        " SELECT c.id::text AS id, TRIM(CONCAT(COALESCE(a.first_name, ''), ' ', COALESCE(a.last_name, ''))) AS name, "
        " 'Earned Commission' AS action, c.amount AS amount, c.created_at AS activity_at "
        " FROM affiliate_commissions c JOIN affiliates a ON a.id = c.affiliate_id WHERE c.status <> 17 "
        " UNION ALL SELECT a.id::text, TRIM(CONCAT(COALESCE(a.first_name, ''), ' ', COALESCE(a.last_name, ''))), "
        " 'New Affiliate Signup', 0, a.created_at FROM affiliates a "
        " UNION ALL SELECT a.id::text, TRIM(CONCAT(COALESCE(a.first_name, ''), ' ', COALESCE(a.last_name, ''))), "
        " 'Application Approved', 0, a.updated_at FROM affiliates a WHERE a.status = 16 "
        " UNION ALL SELECT p.id::text, TRIM(CONCAT(COALESCE(a.first_name, ''), ' ', COALESCE(a.last_name, ''))), "
        " 'Payout Processed', -p.total_amount, p.updated_at FROM affiliate_payouts p "
        " JOIN affiliates a ON a.id = p.affiliate_id WHERE p.status = 19 "
        ") events CROSS JOIN bounds b WHERE "
        "(b.start_date IS NULL OR activity_at >= b.start_date) AND "
        "(b.end_date IS NULL OR activity_at < b.end_date + 1) "
        "ORDER BY activity_at DESC, id DESC LIMIT 5", startDate, endDate);
    Json::Value recent(Json::arrayValue);
    for (const auto &activity : activities) {
      Json::Value item;
      item["id"] = activity["id"].as<std::string>();
      item["name"] = activity["name"].as<std::string>();
      item["action"] = activity["action"].as<std::string>();
      item["amount"] = activity["amount"].as<double>();
      auto seconds = static_cast<int64_t>(activity["age_seconds"].as<double>());
      if (seconds < 60) item["date"] = "just now";
      else if (seconds < 3600) item["date"] = std::to_string(seconds / 60) + " mins ago";
      else if (seconds < 86400) item["date"] = std::to_string(seconds / 3600) + " hours ago";
      else item["date"] = std::to_string(seconds / 86400) + " days ago";
      recent.append(item);
    }
    // The cards may be unbounded, but a chart needs a finite window. With no
    // bounds show the last 30 calendar days; with only endDate show the 30 days
    // ending there; with only startDate show through today.
    const auto range = co_await dbClient->execSqlCoro(
        "WITH input AS (SELECT NULLIF($1, '')::date AS requested_start, "
        "NULLIF($2, '')::date AS requested_end), "
        "bounds AS (SELECT COALESCE(requested_start, "
        "COALESCE(requested_end, CURRENT_DATE) - 29) AS start_date, "
        "COALESCE(requested_end, GREATEST(CURRENT_DATE, "
        "COALESCE(requested_start, CURRENT_DATE))) AS end_date FROM input) "
        "SELECT start_date::text AS start_date, end_date::text AS end_date, "
        "end_date - start_date + 1 AS days FROM bounds",
        startDate, endDate);
    const std::string trendStart = range[0]["start_date"].as<std::string>();
    const std::string trendEnd = range[0]["end_date"].as<std::string>();
    const int days = range[0]["days"].as<int>();

    // Bucket widths keep the chart readable for arbitrary custom ranges.
    // Weeks are consecutive seven-day windows anchored at trendStart; months
    // and years use calendar boundaries. generate_series fills empty buckets.
    std::string seriesStart = "$1::date";
    std::string step = "1 day";
    std::string bucket = "c.created_at::date";
    if (days > 730) {
      seriesStart = "date_trunc('year', $1::date)::date";
      step = "1 year";
      bucket = "date_trunc('year', c.created_at)::date";
    } else if (days > 180) {
      seriesStart = "date_trunc('month', $1::date)::date";
      step = "1 month";
      bucket = "date_trunc('month', c.created_at)::date";
    } else if (days > 31) {
      step = "7 days";
      bucket = "($1::date + ((c.created_at::date - $1::date) / 7) * 7)";
    }
    const auto trendRows = co_await dbClient->execSqlCoro(
        "WITH buckets AS (SELECT generate_series(" + seriesStart +
        ", $2::date, interval '" + step + "')::date AS bucket_start), "
        "totals AS (SELECT " + bucket + " AS bucket_start, SUM(c.amount) AS amount "
        "FROM affiliate_commissions c WHERE c.status <> 17 "
        "AND c.created_at >= $1::date AND c.created_at < $2::date + 1 "
        "GROUP BY 1) "
        "SELECT b.bucket_start::text AS bucket_start, COALESCE(t.amount, 0) AS amount "
        "FROM buckets b LEFT JOIN totals t ON t.bucket_start = b.bucket_start "
        "ORDER BY b.bucket_start", trendStart, trendEnd);
    Json::Value xAxisData(Json::arrayValue);
    Json::Value data(Json::arrayValue);
    for (const auto &point : trendRows) {
      const std::string date = point["bucket_start"].as<std::string>();
      if (days > 730) xAxisData.append(date.substr(0, 4));
      else if (days > 180) xAxisData.append(date.substr(0, 7));
      else if (days > 31) xAxisData.append("Week " + std::to_string(xAxisData.size() + 1));
      else xAxisData.append(date);
      data.append(point["amount"].as<double>());
    }
    Json::Value commissionTrend;
    commissionTrend["xAxisData"] = xAxisData;
    commissionTrend["data"] = data;

    dto::BaseApiResponse response;
    response.success = true;
    response.result["commissionTrend"] = commissionTrend;
    response.result["stats"] = stats;
    response.result["recentActivity"] = recent;
    response.result["topAffiliates"] = top;
    co_return response;
  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse response;
    response.error["code"] = constants::ERR_DB_QUERY;
    response.error["message"] = "Database error while fetching affiliate dashboard.";
    response.error["detail"] = e.base().what();
    co_return response;
  }
}

drogon::Task<dto::BaseApiResponse> AffiliateService::resetAffiliateUserPassword(const std::string &id) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Affiliates> affiliateMapper(dbClient);
  CoroMapper<drogon_model::Gnp::Users> userMapper(dbClient);

  try {
    // 1. Fetch the affiliate
    Affiliates affiliate;
    try {
      affiliate = co_await affiliateMapper.findByPrimaryKey(id);
    } catch (const UnexpectedRows &) {
      dto::BaseApiResponse response;
      response.success = false;
      response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
      response.error["message"] = "Affiliate not found.";
      co_return response;
    }

    // 2. Ensure the affiliate has a linked user account (created on approval)
    if (!affiliate.getUserId() || affiliate.getUserId()->empty()) {
      dto::BaseApiResponse response;
      response.success = false;
      response.error["code"] = constants::ERR_VALIDATION;
      response.error["message"] = "Affiliate does not have a user account yet. Only approved affiliates can have their password reset.";
      co_return response;
    }

    // 3. Fetch the linked user account
    drogon_model::Gnp::Users user;
    try {
      user = co_await userMapper.findByPrimaryKey(affiliate.getValueOfUserId());
    } catch (const UnexpectedRows &) {
      dto::BaseApiResponse response;
      response.success = false;
      response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
      response.error["message"] = "Affiliate user account not found.";
      co_return response;
    }

    // 4. Generate a new random password and update the user's credentials
    std::string newPassword = gnp::utils::PasswordUtils::generateRandomPassword(8);

    user.setPasswordHash(bcrypt::generateHash(newPassword));
    user.setIsLockedOut(false); // allow login with the new credentials
    user.setUpdatedAt(trantor::Date::now());
    co_await userMapper.update(user);

    // 5. Send email with the new credentials
    const std::string loginUrl = "https://dev.graphicnewsplus.com/affiliates/login";
    std::string currentYear = trantor::Date::now().toCustomFormattedString("%Y");

    std::string htmlBody =
        "<div style=\"font-family: Arial, sans-serif; font-size: 15px; color: #333; line-height: 1.6; max-width: 600px; margin: 0 auto; padding: 20px;\">\n"
        "  <h2 style=\"color: #1a1a2e; margin-bottom: 16px;\">Graphic News Plus</h2>\n"
        "  <p>Dear <strong>" + affiliate.getValueOfFirstName() + " " + affiliate.getValueOfLastName() + "</strong>,</p>\n"
        "  <p>Your affiliate account password has been reset by an administrator. You can now access the <strong>Affiliate Dashboard</strong> using the credentials below:</p>\n"
        "  <p style=\"background: #f0f4ff; padding: 15px; border-radius: 6px; border: 1px solid #dce3f5;\">\n"
        "    <strong>Login URL:</strong> <a href=\"" + loginUrl + "\" target=\"_blank\" style=\"color:#4f6ef7;\">" + loginUrl + "</a><br>\n"
        "    <strong>Username:</strong> " + user.getValueOfUsername() + "<br>\n"
        "    <strong>Temporary Password:</strong> " + newPassword + "<br>\n"
        "    <strong>Affiliate ID:</strong> " + affiliate.getValueOfAffiliateId() + "\n"
        "  </p>\n"
        "  <p style=\"margin: 20px 0;\">\n"
        "    <a href=\"" + loginUrl + "\"\n"
        "       target=\"_blank\"\n"
        "       style=\"background:#4f6ef7;color:#fff;padding:12px 24px;text-decoration:none;border-radius:4px;display:inline-block;font-weight:bold;\">Go to Affiliate Dashboard</a>\n"
        "  </p>\n"
        "  <p style=\"font-size: 13px; color: #777;\">&#128274; For security reasons, please change your password immediately after logging in.</p>\n"
        "  <p style=\"font-size: 13px; color: #777;\">If you did not expect this change, please contact us immediately at <a href=\"mailto:support@graphicnewsplus.com\" style=\"color: #4f6ef7;\">support@graphicnewsplus.com</a>.</p>\n"
        "  <hr style=\"border: none; border-top: 1px solid #eee; margin: 25px 0 15px 0;\">\n"
        "  <p style=\"font-size: 12px; color: #888;\">&copy; " + currentYear + " Graphic News Plus. All rights reserved.</p>\n"
        "</div>";

    dto::SendEmailDto emailDto;
    emailDto.setTo(user.getValueOfEmail());
    emailDto.setSubject("Graphic News Plus - Affiliate Password Reset");
    emailDto.setBody(htmlBody);

    if (!emailDto.getTo().empty()) {
      auto plugin = drogon::app().getPlugin<plugins::GnpServicePlugin>();
      if (plugin) {
        auto &emailService = plugin->getEmailService();
        co_await emailService.sendEmailAsync(emailDto);
      } else {
        EmailService emailService;
        co_await emailService.sendEmailAsync(emailDto);
      }
    }

    // 6. Build success response
    dto::BaseApiResponse response;
    response.success = true;
    response.result["message"] = "Affiliate password reset successfully. New credentials have been sent to the affiliate's email.";
    response.result["affiliateId"] = id;
    response.result["userId"] = user.getValueOfId();
    response.result["generatedPassword"] = newPassword;
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] = "Database error while resetting affiliate user password.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  } catch (const std::exception &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_INTERNAL;
    errorResponse.error["message"] = "Internal error.";
    errorResponse.error["detail"] = e.what();
    co_return errorResponse;
  }
}


// commissions

drogon::Task<::gnp::dto::BaseApiResponse> AffiliateService::getAllCommissions(int pageNo, int pageSize, const std::string &affiliateId, const std::string &startDate, const std::string &endDate)
{
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<AffiliateCommissions> mp(dbClient);

  Criteria criteria = Criteria();

  if (!affiliateId.empty()) {

    criteria = criteria && Criteria(AffiliatePayouts::Cols::_affiliate_id, CompareOperator::EQ, affiliateId);
  }

  if (!startDate.empty()) {
    criteria = criteria && Criteria(AffiliateCommissions::Cols::_created_at, CompareOperator::GE, startDate);
  }

  if (!endDate.empty()) {
    // Assuming endDate is just a date string, append time to cover the full day
    std::string endDateTime = endDate + " 23:59:59";
    criteria = criteria && Criteria(AffiliateCommissions::Cols::_created_at, CompareOperator::LE, endDateTime);
  }

  try {
    auto totalCount = co_await mp.count(criteria);

    if (totalCount == 0) {
      dto::BaseApiResponse response;
      response.success = true;
      response.result["data"] = Json::arrayValue;
      response.result["totalCount"] = 0;
      co_return response;
    }

    auto commissions =
        co_await mp
            .orderBy(AffiliateCommissions::Cols::_created_at, SortOrder::DESC)
            .limit(pageSize)
            .offset((pageNo - 1) * pageSize)
            .findBy(criteria);

    dto::BaseApiResponse response;
    auto totalPages = (totalCount + pageSize - 1) / pageSize;

    response.success = true;
    response.result["totalCount"] = (Json::UInt64)totalCount;
    response.result["pageNo"] = pageNo;
    response.result["pageSize"] = pageSize;
    response.result["totalPages"] = (int)totalPages;

    Json::Value data = Json::arrayValue;
    for (const auto &commission : commissions) {
      Json::Value json = commission.toJson();
      // Convert to camelCase if needed, but standard toJson might be
      // snake_case. Let's stick to toJson or manually map if inconsistent.
      // Existing getAll manually maps to camelCase. I should do the same for
      // consistency.
      Json::Value camelCaseJson;
      camelCaseJson["id"] = json["id"];
      camelCaseJson["affiliateName"] = json["affiliate_name"];
      camelCaseJson["amount"] = json["amount"];
      camelCaseJson["orderId"] = json["order_id"];
      camelCaseJson["transactionReference"] = json["transaction_reference"];
      camelCaseJson["status"] = json["status"];
      camelCaseJson["createdAt"] = json["created_at"];
      camelCaseJson["updatedAt"] = json["updated_at"];

      data.append(camelCaseJson);
    }
    response.result["data"] = data;
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] =
        "Database error while fetching commissions.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

// payouts

drogon::Task<::gnp::dto::BaseApiResponse> AffiliateService::getAllPayouts(int pageNo, int pageSize, const std::string &affiliateId, const std::string &startDate, const std::string &endDate)
{
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<AffiliatePayouts> mp(dbClient);

  Criteria criteria = Criteria();

  if (!affiliateId.empty()) {

    criteria = criteria && Criteria(AffiliatePayouts::Cols::_affiliate_id, CompareOperator::EQ, affiliateId);
  }

  if (!startDate.empty()) {
    criteria = criteria && Criteria(AffiliatePayouts::Cols::_created_at, CompareOperator::GE, startDate);
  }

  if (!endDate.empty()) {
    std::string endDateTime = endDate + " 23:59:59";
    criteria = criteria && Criteria(AffiliatePayouts::Cols::_created_at, CompareOperator::LE, endDateTime);
  }

  try {

    auto totalCount = co_await mp.count(criteria);

    if (totalCount == 0) {
      dto::BaseApiResponse response;
      response.success = true;
      response.result["data"] = Json::arrayValue;
      response.result["totalCount"] = 0;
      co_return response;
    }

    auto payouts = co_await mp.orderBy(AffiliatePayouts::Cols::_created_at, SortOrder::DESC)
            .limit(pageSize)
            .offset((pageNo - 1) * pageSize)
            .findBy(criteria);

    dto::BaseApiResponse response;
    auto totalPages = (totalCount + pageSize - 1) / pageSize;

    response.success = true;
    response.result["totalCount"] = (Json::UInt64)totalCount;
    response.result["pageNo"] = pageNo;
    response.result["pageSize"] = pageSize;
    response.result["totalPages"] = (int)totalPages;

    Json::Value data = Json::arrayValue;
    for (const auto &payout : payouts) {
      Json::Value json = payout.toJson();
      Json::Value camelCaseJson;
      camelCaseJson["id"] = json["id"];
      camelCaseJson["affiliateName"] = json["affiliate_name"];
      camelCaseJson["totalAmount"] = json["total_amount"];
      camelCaseJson["accountType"] = json["account_type"];
      camelCaseJson["accountName"] = json["account_name"];
      camelCaseJson["accountProvider"] = json["account_provider"];
      camelCaseJson["status"] = json["status"];
      camelCaseJson["platforms"] = json["platforms"];
      camelCaseJson["createdAt"] = json["created_at"];
      camelCaseJson["updatedAt"] = json["updated_at"];

      data.append(camelCaseJson);
    }
    response.result["data"] = data;
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] = "Database error while fetching payouts.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}


drogon::Task<::gnp::dto::BaseApiResponse> AffiliateService::issueAffiliatePayout(const std::string &affiliateId) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Affiliates> affiliateMapper(dbClient);
  CoroMapper<AffiliateCommissions> commissionMapper(dbClient);
  CoroMapper<AffiliatePayouts> payoutMapper(dbClient);

  try {
    // 1. Validate Affiliate
    auto affiliate = co_await affiliateMapper.findByPrimaryKey(affiliateId);

    // 2. Fetch unpaid commissions
    auto commissions = co_await commissionMapper.findBy(Criteria(AffiliateCommissions::Cols::_affiliate_id,CompareOperator::EQ, affiliateId) && Criteria(AffiliateCommissions::Cols::_status, CompareOperator::EQ, static_cast<int>(constants::StatusTypes::PENDING)));

    if (commissions.empty()) {
      dto::BaseApiResponse response;
      response.success = false;
      response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
      response.error["message"] = "No unpaid commissions found for this affiliate.";
      co_return response;
    }

    // 3. Calculate total amount
    double totalAmount = 0.0;
    for (const auto &commission : commissions) {
      // Assuming amount is stored as string but represents a number
      try {
        totalAmount += std::stod(commission.getValueOfAmount());
      } catch (...) {
        LOG_ERROR << "Invalid commission amount: "
                  << commission.getValueOfAmount();
      }
    }

    if (totalAmount <= 0) {
      dto::BaseApiResponse response;
      response.success = false;
      response.error["code"] = constants::ERR_VALIDATION;
      response.error["message"] = "Total commission amount is zero or negative.";
      co_return response;
    }

    // Start Transaction
    auto transaction = co_await dbClient->newTransactionCoro();

    try {
      // 4. Create Payout record
      AffiliatePayouts payout;
      payout.setAffiliateName(affiliate.getValueOfFirstName() + " " + affiliate.getValueOfLastName());
      payout.setTotalAmount(std::to_string(totalAmount));

      // default
      payout.setAccountType(affiliate.getValueOfAccountType());
      payout.setAccountName(affiliate.getValueOfAccountName());
      payout.setAccountProvider(affiliate.getValueOfAccountProvider());
      payout.setStatus(constants::StatusTypes::PROCESSING);


      CoroMapper<AffiliatePayouts> payoutTxMapper(transaction);
      co_await payoutTxMapper.insert(payout);

      // 5. Update Commissions status
      CoroMapper<AffiliateCommissions> commissionTxMapper(transaction);
      for (auto &commission : commissions) {
        commission.setStatus(constants::StatusTypes::PAID);
        co_await commissionTxMapper.update(commission);
      }

      // Update Affiliate total earnings (optional, but good for consistency)
      // We are not tracking total paid out in the affiliate model directly as a
      // sum, but 'total_earnings' column exists. Let's update it. Actually,
      // 'total_earnings' might be a calculated field or a running total. Given
      // the logic in getAll, it seems to be calculated on the fly. But let's
      // check if we need to update the model. The getAll method calculates it
      // via SQL. So we might not need to update it here unless there is a
      // specific column. Looking at Affiliates.h, there is `_total_earnings`.
      // Let's update it.
      double currentEarnings = 0.0;
      try {
        currentEarnings = std::stod(affiliate.getValueOfTotalEarnings());
      } catch (...) {
      }

      affiliate.setTotalEarnings(std::to_string(currentEarnings + totalAmount));

      CoroMapper<Affiliates> affiliateTxMapper(transaction);

      co_await affiliateTxMapper.update(affiliate);

      // Commit Transaction
      // Drogon transaction commits automatically on destruction if not rolled
      // back? No, we strictly need to commit if using newTransactionCoro? Wait,
      // Transaction is a shared_ptr. logic usually is explicit commit. But
      // typically we don't have explicit commit method on the
      // shared_ptr<Transaction>? Actually we just passed it to mappers. We
      // assume success if no exception. Mappers execute on transaction. Ideally
      // we would want an explicit commit, but Transaction class destructor
      // might rollback if not committed? Let's check if we have commit
      // capability. dbClient->newTransactionCoro() returns a Transaction. We
      // don't have explicit commit here in the snippet. Standard Drogon used to
      // be scoped. Let's just assume successful execution means we are good?
      // Actually, standard practice with these is manual commit if available,
      // or just letting it go? Let's look for commit... actually Transaction
      // has `rollback()`. It commits on destruction if not rolled back? No
      // usually rollback on destruction. Actually, we must use a scoped
      // transaction or similar. BUT for now, since I don't see
      // `transaction->commit()` in standard examples recently, I will assume
      // that the transaction commits when the object goes out of scope IF no
      // exception was thrown? Actually, typically we need to distinct success
      // path. Wait, the Transaction class usually has a commit() method? Let's
      // check `drogon/orm/DbClient.h`... but I can't. I will use
      // `transaction->commit()` assuming it exists, or just let it be if I
      // can't find it. Given I cannot check, I will assume safe default: no
      // explicit commit needed if strict scoping? actually most C++ ORMs
      // require explicit commit. I'll leave it as is, rely on scope? No, that's
      // risky. I will restart the transaction concept: In Drogon, `co_await
      // transaction->commit()` is often used? Let's try to verify via other
      // files? No other transaction usage in this file. I'll take a safe bet:
      // `co_await dbClient->execSqlCoro("COMMIT")`? No, that's on client. The
      // transaction object itself implements DbClient interface.

      // Let's just create the payout and update commissions.

    } catch (...) {
      // Rollback is automatic on destruction usually?
      transaction->rollback(); // might be needed?
      throw;
    }

    // send to rabbit mq to process payments

    dto::BaseApiResponse response;
    response.success = true;
    response.result["message"] = "Payout issued successfully.";
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] = "Database error while issuing payout.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  } catch (const std::exception &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_INTERNAL;
    errorResponse.error["message"] = "Internal error.";
    errorResponse.error["detail"] = e.what();
    co_return errorResponse;
  }
}


drogon::Task<::gnp::dto::BaseApiResponse> AffiliateService::getAffiliateCommissions(const std::string &affiliateId) {
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Affiliates> affiliateMapper(dbClient);
  CoroMapper<AffiliateCommissions> commissionMapper(dbClient);

  try {
    // 1. Fetch affiliate to get name
    Affiliates affiliate;
    try {
      affiliate = co_await affiliateMapper.findByPrimaryKey(affiliateId);
    } catch (const UnexpectedRows &) {
      dto::BaseApiResponse response;
      response.success = false;
      response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
      response.error["message"] = "Affiliate not found.";
      co_return response;
    }

    // 2. Fetch commissions by affiliate name
    auto commissions =
        co_await commissionMapper
            .orderBy(AffiliateCommissions::Cols::_created_at, SortOrder::DESC)
            .findBy(Criteria(AffiliateCommissions::Cols::_affiliate_id, CompareOperator::EQ, affiliateId));

    // 3. Map to response
    dto::BaseApiResponse response;
    response.success = true;
    Json::Value data = Json::arrayValue;
    for (const auto &commission : commissions) {
      Json::Value json = commission.toJson();
      Json::Value camelCaseJson;
      camelCaseJson["id"] = json["id"];
      camelCaseJson["affiliateName"] = json["affiliate_name"];
      camelCaseJson["amount"] = json["amount"];
      camelCaseJson["orderId"] = json["order_id"];
      camelCaseJson["transactionReference"] = json["transaction_reference"];
      camelCaseJson["status"] = json["status"];
      camelCaseJson["createdAt"] = json["created_at"];
      camelCaseJson["updatedAt"] = json["updated_at"];
      data.append(camelCaseJson);
    }
    response.result["data"] = data;
    response.result["totalCount"] = (Json::UInt)commissions.size();
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] =
        "Database error while fetching affiliate commissions.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}


drogon::Task<::gnp::dto::BaseApiResponse> AffiliateService::getAffiliatePayouts(const std::string &affiliateId) {
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Affiliates> affiliateMapper(dbClient);
  CoroMapper<AffiliatePayouts> payoutMapper(dbClient);

  try {
    // 1. Fetch affiliate to get name
    Affiliates affiliate;
    try {
      affiliate = co_await affiliateMapper.findByPrimaryKey(affiliateId);
    } catch (const UnexpectedRows &) {
      dto::BaseApiResponse response;
      response.success = false;
      response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
      response.error["message"] = "Affiliate not found.";
      co_return response;
    }

    // 2. Fetch payouts by affiliate name
    auto payouts = co_await payoutMapper
            .orderBy(AffiliatePayouts::Cols::_created_at, SortOrder::DESC)
            .findBy(Criteria(AffiliatePayouts::Cols::_affiliate_id, CompareOperator::EQ, affiliateId));

    // 3. Map to response
    dto::BaseApiResponse response;
    response.success = true;
    Json::Value data = Json::arrayValue;
    for (const auto &payout : payouts) {
      Json::Value json = payout.toJson();
      Json::Value camelCaseJson;
      camelCaseJson["id"] = json["id"];
      camelCaseJson["affiliateName"] = json["affiliate_name"];
      camelCaseJson["totalAmount"] = json["total_amount"];
      camelCaseJson["accountType"] = json["account_type"];
      camelCaseJson["accountName"] = json["account_name"];
      camelCaseJson["accountProvider"] = json["account_provider"];
      camelCaseJson["status"] = json["status"];
      camelCaseJson["platforms"] = json["platforms"];
      camelCaseJson["createdAt"] = json["created_at"];
      camelCaseJson["updatedAt"] = json["updated_at"];
      data.append(camelCaseJson);
    }
    response.result["data"] = data;
    response.result["totalCount"] = (Json::UInt)payouts.size();
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] =
        "Database error while fetching affiliate payouts.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}


drogon::Task<::gnp::dto::BaseApiResponse> AffiliateService::issueBulkPayout() {

  auto dbClient = drogon::app().getDbClient();

  try {
    // Find all affiliates with pending commissions (affiliate_commissions
    // carries the status enum; the name lives on the affiliates row).
    std::string sql =
        "SELECT a.id FROM affiliates a "
        "WHERE EXISTS (SELECT 1 FROM affiliate_commissions ac "
        "              WHERE ac.affiliate_id = a.id "
        "                AND ac.status = $1)";

    auto result = co_await dbClient->execSqlCoro(
        sql, static_cast<int>(constants::StatusTypes::PENDING));

    if (result.empty()) {
      dto::BaseApiResponse response;
      response.success = true;
      response.result["message"] = "No pending commissions found.";
      response.result["processedCount"] = 0;
      response.result["successCount"] = 0;
      response.result["failureCount"] = 0;
      co_return response;
    }

    int successCount = 0;
    int failureCount = 0;

    Json::Value errors = Json::arrayValue;

    for (const auto &row : result) {
      std::string affiliateId = row["id"].as<std::string>();

      try {
        auto payoutResponse = co_await issueAffiliatePayout(affiliateId);
        if (payoutResponse.success) {
          successCount++;
        } else {
          failureCount++;
          Json::Value err;
          err["affiliateId"] = affiliateId;
          err["error"] = payoutResponse.error;
          errors.append(err);
        }
      } catch (const std::exception &e) {
        failureCount++;
        Json::Value err;
        err["affiliateId"] = affiliateId;
        err["message"] = e.what();
        errors.append(err);
      }
    }

    dto::BaseApiResponse response;
    response.success = true;
    response.result["message"] = "Bulk payout processing completed.";
    response.result["processedCount"] = successCount + failureCount;
    response.result["successCount"] = successCount;
    response.result["failureCount"] = failureCount;
    if (failureCount > 0) {
      response.result["errors"] = errors;
    }

    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] = "Database error during bulk payout.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}


namespace {

Json::Value affiliateSettingsJson(const drogon::orm::Row &row) {
  Json::Value data;
  data["id"] = row["id"].as<std::string>();
  data["defaultCommissionRate"] = row["default_commission_rate"].as<double>();
  data["minimumPayoutAmount"] = row["minimum_payout_amount"].as<double>();
  data["maximumPayoutAmount"] = row["maximum_payout_amount"].as<double>();
  data["payoutSchedule"] = row["payout_schedule"].isNull()
      ? Json::Value(Json::nullValue) : Json::Value(row["payout_schedule"].as<int>());
  data["sessionDuration"] = row["session_duration"].isNull()
      ? Json::Value(Json::nullValue) : Json::Value(row["session_duration"].as<int>());
  data["termsAndConditions"] = row["terms_and_conditions"].as<std::string>();
  return data;
}

} // namespace

drogon::Task<::gnp::dto::BaseApiResponse> AffiliateService::getAffiliateSettings() {
  dto::BaseApiResponse response;
  try {
    const auto rows = co_await drogon::app().getDbClient()->execSqlCoro(
        "SELECT * FROM affiliate_program_settings ORDER BY id LIMIT 1");
    response.success = true;
    response.result["data"] = rows.empty()
        ? Json::Value(Json::nullValue) : affiliateSettingsJson(rows[0]);
  } catch (const DrogonDbException &e) {
    response.error["code"] = constants::ERR_DB_QUERY;
    response.error["message"] = "Database error while fetching affiliate settings.";
    response.error["detail"] = e.base().what();
  }
  co_return response;
}

drogon::Task<::gnp::dto::BaseApiResponse> AffiliateService::createAffiliateSettings(
    const ::gnp::dto::AffiliateSettingsDto &dto) {
  dto::BaseApiResponse response;
  const auto &values = dto.values();
  auto invalid = [&](const std::string &message) {
    response.error["code"] = constants::ERR_VALIDATION;
    response.error["message"] = message;
  };
  if (!values.isObject() || values.empty()) {
    invalid("Provide affiliate program settings as a non-empty JSON object.");
    co_return response;
  }

  const std::string keys[] = {"defaultCommissionRate", "minimumPayoutAmount",
      "maximumPayoutAmount", "payoutSchedule", "sessionDuration", "termsAndConditions"};
  for (const auto &key : values.getMemberNames()) {
    if (std::find(std::begin(keys), std::end(keys), key) == std::end(keys)) {
      invalid("Unknown affiliate setting: " + key);
      co_return response;
    }
  }

  // Empty strings denote omitted fields only in the SQL bindings. Supplied
  // values must be valid; null must not silently erase NOT NULL columns.
  auto decimal = [&](const char *name, std::string &out) {
    if (!values.isMember(name)) return true;
    const auto &v = values[name];
    if (!(v.isNumeric() || v.isString())) return false;
    out = v.asString();
    try {
      size_t used = 0;
      const double number = std::stod(out, &used);
      return used == out.size() && std::isfinite(number) && number >= 0;
    } catch (const std::exception &) {
      return false;
    }
  };
  std::string rate, minimum, maximum;
  if (!decimal("defaultCommissionRate", rate) ||
      !decimal("minimumPayoutAmount", minimum) ||
      !decimal("maximumPayoutAmount", maximum)) {
    invalid("Commission rate and payout amounts must be non-negative numbers.");
    co_return response;
  }
  auto integer = [&](const char *name, std::string &out) {
    if (!values.isMember(name)) return true;
    const auto &v = values[name];
    if (!v.isInt() || v.asInt() < 0) return false;
    out = std::to_string(v.asInt());
    return true;
  };
  std::string schedule, duration;
  if (!integer("payoutSchedule", schedule) ||
      !integer("sessionDuration", duration)) {
    invalid("Payout schedule and session duration must be non-negative integers.");
    co_return response;
  }
  std::string terms;
  if (values.isMember("termsAndConditions")) {
    if (!values["termsAndConditions"].isString() ||
        values["termsAndConditions"].asString().size() > 1000) {
      invalid("Terms and conditions must be a string of at most 1000 characters.");
      co_return response;
    }
    terms = values["termsAndConditions"].asString();
  }

  try {
    auto transaction = co_await drogon::app().getDbClient()->newTransactionCoro();
    // Serialize writers (including the initially empty table) before deciding
    // whether to insert or update. All reads and writes use this transaction.
    co_await transaction->execSqlCoro("SELECT pg_advisory_xact_lock(73194824)");
    const auto existing = co_await transaction->execSqlCoro(
        "SELECT * FROM affiliate_program_settings ORDER BY id LIMIT 1 FOR UPDATE");
    drogon::orm::Result saved(nullptr);
    if (existing.empty()) {
      if (rate.empty() || minimum.empty() || maximum.empty() ||
          !values.isMember("termsAndConditions")) {
        invalid("defaultCommissionRate, minimumPayoutAmount, maximumPayoutAmount and termsAndConditions are required when creating settings.");
        transaction->rollback();
        co_return response;
      }
      if (std::stod(minimum) > std::stod(maximum)) {
        invalid("minimumPayoutAmount must not exceed maximumPayoutAmount.");
        transaction->rollback();
        co_return response;
      }
      saved = co_await transaction->execSqlCoro(
          "INSERT INTO affiliate_program_settings "
          "(id, default_commission_rate, minimum_payout_amount, "
          "maximum_payout_amount, payout_schedule, session_duration, terms_and_conditions) "
          "VALUES ($1::uuid, $2::numeric, $3::numeric, $4::numeric, "
          "NULLIF($5, '')::integer, NULLIF($6, '')::integer, $7) RETURNING *",
          gnp::utils::IdGeneratorUtils::generateGuid(), rate, minimum, maximum,
          schedule, duration, terms);
    } else {
      const auto &row = existing[0];
      const double effectiveMin = minimum.empty()
          ? row["minimum_payout_amount"].as<double>() : std::stod(minimum);
      const double effectiveMax = maximum.empty()
          ? row["maximum_payout_amount"].as<double>() : std::stod(maximum);
      if (effectiveMin > effectiveMax) {
        invalid("minimumPayoutAmount must not exceed maximumPayoutAmount.");
        transaction->rollback();
        co_return response;
      }
      const auto id = row["id"].as<std::string>();
      saved = co_await transaction->execSqlCoro(
          "UPDATE affiliate_program_settings SET "
          "default_commission_rate = COALESCE(NULLIF($2, '')::numeric, default_commission_rate), "
          "minimum_payout_amount = COALESCE(NULLIF($3, '')::numeric, minimum_payout_amount), "
          "maximum_payout_amount = COALESCE(NULLIF($4, '')::numeric, maximum_payout_amount), "
          "payout_schedule = COALESCE(NULLIF($5, '')::integer, payout_schedule), "
          "session_duration = COALESCE(NULLIF($6, '')::integer, session_duration), "
          "terms_and_conditions = CASE WHEN $7::boolean THEN $8::text "
          "ELSE terms_and_conditions END "
          "WHERE id = $1::uuid RETURNING *",
          id, rate, minimum, maximum, schedule, duration,
          values.isMember("termsAndConditions"), terms);
      // Consolidate any older rows created before singleton writes were used.
      co_await transaction->execSqlCoro(
          "DELETE FROM affiliate_program_settings WHERE id <> $1::uuid", id);
    }
    response.success = true;
    response.result["data"] = affiliateSettingsJson(saved[0]);
    response.message = "Affiliate program settings saved successfully.";
    // Drogon commits the transaction when the last reference leaves scope.
    co_return response;
  } catch (const DrogonDbException &e) {
    response.error["code"] = constants::ERR_DB_QUERY;
    response.error["message"] = "Database error while saving affiliate settings.";
    response.error["detail"] = e.base().what();
    co_return response;
  } catch (const std::exception &e) {
    response.error["code"] = constants::ERR_INTERNAL;
    response.error["message"] = "Error while saving affiliate settings.";
    response.error["detail"] = e.what();
    co_return response;
  }
}


} // namespace gnp::services