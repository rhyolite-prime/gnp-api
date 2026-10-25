//
// Created by Emmanuel Addo-Odame on 03/09/2025.
//

#include "SubscriptionService.h"
#include "services/reports/ReportInstrumentation.h"
#include <jwt-cpp/jwt.h>

#include "constants/ErrorCodes.h"
#include "dto/BaseApiResponse.h"
#include <drogon/orm/Criteria.h>
#include <drogon/orm/Mapper.h>

#include "Newspapers.h"
#include "PurchaseAttempts.h"
#include "SubscriptionRenewalHistory.h"
#include "UserNotificationSubscriptions.h"
#include "UserSubscriptions.h"
#include "Users.h"
#include "Payments.h"
#include "bcrypt.h"
#include "constants/NotificationTypes.h"
#include "dto/BuyNewspaperCopyDto.h" // Added missing include
#include "dto/SendEmailDto.h"
#include "plugins/GnpServicePlugin.h"
#include "services/email/EmailService.h"
#include "services/hubtel_sms/HubtelSmsApi.h"
#include "services/newspapers/NewspaperService.h"
#include "services/payments/PaymentService.h"
#include "services/paystack/PaystackApi.h"
#include "utils/IdGeneratorUtils.h"

using namespace drogon::orm;
using drogon_model::Gnp::PurchaseAttempts;
using drogon_model::Gnp::SubscriptionRenewalHistory;
using drogon_model::Gnp::UserNotificationSubscriptions;
using drogon_model::Gnp::Users;
using drogon_model::Gnp::UserSubscriptions;
using drogon_model::Gnp::Payments;

namespace gnp::services {

void SubscriptionService::manageGuestSubscription(
    const dto::GuestSubscriptionDto &guestSubscriptionDto,
    const std::function<void(const dto::BaseApiResponse &)> &callback) {

  auto dbClient = drogon::app().getDbClient();

  Mapper<drogon_model::Gnp::Users> mp(dbClient);

  Users newUser;

  newUser.setFirstName(guestSubscriptionDto.getFirstName());
  newUser.setLastName(guestSubscriptionDto.getLastName());
  newUser.setEmail(guestSubscriptionDto.getEmail());
  newUser.setPhoneNumber(guestSubscriptionDto.getPhoneNumber());
  newUser.setIsActive(true);
  newUser.setIsLockedOut(false);
  newUser.setCreatedAt(trantor::Date::now());

  mp.insert(
      newUser,
      [callback, dbClient,
       guestSubscriptionDto](const drogon_model::Gnp::Users &user) {
        // create an inactive user subscription.
        // but first check if user email/phone number exists

        Mapper<drogon_model::Gnp::UserSubscriptions> mp(dbClient);

        UserSubscriptions newUserSubscription;

        newUserSubscription.setSubscriptionIdentifier(
            gnp::utils::IdGeneratorUtils::generateRandomSixDigit());
        newUserSubscription.setUserId(user.getValueOfId());
        newUserSubscription.setEmail(user.getValueOfEmail());
        newUserSubscription.setNewspaperEntitlementsToNull();
        newUserSubscription.setIsActive(false);
        newUserSubscription.setCreatedAt(trantor::Date::now());

        mp.insert(
            newUserSubscription,
            [callback, user, guestSubscriptionDto](
                const drogon_model::Gnp::UserSubscriptions &userSubscription) {
              // create subscription_attempt

              // use initialize checkout url

              auto plugin =
                  drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
              auto &paystackApi = plugin->getPaystackApi();

              // 1. Build InitializePaymentRequest
              gnp::dto::InitializePaymentRequest initReq;
              initReq.setAmount("0.1");
              initReq.setPhone(guestSubscriptionDto.getPhoneNumber());
              std::string clientReference = gnp::utils::IdGeneratorUtils::generateGuid();

              initReq.setClientReference(clientReference);
              initReq.setCallBackUrl("https://gnp-api.com/paystack/callback");

              paystackApi.initialize(
                  initReq,
                  [callback](
                      const gnp::dto::InitializePaymentResponse &payResp) {
                    dto::BaseApiResponse response;

                    if (!payResp.getStatus()) {

                      response.success = false;
                      response.message = payResp.getMessage().empty()
                                             ? "Failed to initialize payment"
                                             : payResp.getMessage();
                      callback(response);
                    }

                    const auto &payData = payResp.getData();
                    response.success = true;
                    response.message =
                        "Subscription attempt successful. Payment initialized";
                    response.result["paymentUrl"] =
                        payData.getAuthorizationUrl();
                    response.result["reference"] = payData.getReference();
                    callback(response);
                  });
            },
            [callback](const drogon::orm::DrogonDbException &e) {
              dto::BaseApiResponse errorResponse;
              errorResponse.success = false;
              errorResponse.message =
                  "Database error while initializing user subscription";
              errorResponse.error["code"] = constants::ERR_DB_QUERY;
              callback(errorResponse);
            });
      },
      [callback](const drogon::orm::DrogonDbException &e) {
        dto::BaseApiResponse errorResponse;
        errorResponse.success = false;
        errorResponse.message = "Database error while creating User";
        errorResponse.error["code"] = constants::ERR_DB_QUERY;
        callback(errorResponse);
      });
}

drogon::Task<gnp::dto::BaseApiResponse>
SubscriptionService::manageGuestOneTimeBuyAsync(
    const dto::GuestOnetimeBuyDto &guestOnetimeBuyDto) {

  auto dbClient = drogon::app().getDbClient();
  dto::BaseApiResponse response;

  try {
    // 1. Find Newspaper
    CoroMapper<drogon_model::Gnp::Newspapers> newspaperMapper(dbClient);
    auto newspaper = co_await newspaperMapper.findByPrimaryKey(
        guestOnetimeBuyDto.getNewsPaperId());

    const auto &pricePtr = newspaper.getPrice();
    const std::string &paperCost = *pricePtr;
    std::string clientReference = gnp::utils::IdGeneratorUtils::generateGuid();

    // 2. Check for existing user
    CoroMapper<Users> userMapper(dbClient);
    Criteria checkCriteria =
        Criteria(Users::Cols::_email, CompareOperator::EQ,
                 guestOnetimeBuyDto.getEmail()) ||
        Criteria(Users::Cols::_phone_number, CompareOperator::EQ,
                 guestOnetimeBuyDto.getPhoneNumber());

    auto users = co_await userMapper.findBy(checkCriteria);
    if (!users.empty()) {
      response.success = false;
      response.message = "User with this email or phone number already exists.";
      co_return response;
    }

    // 3. Create New User
    Users newUser;
    newUser.setFirstName(guestOnetimeBuyDto.getFirstName());
    newUser.setLastName(guestOnetimeBuyDto.getLastName());
    newUser.setEmail(guestOnetimeBuyDto.getEmail());
    newUser.setPhoneNumber(guestOnetimeBuyDto.getPhoneNumber());
    newUser.setIsActive(true);
    newUser.setIsLockedOut(false);
    newUser.setCreatedAt(trantor::Date::now());

    auto user = co_await userMapper.insert(newUser);

    // 4. Create User Subscription (inactive)
    CoroMapper<drogon_model::Gnp::UserSubscriptions> subMapper(dbClient);
    UserSubscriptions newUserSubscription;
    newUserSubscription.setSubscriptionIdentifier(
        gnp::utils::IdGeneratorUtils::generateRandomSixDigit());
    newUserSubscription.setUserId(user.getValueOfId());
    newUserSubscription.setEmail(user.getValueOfEmail());
    newUserSubscription.setIsActive(false);
    newUserSubscription.setCreatedAt(trantor::Date::now());

    co_await subMapper.insert(newUserSubscription);

    // 5. Create Purchase Attempt
    CoroMapper<PurchaseAttempts> paMapper(dbClient);
    PurchaseAttempts newPurchaseAttempt;
    newPurchaseAttempt.setUserId(user.getValueOfId());
    newPurchaseAttempt.setNewspaperId(guestOnetimeBuyDto.getNewsPaperId());
    newPurchaseAttempt.setAttemptReference(clientReference);
    newPurchaseAttempt.setAmount(paperCost);
    newPurchaseAttempt.setStatus("Initiated");
    newPurchaseAttempt.setFailureReasonToNull();
    newPurchaseAttempt.setCreatedAt(trantor::Date::now());

    co_await paMapper.insert(newPurchaseAttempt);

    // 6. Create Payment Record (Async)
    auto paymentService = std::make_shared<PaymentService>();
    gnp::dto::CreatePaymentDto paymentDto;
    paymentDto.setUserId(user.getValueOfId());
    paymentDto.setUserName(user.getValueOfUsername());
    paymentDto.setUserEmail(user.getValueOfEmail());
    paymentDto.setPackageName(*newspaper.getTitle());
    paymentDto.setAmountPaid(paperCost);
    paymentDto.setReceiptNo(clientReference);
    paymentDto.setTransactionReference(clientReference);
    paymentDto.setStatus("Initiated");

    co_await paymentService->createPaymentAsync(paymentDto);

    // 7. Initialize Paystack Payment (Async)
    auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
    auto &paystackApi = plugin->getPaystackApi();

    gnp::dto::InitializePaymentRequest initReq;
    initReq.setAmount(paperCost);
    initReq.setPhone(guestOnetimeBuyDto.getPhoneNumber());
    initReq.setEmail(guestOnetimeBuyDto.getEmail());
    initReq.setClientReference(clientReference);
    initReq.setCallBackUrl("https://gnp-api.com/paystack/callback");

    auto payResp = co_await paystackApi.initializeAsync(initReq);
    if (!payResp.getStatus()) {
      response.success = false;
      response.message = payResp.getMessage().empty()
                             ? "Failed to initialize payment"
                             : payResp.getMessage();
      co_return response;
    }

    const auto &payData = payResp.getData();
    response.success = true;
    response.message = "Subscription created successfully. Payment initialized";
    response.result["paymentUrl"] = payData.getAuthorizationUrl();
    response.result["reference"] = payData.getReference();
    response.result["userId"] = user.getValueOfId();

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.message = "Database error: " + std::string(e.base().what());
    response.error["code"] = constants::ERR_DB_QUERY;
  } catch (const std::exception &e) {
    response.success = false;
    response.message = "An error occurred: " + std::string(e.what());
  }

  co_return response;
}

drogon::Task<gnp::dto::BaseApiResponse>
SubscriptionService::completeGuestOneTimeBuyAsync(
    const std::string &reference) {

  auto dbClient = drogon::app().getDbClient();
  dto::BaseApiResponse response;

  try {
    // 1. Find Purchase Attempt
    CoroMapper<PurchaseAttempts> purchaseAttemptMapper(dbClient);
    Criteria criteria = Criteria(PurchaseAttempts::Cols::_attempt_reference,
                                 CompareOperator::EQ, reference);
    auto purchaseAttempt = co_await purchaseAttemptMapper.findOne(criteria);

    // 2. Find User
    CoroMapper<Users> userMapper(dbClient);
    auto user = co_await userMapper.findByPrimaryKey(
        purchaseAttempt.getValueOfUserId());

    // 3. Verify Payment with Paystack
    auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
    auto &paystackApi = plugin->getPaystackApi();
    auto verifyPayResponse = co_await paystackApi.verifyAsync(reference);

    if (!verifyPayResponse.getStatus()) {
      response.success = false;
      response.message = !verifyPayResponse.getMessage().empty()
                             ? verifyPayResponse.getMessage()
                             : "Failed to verify payment";
      co_return response;
    }

    const auto &verifyData = verifyPayResponse.getData();
    auto paymentService = std::make_shared<gnp::services::PaymentService>();

    if (verifyData.status_ == "success") {
      // 4. Update status to Success
      purchaseAttempt.setStatus("Success");
      purchaseAttempt.setFailureReasonToNull();
      co_await purchaseAttemptMapper.update(purchaseAttempt);

      co_await paymentService->updateStatusAsync("Success", reference);

      // Increment Newspaper Sales
      try {
        CoroMapper<drogon_model::Gnp::Newspapers> newspaperMapper(dbClient);
        auto newspaper = co_await newspaperMapper.findByPrimaryKey(
            purchaseAttempt.getValueOfNewspaperId());

        std::string salesStr = newspaper.getValueOfSales();
        long long sales = 0;
        if (!salesStr.empty()) {
          try {
            sales = std::stoll(salesStr);
          } catch (...) {
            sales = 0;
          }
        }
        sales++;
        newspaper.setSales(std::to_string(sales));
        co_await newspaperMapper.update(newspaper);
        // Report telemetry: single-copy sale (edition_sales_ledger)
        {
          double saleAmount = 0.0;
          double salePrice = 0.0;
          try { saleAmount = std::stod(purchaseAttempt.getValueOfAmount()); } catch (...) {}
          try { salePrice = std::stod(newspaper.getValueOfPrice()); } catch (...) {}
          co_await reporting::recordEditionSale(
              newspaper.getValueOfId(), newspaper.getValueOfPublicationId(),
              purchaseAttempt.getValueOfUserId(), "", true, "", "",
              salePrice, 0.0, saleAmount);
        }
      } catch (...) {
        // Log error or handle failure to update sales (not critical to the
        // purchase flow)
      }

      response.success = true;
      response.message =
          "Payment verified successfully. Access to Newspaper granted.";

      // 5. Generate and hash password
      std::string firstName = user.getValueOfFirstName();
      std::string phoneNumber = user.getValueOfPhoneNumber();
      std::string password;
      if (phoneNumber.length() >= 4) {
        password =
            firstName + "@" + phoneNumber.substr(phoneNumber.length() - 4);
      } else {
        password = firstName + "@" + phoneNumber;
      }
      std::string passwordHash = bcrypt::generateHash(password);

      // 6. Update User with Password
      user.setPasswordHash(passwordHash);
      co_await userMapper.update(user);

      // 7. Send Email (Fire and forget, same as before)
      auto emailService = std::make_shared<gnp::services::EmailService>();
      gnp::dto::SendEmailDto emailDto;
      emailDto.setTo(user.getValueOfEmail());
      emailDto.setSubject("Your Graphic News Plus Account Details");

      std::string emailBody =
          R"(
              <!DOCTYPE html>
              <html>
              <head>
              <style>
                body { font-family: Arial, sans-serif; background-color: #f4f4f4; margin: 0; padding: 0; }
                .container { max-width: 600px; margin: 20px auto; background-color: #ffffff; border-radius: 8px; overflow: hidden; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
                .header { background-color: #D32F2F; color: #ffffff; padding: 20px; text-align: center; }
                .content { padding: 30px; text-align: center; color: #333333; }
                .password { font-size: 24px; font-weight: bold; color: #D32F2F; margin: 20px 0; }
                .footer { background-color: #f4f4f4; color: #666666; padding: 10px; text-align: center; font-size: 12px; }
              </style>
              </head>
              <body>
              <div class="container">
                <div class="header">
                  <h1>Graphic News Plus</h1>
                </div>
                <div class="content">
                  <p>Hello )" +
          firstName + R"(,</p>
                  <p>Thank you for your purchase. An account has been created for you.</p>
                  <p>Your password is:</p>
                  <div class="password">)" +
          password + R"(</div>
                  <p>You can use this password to log in to your account.</p>
                  <p>You're all set! Log in now to explore more engaging content made just for you.</p>
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

      co_await emailService->sendEmailAsync(emailDto);

      // 8. Generate JWT
      auto &app = drogon::app();
      auto customConfig = app.getCustomConfig();
      std::string jwtSecurityKey = customConfig["JwtBearer"]["JwtSecurityKey"].asString();
      std::string jwtIssuer = customConfig["JwtBearer"]["JwtIssuer"].asString();

      auto token =
          jwt::create()
              .set_issuer(jwtIssuer)
              .set_type("JWT")
              .set_issued_at(std::chrono::system_clock::now())
              .set_expires_at(std::chrono::system_clock::now() +
                              std::chrono::hours(24 * 30))
              .set_payload_claim("userId", jwt::claim(user.getValueOfId()))
              .set_payload_claim("firstName",
                                 jwt::claim(user.getValueOfFirstName()))
              .set_payload_claim("surName",
                                 jwt::claim(user.getValueOfLastName()))
              .set_payload_claim("username",
                                 jwt::claim(user.getValueOfUsername()))
              .set_payload_claim("email", jwt::claim(user.getValueOfEmail()))
              .sign(jwt::algorithm::hs256{jwtSecurityKey});

      response.result = token;

      // 9. Update User Subscription Entitlements
      CoroMapper<UserSubscriptions> subMapper(dbClient);
      Criteria subCriteria(UserSubscriptions::Cols::_user_id,
                           CompareOperator::EQ, user.getValueOfId());
      auto userSub = co_await subMapper.findOne(subCriteria);

      Json::Value entitlements;
      std::string currentEntitlementsStr =
          userSub.getValueOfNewspaperEntitlements();

      if (!currentEntitlementsStr.empty()) {
        Json::CharReaderBuilder readerBuilder;
        std::string errs;
        std::istringstream s(currentEntitlementsStr);
        if (!Json::parseFromStream(readerBuilder, s, &entitlements, &errs)) {
          entitlements = Json::arrayValue;
        }
      } else {
        entitlements = Json::arrayValue;
      }

      std::string newPaperId = purchaseAttempt.getValueOfNewspaperId();

      bool alreadyExists = false;

      for (const auto &ent : entitlements) {
        if (ent.isObject() && ent.isMember("id") && ent["id"].isString() &&
            ent["id"].asString() == newPaperId) {
          alreadyExists = true;
          break;
        } else if (ent.isString() && ent.asString() == newPaperId) {
          alreadyExists = true;
          break;
        }
      }

      if (!alreadyExists) {
        Json::Value newEnt;
        newEnt["id"] = newPaperId;
        newEnt["uniqueId"] =  gnp::utils::IdGeneratorUtils::generateAlphanumericId();
        entitlements.append(newEnt);
      }

      Json::StreamWriterBuilder writerBuilder;
      writerBuilder["indentation"] = "";
      userSub.setNewspaperEntitlements(
          Json::writeString(writerBuilder, entitlements));
      userSub.setIsActive(true);
      co_await subMapper.update(userSub);

    } else {
      // Payment Failed
      purchaseAttempt.setStatus("Failed");
      purchaseAttempt.setFailureReason(verifyData.message_);
      co_await purchaseAttemptMapper.update(purchaseAttempt);

      co_await paymentService->updateStatusAsync("Failed", reference);

      response.success = false;
      response.message =
          "Payment verification failed. Status: " + verifyData.status_;
    }

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.message = "Database error: " + std::string(e.base().what());
    response.error["code"] = constants::ERR_DB_QUERY;
  } catch (const std::exception &e) {
    response.success = false;
    response.message = "An error occurred: " + std::string(e.what());
  }

  co_return response;
}

drogon::Task<gnp::dto::BaseApiResponse>
SubscriptionService::completeUserOneTimeBuyAsync(const std::string &userId,
                                                 const std::string &reference) {
  auto dbClient = drogon::app().getDbClient();
  dto::BaseApiResponse response;

  try {
    // 1. Find Purchase Attempt
    CoroMapper<PurchaseAttempts> purchaseAttemptMapper(dbClient);
    Criteria criteria = Criteria(PurchaseAttempts::Cols::_attempt_reference,
                                 CompareOperator::EQ, reference);
    auto purchaseAttempt = co_await purchaseAttemptMapper.findOne(criteria);

    // 2. Verify Payment with Paystack
    auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
    auto &paystackApi = plugin->getPaystackApi();
    auto verifyPayResponse = co_await paystackApi.verifyAsync(reference);

    if (!verifyPayResponse.getStatus()) {
      response.success = false;
      response.message = !verifyPayResponse.getMessage().empty()
                             ? verifyPayResponse.getMessage()
                             : "Failed to verify payment";
      co_return response;
    }

    const auto &verifyData = verifyPayResponse.getData();
    auto paymentService = std::make_shared<gnp::services::PaymentService>();

    if (verifyData.status_ == "success") {
      // 3. Update status to Success
      purchaseAttempt.setStatus("Success");
      purchaseAttempt.setFailureReasonToNull();
      co_await purchaseAttemptMapper.update(purchaseAttempt);

      co_await paymentService->updateStatusAsync("Success", reference);

      // Increment Newspaper Sales
      try {
        CoroMapper<drogon_model::Gnp::Newspapers> newspaperMapper(dbClient);
        auto newspaper = co_await newspaperMapper.findByPrimaryKey(
            purchaseAttempt.getValueOfNewspaperId());

        std::string salesStr = newspaper.getValueOfSales();
        long long sales = 0;
        if (!salesStr.empty()) {
          try {
            sales = std::stoll(salesStr);
          } catch (...) {
            sales = 0;
          }
        }
        sales++;
        newspaper.setSales(std::to_string(sales));
        co_await newspaperMapper.update(newspaper);
        // Report telemetry: single-copy sale (edition_sales_ledger)
        {
          double saleAmount = 0.0;
          double salePrice = 0.0;
          try { saleAmount = std::stod(purchaseAttempt.getValueOfAmount()); } catch (...) {}
          try { salePrice = std::stod(newspaper.getValueOfPrice()); } catch (...) {}
          co_await reporting::recordEditionSale(
              newspaper.getValueOfId(), newspaper.getValueOfPublicationId(),
              purchaseAttempt.getValueOfUserId(), "", false, "", "",
              salePrice, 0.0, saleAmount);
        }
      } catch (...) {
        // Log error or handle failure to update sales
      }

      // 4. Update User Subscription Entitlements
      CoroMapper<UserSubscriptions> subMapper(dbClient);
      Criteria subCriteria(UserSubscriptions::Cols::_user_id,
                           CompareOperator::EQ, userId);
      auto userSub = co_await subMapper.findOne(subCriteria);

      Json::Value entitlements;
      std::string currentEntitlementsStr =
          userSub.getValueOfNewspaperEntitlements();

      if (!currentEntitlementsStr.empty()) {
        Json::CharReaderBuilder readerBuilder;
        std::string errs;
        std::istringstream s(currentEntitlementsStr);
        if (!Json::parseFromStream(readerBuilder, s, &entitlements, &errs)) {
          entitlements = Json::arrayValue;
        }
      } else {
        entitlements = Json::arrayValue;
      }

      std::string newPaperId = purchaseAttempt.getValueOfNewspaperId();
      bool alreadyExists = false;

      for (const auto &ent : entitlements) {
        if (ent.isObject() && ent.isMember("id") && ent["id"].isString() &&
            ent["id"].asString() == newPaperId) {
          alreadyExists = true;
          break;
        } else if (ent.isString() && ent.asString() == newPaperId) {
          alreadyExists = true;
          break;
        }
      }

      if (!alreadyExists) {
        Json::Value newEnt;
        newEnt["id"] = newPaperId;
        newEnt["uniqueId"] =
            gnp::utils::IdGeneratorUtils::generateAlphanumericId();
        entitlements.append(newEnt);
      }

      Json::StreamWriterBuilder writerBuilder;
      writerBuilder["indentation"] = "";
      userSub.setNewspaperEntitlements(
          Json::writeString(writerBuilder, entitlements));
      userSub.setIsActive(true);
      co_await subMapper.update(userSub);

      response.success = true;
      response.message =
          "Payment verified successfully. Access to Newspaper granted.";

    } else {
      // Payment Failed
      purchaseAttempt.setStatus("Failed");
      purchaseAttempt.setFailureReason(verifyData.message_);
      co_await purchaseAttemptMapper.update(purchaseAttempt);

      co_await paymentService->updateStatusAsync("Failed", reference);

      response.success = false;
      response.message =
          "Payment verification failed. Status: " + verifyData.status_;
    }

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.message = "Database error: " + std::string(e.base().what());
    response.error["code"] = constants::ERR_DB_QUERY;
  } catch (const std::exception &e) {
    response.success = false;
    response.message = "An error occurred: " + std::string(e.what());
  }

  co_return response;
}

drogon::Task<gnp::dto::BaseApiResponse>
SubscriptionService::fulfillSubscriptionPaymentAsync(const std::string &userId, const std::string &reference) {
  auto dbClient = drogon::app().getDbClient();
  dto::BaseApiResponse response;

  try {
    // 1. Verify Payment with Paystack
    auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
    auto &paystackApi = plugin->getPaystackApi();
    auto verifyPayResponse = co_await paystackApi.verifyAsync(reference);

    if (!verifyPayResponse.getStatus()) {
      response.success = false;
      response.message = !verifyPayResponse.getMessage().empty()
                             ? verifyPayResponse.getMessage()
                             : "Failed to verify payment";
      co_return response;
    }

    const auto &verifyData = verifyPayResponse.getData();
    auto paymentService = std::make_shared<gnp::services::PaymentService>();

    if (verifyData.status_ == "success") {
      // 2. Fetch UserSubscription
      CoroMapper<UserSubscriptions> subMapper(dbClient);
      Criteria subCriteria = Criteria(UserSubscriptions::Cols::_subscription_identifier, CompareOperator::EQ, reference) &&
                             Criteria(UserSubscriptions::Cols::_user_id, CompareOperator::EQ, userId);
      auto userSub = co_await subMapper.findOne(subCriteria);

      if (userSub.getValueOfIsActive()) {
         response.success = true;
         response.message = "Subscription is already active.";
         co_return response;
      }

      // 3. Update status to Success in Payments table
      co_await paymentService->updateStatusAsync("Success", reference);

      // 4. Activate Subscription
      userSub.setIsActive(true);
      co_await subMapper.update(userSub);

      // 5. Fetch payment details for history
      CoroMapper<Payments> paymentMapper(dbClient);
      Criteria paymentCriteria(Payments::Cols::_transaction_reference, CompareOperator::EQ, reference);
      auto paymentRecords = co_await paymentMapper.findBy(paymentCriteria);
      std::string amountPaid = "0.0";
      if (!paymentRecords.empty()) {
          amountPaid = paymentRecords[0].getValueOfAmountPaid();
      }

      // 6. Fetch user to get names
      CoroMapper<Users> userMapper(dbClient);
      auto user = co_await userMapper.findByPrimaryKey(userId);
      std::string fullName = user.getValueOfFirstName() + " " + user.getValueOfLastName();

      // 7. Insert Subscription Renewal History
      CoroMapper<drogon_model::Gnp::SubscriptionRenewalHistory> renewalMapper(dbClient);
      drogon_model::Gnp::SubscriptionRenewalHistory renewal;
      renewal.setSubscriptionIdentifier(reference);
      renewal.setUserId(userId);
      renewal.setUserName(fullName);
      renewal.setEmail(user.getValueOfEmail());
      renewal.setSubscriptionPlanId(userSub.getValueOfSubscriptionPlanId());
      renewal.setSubscriptionPlanName(userSub.getValueOfSubscriptionPlanDescription());
      renewal.setPastBillingCycle("N/A");
      renewal.setCurrentBillingCycle(userSub.getValueOfBillingCycle());
      renewal.setTransactionStatus("Successful");
      renewal.setPaidBy(fullName);
      renewal.setAmountPaid(amountPaid);
      renewal.setCreatedAt(trantor::Date::now());
      co_await renewalMapper.insert(renewal);

      // Report telemetry: subscription lifecycle event (subscription_lifecycle_events)
      {
        double mrrDelta = 0.0;
        try { mrrDelta = std::stod(amountPaid); } catch (...) {}
        const ::trantor::Date &startedAt = userSub.getValueOfStartDate();
        const bool isNewSubscription =
            startedAt.microSecondsSinceEpoch() >
            (::trantor::Date::now().microSecondsSinceEpoch() -
             2LL * 24 * 3600 * 1000000LL);
        co_await reporting::recordSubscriptionLifecycleEvent(
            userSub.getValueOfId(), userId, userSub.getValueOfPartnerId(),
            "", userSub.getValueOfSubscriptionPlanId(),
            isNewSubscription ? "new" : "renewal", mrrDelta,
            paymentRecords.empty() ? std::string("")
                                   : paymentRecords[0].getValueOfId(),
            "");
      }

      response.success = true;
      response.message = "Subscription activated successfully.";

    } else {
      // Payment Failed
      co_await paymentService->updateStatusAsync("Failed", reference);

      response.success = false;
      response.message = "Payment verification failed. Status: " + verifyData.status_;
    }

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.message = "Database error: " + std::string(e.base().what());
    response.error["code"] = constants::ERR_DB_QUERY;
  } catch (const std::exception &e) {
    response.success = false;
    response.message = "An error occurred: " + std::string(e.what());
  }

  co_return response;
}


drogon::Task<gnp::dto::BaseApiResponse> SubscriptionService::validateNewsPaperEntitlementAsync(
    const std::string &newsPaperId, const std::string &userId) {

  dto::BaseApiResponse response;
  auto dbClient = drogon::app().getDbClient();

  try {
    // Check UserSubscription Table using CoroMapper
    CoroMapper<UserSubscriptions> subMapper(dbClient);
    Criteria subCriteria(UserSubscriptions::Cols::_user_id, CompareOperator::EQ,
                         userId);

    auto userSub = co_await subMapper.findOne(subCriteria);

    // Check newspaper_entitlements
    std::string entitlementsStr = userSub.getValueOfNewspaperEntitlements();
    bool hasAccess = false;
    std::string uniqueId = "";

    if (!entitlementsStr.empty()) {
      Json::Value entitlements;
      Json::CharReaderBuilder readerBuilder;
      std::string errs;
      std::istringstream s(entitlementsStr);
      if (Json::parseFromStream(readerBuilder, s, &entitlements, &errs)) {
        for (const auto &ent : entitlements) {
          if (ent.isObject() && ent.isMember("id") && ent["id"].isString()) {
            if (ent["id"].asString() == newsPaperId) {
              hasAccess = true;
              if (ent.isMember("uniqueId") && ent["uniqueId"].isString()) {
                uniqueId = ent["uniqueId"].asString();
              }
              break;
            }
          } else if (ent.isString() && ent.asString() == newsPaperId) {
            hasAccess = true;
            break;
          }
        }
      }
    }

    response.success = true; // The check itself was successful
    response.message = hasAccess ? "Access granted" : "Access denied";
    response.result["hasAccess"] = hasAccess;
    response.result["newsPaperId"] = newsPaperId;
    response.result["price"] = 1.5;
    response.result["uniqueId"] = uniqueId;

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.message = "User subscription not found or database error";
    response.error["code"] = constants::ERR_DB_QUERY;
  } catch (const std::exception &e) {
    response.success = false;
    response.message = "An error occurred: " + std::string(e.what());
  }

  co_return response;
}


void SubscriptionService::grantNewsPaperAccessToRequester(
    const dto::GrantNewsPaperAccessDto &dto,
    const std::function<void(const gnp::dto::BaseApiResponse &)> &callback) {

  auto dbClient = drogon::app().getDbClient();
  Mapper<UserSubscriptions> subMapper(dbClient);

  Criteria criteria = Criteria(UserSubscriptions::Cols::_user_id,
                               CompareOperator::EQ, dto.getUserId()) &&
                      Criteria(UserSubscriptions::Cols::_email,
                               CompareOperator::EQ, dto.getEmail());

  subMapper.findOne(
      criteria,
      [callback, dto, dbClient](const UserSubscriptions &userSub) {
        std::string entitlementsStr = userSub.getValueOfNewspaperEntitlements();
        if (entitlementsStr.empty()) {
          dto::BaseApiResponse response;
          response.success = false;
          response.message = "No newspaper entitlements found for this user";
          callback(response);
          return;
        }

        Json::Value entitlements;
        Json::CharReaderBuilder readerBuilder;
        std::string errs;
        std::istringstream s(entitlementsStr);

        if (!Json::parseFromStream(readerBuilder, s, &entitlements, &errs)) {
          dto::BaseApiResponse response;
          response.success = false;
          response.message = "Failed to parse newspaper entitlements";
          callback(response);
          return;
        }

        std::string newspaperId = "";
        for (const auto &ent : entitlements) {
          if (ent.isObject() && ent.isMember("uniqueId") &&
              ent["uniqueId"].asString() == dto.getUniqueId()) {
            newspaperId = ent["id"].asString();
            break;
          }
        }

        if (newspaperId.empty()) {
          dto::BaseApiResponse response;
          response.success = false;
          response.message =
              "Newspaper with provided uniqueId not found in entitlements";
          callback(response);
          return;
        }

        // Fetch newspaper details
        Mapper<drogon_model::Gnp::Newspapers> newspaperMapper(dbClient);
        newspaperMapper.findByPrimaryKey(
            newspaperId,
            [callback](const drogon_model::Gnp::Newspapers &newspaper) {
              dto::BaseApiResponse response;
              response.success = true;
              response.message = "Newspaper access granted";

              Json::Value src = newspaper.toJson();
              Json::Value data;

              // Basic fields
              data["id"] = src["id"];
              data["title"] = src["title"];
              data["slug"] = src["slug"];
              data["price"] = src["price"];
              data["editionNumber"] = src["edition_number"];
              data["shortDescription"] = src["short_description"];
              data["fullDescription"] = src["full_description"];
              data["thumbnailId"] = src["thumbnail_id"];
              data["fileType"] = src["file_type"];
              data["storageType"] = src["storage_type"];
              data["documentId"] = src["document_id"];
              data["isFree"] = src["is_free"];
              data["isPopular"] = src["is_popular"];
              data["publicationDate"] = src["publication_date"];
              data["isPublished"] = src["is_published"];

              // Category / publication info
              data["categoryId"] = src["category_id"];
              data["categoryName"] = src["category_name"];
              data["publicationId"] = src["publication_id"];
              data["publicationName"] = src["publication_name"];

              // Copyright
              data["copyrightOwner"] = src["copyright_owner"];

              // Featured stories (stored as JSON string)
              std::string featuredStoriesStr =
                  newspaper.getValueOfFeaturedStories();
              Json::Value featuredStoriesJson;
              Json::Reader reader;
              if (!featuredStoriesStr.empty() &&
                  reader.parse(featuredStoriesStr, featuredStoriesJson)) {
                data["featuredStories"] = featuredStoriesJson;
              } else {
                data["featuredStories"] = Json::arrayValue;
              }

              response.result = data;

              callback(response);
            },
            [callback](const DrogonDbException &e) {
              dto::BaseApiResponse response;
              response.success = false;
              response.message =
                  "Database error while fetching newspaper details";
              response.error["code"] = constants::ERR_DB_QUERY;
              callback(response);
            });
      },
      [callback](const DrogonDbException &e) {
        dto::BaseApiResponse response;
        response.success = false;
        response.message = "User subscription not found";
        response.error["code"] = constants::ERR_DB_QUERY;
        callback(response);
      });
}

drogon::Task<gnp::dto::BaseApiResponse>
SubscriptionService::getNewsPaperRedactedDetailsWithUniqueIdAsync(
    const std::string &uniqueId, const std::string &userId,
    const std::string &email) {

  gnp::dto::BaseApiResponse response;
  auto dbClient = drogon::app().getDbClient();

  try {
    CoroMapper<UserSubscriptions> subMapper(dbClient);

    Criteria criteria =
        Criteria(UserSubscriptions::Cols::_user_id, CompareOperator::EQ,
                 userId) &&
        Criteria(UserSubscriptions::Cols::_email, CompareOperator::EQ, email);

    auto userSub = co_await subMapper.findOne(criteria);

    std::string entitlementsStr = userSub.getValueOfNewspaperEntitlements();
    if (entitlementsStr.empty()) {
      response.success = false;
      response.message = "No newspaper entitlements found for this user";
      co_return response;
    }

    Json::Value entitlements;
    Json::CharReaderBuilder readerBuilder;
    std::string errs;
    std::istringstream s(entitlementsStr);

    if (!Json::parseFromStream(readerBuilder, s, &entitlements, &errs)) {
      response.success = false;
      response.message = "Failed to parse newspaper entitlements";
      co_return response;
    }

    std::string newspaperId = "";
    for (const auto &ent : entitlements) {
      if (ent.isObject() && ent.isMember("uniqueId") &&
          ent["uniqueId"].asString() == uniqueId) {
        newspaperId = ent["id"].asString();
        break;
      }
    }

    if (newspaperId.empty()) {
      response.success = false;
      response.message =
          "Newspaper with provided uniqueId not found in entitlements";
      co_return response;
    }

    // Fetch newspaper details
    CoroMapper<drogon_model::Gnp::Newspapers> newspaperMapper(dbClient);
    auto newspaper = co_await newspaperMapper.findByPrimaryKey(newspaperId);

    response.success = true;
    response.message = "Newspaper access granted";

    Json::Value src = newspaper.toJson();
    Json::Value data;

    // Basic fields
    data["id"] = src["id"];
    data["title"] = src["title"];
    data["slug"] = src["slug"];
    data["publicationDate"] = src["publication_date"];
    data["publicationId"] = src["publication_id"];

    response.result = data;

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.message = "User subscription not found or database error: " +
                       std::string(e.base().what());
    response.error["code"] = constants::ERR_DB_QUERY;
  } catch (const std::exception &e) {
    response.success = false;
    response.message = "An error occurred: " + std::string(e.what());
  }

  co_return response;
}

drogon::Task<gnp::dto::BaseApiResponse>
SubscriptionService::readNewsPaperByDateAndPublicationAsync(
    const std::string &publicationId, const std::string &publicationDate,
    const std::string &userId, const std::string &email) {

  try {
    auto dbClient = drogon::app().getDbClient();

    // Find Newspaper ID by publicationId and date
    CoroMapper<drogon_model::Gnp::Newspapers> newspaperMapper(dbClient);
    Criteria newsCriteria =
        Criteria(drogon_model::Gnp::Newspapers::Cols::_publication_id,
                 CompareOperator::EQ, publicationId) &&
        Criteria(drogon_model::Gnp::Newspapers::Cols::_publication_date,
                 CompareOperator::EQ, publicationDate);

    auto newspaper = co_await newspaperMapper.findOne(newsCriteria);
    std::string newspaperId = newspaper.getValueOfId();
    std::string slug = newspaper.getValueOfSlug();
    std::string title = *newspaper.getTitle();
    std::string pubDate =
        newspaper.getValueOfPublicationDate().toDbStringLocal();

    // Check User Entitlements
    CoroMapper<UserSubscriptions> subMapper(dbClient);
    Criteria subCriteria =
        Criteria(UserSubscriptions::Cols::_user_id, CompareOperator::EQ,
                 userId) &&
        Criteria(UserSubscriptions::Cols::_email, CompareOperator::EQ, email);

    auto userSub = co_await subMapper.findOne(subCriteria);
    std::string entitlementsStr = userSub.getValueOfNewspaperEntitlements();

    if (entitlementsStr.empty()) {
      gnp::dto::BaseApiResponse response;
      response.success = false;
      response.message = "No newspaper entitlements found for this user";
      co_return response;
    }

    Json::Value entitlements;
    Json::CharReaderBuilder readerBuilder;
    std::string errs;
    std::istringstream s(entitlementsStr);

    if (!Json::parseFromStream(readerBuilder, s, &entitlements, &errs)) {
      gnp::dto::BaseApiResponse response;
      response.success = false;
      response.message = "Failed to parse newspaper entitlements";
      co_return response;
    }

    std::string uniqueId = "";
    bool hasAccess = false;
    for (const auto &ent : entitlements) {
      if (ent.isObject() && ent.isMember("id") &&
          ent["id"].asString() == newspaperId) {
        hasAccess = true;
        if (ent.isMember("uniqueId")) {
          uniqueId = ent["uniqueId"].asString();
        }
        break;
      }
    }

    if (!hasAccess) {
      gnp::dto::BaseApiResponse response;
      response.success = false;
      response.message = "Access denied for this newspaper";
      co_return response;
    }

    gnp::dto::BaseApiResponse response;
    response.success = true;
    response.message = "Access verified";
    response.result["uniqueId"] = uniqueId;
    response.result["slug"] = slug;
    response.result["title"] = title;
    response.result["publicationDate"] = pubDate;

    co_return response;

  } catch (const DrogonDbException &e) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.message = "Database error or resource not found";
    response.error["code"] = constants::ERR_DB_QUERY;
    response.error["detail"] = e.base().what();
    co_return response;
  } catch (const std::exception &e) {
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.message = std::string("Operation failed: ") + e.what();
    co_return response;
  }
}

drogon::Task<gnp::dto::BaseApiResponse>
SubscriptionService::initializeUserOneTimeBuyAsync(
    const std::string &newsPaperId, const std::string &userId) {
  auto dbClient = drogon::app().getDbClient();
  dto::BaseApiResponse response;

  try {
    // 1. Find Newspaper
    CoroMapper<drogon_model::Gnp::Newspapers> newspaperMapper(dbClient);
    auto newspaper = co_await newspaperMapper.findByPrimaryKey(newsPaperId);

    const auto &pricePtr = newspaper.getPrice();
    const std::string &paperCost = *pricePtr;
    std::string clientReference = gnp::utils::IdGeneratorUtils::generateGuid();

    // 2. Find User
    CoroMapper<Users> userMapper(dbClient);
    auto user = co_await userMapper.findByPrimaryKey(userId);

    // 3. Check for existing entitlement (UserSubscription)
    CoroMapper<UserSubscriptions> subMapper(dbClient);
    Criteria subCriteria(UserSubscriptions::Cols::_user_id, CompareOperator::EQ,
                         userId);

    auto userSubscriptions = co_await subMapper.findBy(subCriteria);
    if (!userSubscriptions.empty()) {
      const auto &userSub = userSubscriptions[0];
      std::string entitlementsStr = userSub.getValueOfNewspaperEntitlements();
      if (!entitlementsStr.empty()) {
        Json::Value entitlements;
        Json::CharReaderBuilder readerBuilder;
        std::string errs;
        std::istringstream s(entitlementsStr);
        if (Json::parseFromStream(readerBuilder, s, &entitlements, &errs)) {
          for (const auto &ent : entitlements) {
            if (ent.isObject() && ent.isMember("id") &&
                ent["id"].asString() == newsPaperId) {
              response.success = true;
              response.message = "You already have access to this newspaper.";
              response.result["hasAccess"] = true;
              co_return response;
            }
          }
        }
      }
    }

    // 4. Create Purchase Attempt
    CoroMapper<PurchaseAttempts> paMapper(dbClient);
    PurchaseAttempts newPurchaseAttempt;
    newPurchaseAttempt.setUserId(userId);
    newPurchaseAttempt.setNewspaperId(newsPaperId);
    newPurchaseAttempt.setAttemptReference(clientReference);
    newPurchaseAttempt.setAmount(paperCost);
    newPurchaseAttempt.setStatus("Initiated");
    newPurchaseAttempt.setFailureReasonToNull();
    newPurchaseAttempt.setCreatedAt(trantor::Date::now());

    co_await paMapper.insert(newPurchaseAttempt);

    // 5. Create Payment Record (Async)
    auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
    auto &paymentService = plugin->getPaymentService();

    gnp::dto::CreatePaymentDto paymentDto;
    paymentDto.setUserId(userId);
    paymentDto.setUserName(user.getValueOfUsername());
    paymentDto.setUserEmail(user.getValueOfEmail());
    paymentDto.setPackageName(*newspaper.getTitle());
    paymentDto.setAmountPaid(paperCost);
    paymentDto.setReceiptNo(clientReference);
    paymentDto.setTransactionReference(clientReference);
    paymentDto.setStatus("Initiated");

    co_await paymentService.createPaymentAsync(paymentDto);

    // 6. Initialize Paystack Payment (Async)
    auto &paystackApi = plugin->getPaystackApi();

    gnp::dto::InitializePaymentRequest initReq;
    initReq.setAmount(paperCost);
    initReq.setPhone(user.getValueOfPhoneNumber());
    initReq.setEmail(user.getValueOfEmail());
    initReq.setClientReference(clientReference);
    initReq.setCallBackUrl("https://gnp-api.com/paystack/callback");

    auto payResp = co_await paystackApi.initializeAsync(initReq);
    if (!payResp.getStatus()) {
      response.success = false;
      response.message = payResp.getMessage().empty()
                             ? "Failed to initialize payment"
                             : payResp.getMessage();
      co_return response;
    }

    const auto &payData = payResp.getData();
    response.success = true;
    response.message = "One-time purchase initialized successfully";
    response.result["paymentUrl"] = payData.getAuthorizationUrl();
    response.result["reference"] = payData.getReference();

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.message = "Database error: " + std::string(e.base().what());
    response.error["code"] = constants::ERR_DB_QUERY;
  } catch (const std::exception &e) {
    response.success = false;
    response.message = "An error occurred: " + std::string(e.what());
  }

  co_return response;
}

drogon::Task<gnp::dto::BaseApiResponse>
SubscriptionService::buyCopy(const std::string &userId,
                             const dto::BuyNewspaperCopyDto &dto) {

  auto dbClient = drogon::app().getDbClient();
  dto::BaseApiResponse response;

  try {
    // 1. Find Newspaper
    CoroMapper<drogon_model::Gnp::Newspapers> newspaperMapper(dbClient);
    auto newspaper =
        co_await newspaperMapper.findByPrimaryKey(dto.getNewspaperId());

    const auto &pricePtr = newspaper.getPrice();
    const std::string &paperCost = *pricePtr;
    std::string clientReference = gnp::utils::IdGeneratorUtils::generateGuid();

    std::string targetUserId;
    std::string targetUserEmail;
    std::string targetUserPhone;
    std::string targetUserName;

    CoroMapper<Users> userMapper(dbClient);

    // Determine email to use (provided or generated from phone)
    std::string emailToUse = dto.getPhoneNumber() + "@graphicnewsplus.com.gh";

    Criteria checkCriteria =
        Criteria(Users::Cols::_email, CompareOperator::EQ, emailToUse);
    bool createNewUser = false;
    try {
      auto user = co_await userMapper.findOne(checkCriteria);
      targetUserId = user.getValueOfId();
      targetUserEmail = user.getValueOfEmail();
      targetUserPhone = user.getValueOfPhoneNumber();
      targetUserName = user.getValueOfUsername();
    } catch (const drogon::orm::UnexpectedRows &) {
      createNewUser = true;
    }

    if (createNewUser) {
      // User doesn't exist, create new
      Users newUser;

      newUser.setFirstName(dto.getFirstName());
      newUser.setLastName(dto.getLastName());
      newUser.setEmail(emailToUse);
      newUser.setPhoneNumber(dto.getPhoneNumber());
      newUser.setIsActive(true);
      newUser.setIsLockedOut(false);
      newUser.setCreatedAt(trantor::Date::now());

      auto user = co_await userMapper.insert(newUser);
      targetUserId = user.getValueOfId();
      targetUserEmail = user.getValueOfEmail();
      targetUserPhone = user.getValueOfPhoneNumber();
      targetUserName = dto.getFirstName() + " " + dto.getLastName();

      // Create User Subscription (inactive) for the new user
      CoroMapper<drogon_model::Gnp::UserSubscriptions> subMapper(dbClient);
      UserSubscriptions newUserSubscription;
      newUserSubscription.setSubscriptionIdentifier(
          gnp::utils::IdGeneratorUtils::generateRandomSixDigit());
      newUserSubscription.setUserId(targetUserId);
      newUserSubscription.setEmail(targetUserEmail);
      newUserSubscription.setIsActive(false);
      newUserSubscription.setCreatedAt(trantor::Date::now());

      co_await subMapper.insert(newUserSubscription);
    }

    // 3. Check for existing entitlement (UserSubscription) for TARGET USER
    CoroMapper<UserSubscriptions> subMapper(dbClient);
    Criteria subCriteria(UserSubscriptions::Cols::_user_id, CompareOperator::EQ,
                         targetUserId);

    auto userSubscriptions = co_await subMapper.findBy(subCriteria);

    if (!userSubscriptions.empty()) {
      const auto &userSub = userSubscriptions[0];
      std::string entitlementsStr = userSub.getValueOfNewspaperEntitlements();
      if (!entitlementsStr.empty()) {
        Json::CharReaderBuilder readerBuilder;
        std::string errs;
        std::istringstream s(entitlementsStr);
        Json::Value entitlements;
        if (Json::parseFromStream(readerBuilder, s, &entitlements, &errs)) {
          for (const auto &ent : entitlements) {
            if (ent.isObject() && ent.isMember("id") &&
                ent["id"].asString() == dto.getNewspaperId()) {
              response.success = true;
              response.message =
                  "The recipient already has access to this newspaper.";
              response.result["hasAccess"] = true;
              co_return response;
            }
          }
        }
      }
    }

    // 4. Create Purchase Attempt linked to TARGET USER
    CoroMapper<PurchaseAttempts> paMapper(dbClient);
    PurchaseAttempts newPurchaseAttempt;
    newPurchaseAttempt.setUserId(targetUserId);
    newPurchaseAttempt.setNewspaperId(dto.getNewspaperId());
    newPurchaseAttempt.setAttemptReference(clientReference);
    newPurchaseAttempt.setAmount(paperCost);
    newPurchaseAttempt.setStatus("Initiated");
    newPurchaseAttempt.setFailureReasonToNull();
    newPurchaseAttempt.setCreatedAt(trantor::Date::now());

    co_await paMapper.insert(newPurchaseAttempt);

    // 5. Create Payment Record (Async) linked to TARGET USER
    auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
    auto &paymentService = plugin->getPaymentService();

    gnp::dto::CreatePaymentDto paymentDto;
    paymentDto.setUserId(targetUserId);
    paymentDto.setUserName(targetUserName);
    paymentDto.setUserEmail(targetUserEmail);
    paymentDto.setPackageName(*newspaper.getTitle());
    paymentDto.setAmountPaid(paperCost);
    paymentDto.setReceiptNo(clientReference);
    paymentDto.setTransactionReference(clientReference);
    paymentDto.setStatus("Initiated");

    co_await paymentService.createPaymentAsync(paymentDto);

    auto &paystackApi = plugin->getPaystackApi();

    gnp::dto::InitializePaymentRequest initReq;
    initReq.setAmount(paperCost);
    initReq.setPhone(dto.getPhoneNumber()+ "@graphicnewsplus.com.gh");
    initReq.setEmail(targetUserEmail);
    initReq.setClientReference(clientReference);
    initReq.setCallBackUrl("https://gnp-api.com/paystack/callback");

    auto payResp = co_await paystackApi.initializeAsync(initReq);
    if (!payResp.getStatus()) {
      response.success = false;
      response.message = payResp.getMessage().empty()
                             ? "Failed to initialize payment"
                             : payResp.getMessage();
      co_return response;
    }

    const auto &payData = payResp.getData();
    response.success = true;
    response.message = "Purchase initialized successfully";
    response.result["paymentUrl"] = payData.getAuthorizationUrl();
    response.result["reference"] = payData.getReference();

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.message = "Database error: " + std::string(e.base().what());
    response.error["code"] = constants::ERR_DB_QUERY;
  } catch (const std::exception &e) {
    response.success = false;
    response.message = "An error occurred: " + std::string(e.what());
  }

  co_return response;
}

drogon::Task<gnp::dto::BaseApiResponse> SubscriptionService::fulFillBuyCopy(const std::string &reference) {
  auto dbClient = drogon::app().getDbClient();
  dto::BaseApiResponse response;

  try {
    // 1. Find Purchase Attempt
    CoroMapper<PurchaseAttempts> purchaseAttemptMapper(dbClient);
    Criteria criteria = Criteria(PurchaseAttempts::Cols::_attempt_reference,
                                 CompareOperator::EQ, reference);
    auto purchaseAttempt = co_await purchaseAttemptMapper.findOne(criteria);

    // 2. Verify Payment with Paystack
    auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
    auto &paystackApi = plugin->getPaystackApi();
    auto verifyPayResponse = co_await paystackApi.verifyAsync(reference);

    if (!verifyPayResponse.getStatus()) {
      response.success = false;
      response.message = !verifyPayResponse.getMessage().empty()
                             ? verifyPayResponse.getMessage()
                             : "Failed to verify payment";
      co_return response;
    }

    const auto &verifyData = verifyPayResponse.getData();
    auto paymentService = std::make_shared<gnp::services::PaymentService>();

    if (verifyData.status_ == "success") {
      // 3. Update status to Success
      purchaseAttempt.setStatus("Success");
      purchaseAttempt.setFailureReasonToNull();
      co_await purchaseAttemptMapper.update(purchaseAttempt);

      co_await paymentService->updateStatusAsync("Success", reference);

      // Increment Newspaper Sales
      try {
        CoroMapper<drogon_model::Gnp::Newspapers> newspaperMapper(dbClient);
        auto newspaper = co_await newspaperMapper.findByPrimaryKey(
            purchaseAttempt.getValueOfNewspaperId());

        std::string salesStr = newspaper.getValueOfSales();
        long long sales = 0;
        if (!salesStr.empty()) {
          try {
            sales = std::stoll(salesStr);
          } catch (...) {
            sales = 0;
          }
        }
        sales++;
        newspaper.setSales(std::to_string(sales));
        co_await newspaperMapper.update(newspaper);
        // Report telemetry: single-copy sale (edition_sales_ledger)
        {
          double saleAmount = 0.0;
          double salePrice = 0.0;
          try { saleAmount = std::stod(purchaseAttempt.getValueOfAmount()); } catch (...) {}
          try { salePrice = std::stod(newspaper.getValueOfPrice()); } catch (...) {}
          co_await reporting::recordEditionSale(
              newspaper.getValueOfId(), newspaper.getValueOfPublicationId(),
              purchaseAttempt.getValueOfUserId(), "", false, "", "",
              salePrice, 0.0, saleAmount);
        }
      } catch (...) {
      }

      // 4. Update User Subscription Entitlements for TARGET USER
      std::string targetUserId = purchaseAttempt.getValueOfUserId();

      CoroMapper<UserSubscriptions> subMapper(dbClient);
      Criteria subCriteria(UserSubscriptions::Cols::_user_id,
                           CompareOperator::EQ, targetUserId);
      auto userSub = co_await subMapper.findOne(subCriteria);

      Json::Value entitlements;
      std::string currentEntitlementsStr =
          userSub.getValueOfNewspaperEntitlements();

      if (!currentEntitlementsStr.empty()) {
        Json::CharReaderBuilder readerBuilder;
        std::string errs;
        std::istringstream s(currentEntitlementsStr);
        if (!Json::parseFromStream(readerBuilder, s, &entitlements, &errs)) {
          entitlements = Json::arrayValue;
        }
      } else {
        entitlements = Json::arrayValue;
      }

      std::string newPaperId = purchaseAttempt.getValueOfNewspaperId();
      bool alreadyExists = false;

      for (const auto &ent : entitlements) {
        if (ent.isObject() && ent.isMember("id") && ent["id"].isString() &&
            ent["id"].asString() == newPaperId) {
          alreadyExists = true;
          break;
        } else if (ent.isString() && ent.asString() == newPaperId) {
          alreadyExists = true;
          break;
        }
      }

      std::string uniqueId;
      if (!alreadyExists) {
        Json::Value newEnt;
        newEnt["id"] = newPaperId;
        uniqueId = gnp::utils::IdGeneratorUtils::generateAlphanumericId();
        newEnt["uniqueId"] = uniqueId;
        entitlements.append(newEnt);
      }

      Json::StreamWriterBuilder writerBuilder;
      writerBuilder["indentation"] = "";
      userSub.setNewspaperEntitlements(
          Json::writeString(writerBuilder, entitlements));
      userSub.setIsActive(true);
      co_await subMapper.update(userSub);

      // 5. Check if User needs credentials (Password check)
      CoroMapper<Users> userMapper(dbClient);
      auto user = co_await userMapper.findByPrimaryKey(targetUserId);
      std::string pwdHash = user.getValueOfPasswordHash();

      if (pwdHash.empty()) {
        // New User - Generate Credentials & Send Email
        std::string firstName = user.getValueOfFirstName();
        std::string phoneNumber = user.getValueOfPhoneNumber();
        std::string password;
        if (phoneNumber.length() >= 4) {
          password =
              firstName + "@" + phoneNumber.substr(phoneNumber.length() - 4);
        } else {
          password = firstName + "@" + phoneNumber;
        }
        std::string newPasswordHash = bcrypt::generateHash(password);

        user.setPasswordHash(newPasswordHash);
        co_await userMapper.update(user);

        std::string messageContent =
          "Hello, someone purchased a copy of the newspaper for you. Your digital newspaper copy is ready. Access it here: "
          "https://dev.graphicnewsplus.com/newspapers/" +
          uniqueId + "/open . Login using Phone: " + phoneNumber +
          " & Password: " + password;

        // Send an SMS containing a uniqueId to the newspaper: add the password
        // to their account in their message
        auto &hubtelSmsApi = plugin->getHubtelSmsApi();
        co_await hubtelSmsApi.sendSms(phoneNumber, messageContent);
      } else {
        // Existing User - Maybe send a notification email?
        // For now, we stay silent or generic success message.
      }

      response.success = true;
      response.message = "Payment verified. Access granted.";

    } else {
      // Payment Failed
      purchaseAttempt.setStatus("Failed");
      purchaseAttempt.setFailureReason(verifyData.message_);
      co_await purchaseAttemptMapper.update(purchaseAttempt);

      co_await paymentService->updateStatusAsync("Failed", reference);

      response.success = false;
      response.message =
          "Payment verification failed. Status: " + verifyData.status_;
    }

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.message = "Database error: " + std::string(e.base().what());
    response.error["code"] = constants::ERR_DB_QUERY;
  } catch (const std::exception &e) {
    response.success = false;
    response.message = "An error occurred: " + std::string(e.what());
  }

  co_return response;
}


drogon::Task<void> SubscriptionService::dispatchSubscriptionRenewalReminder() {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<UserSubscriptions> mp(dbClient);

  try {

    auto now = trantor::Date::now();
    auto sevenDaysLater = now.after(7 * 24 * 3600);

    auto todayStr = now.toFormattedString("%Y-%m-%d");
    auto sevenDaysLaterStr = sevenDaysLater.toFormattedString("%Y-%m-%d");

    // auto subscriptions = co_await mp.findBy(drogon::orm::Criteria(drogon_model::Gnp::UserSubscriptions::Cols::_is_active, drogon::orm::CompareOperator::EQ, true));
    auto subscriptions = co_await mp.findBy(Criteria(UserSubscriptions::Cols::_email, CompareOperator::EQ, "vavy712@gmail.com") &&
                                           Criteria(UserSubscriptions::Cols::_end_date, CompareOperator::GE, todayStr) &&
                                           Criteria(UserSubscriptions::Cols::_end_date, CompareOperator::LE, sevenDaysLaterStr));

    auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
    auto &emailService = plugin->getEmailService();

    for (const auto &sub : subscriptions) {
      if (sub.getValueOfEmail().empty()) continue;

      // Personalization
      std::string userName = "Subscriber";
      if (!sub.getValueOfUserId().empty()) {
        try {
          CoroMapper<Users> userMapper(dbClient);
          auto user = co_await userMapper.findByPrimaryKey(sub.getValueOfUserId());
          if (!user.getValueOfFirstName().empty()) {
            userName = user.getValueOfFirstName();
          }
        } catch (...) {
          // Fall back silently
        }
      }

      // Past Renewal History Table Block
      std::string historyHtml = "";
      if (!sub.getValueOfUserId().empty()) {
        try {
          CoroMapper<SubscriptionRenewalHistory> historyMapper(dbClient);
          auto history = co_await historyMapper.findBy(
              Criteria(SubscriptionRenewalHistory::Cols::_user_id, CompareOperator::EQ, sub.getValueOfUserId()));

          if (!history.empty()) {
            historyHtml += R"(
              <div style="margin-top: 32px; border-top: 1px solid #fee2e2; padding-top: 24px;">
                <h3 style="font-size: 15px; color: #111827; margin: 0 0 16px 0; font-weight: 700; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;">
                  Payment & Renewal History
                </h3>
                <table border="0" cellpadding="0" cellspacing="0" width="100%" style="border-collapse: separate; font-size: 13px; text-align: left; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; border: 1px solid #fee2e2; border-radius: 8px; overflow: hidden;">
                  <thead>
                    <tr style="background-color: #fff1f2; color: #991b1b;">
                      <th style="padding: 10px 12px; font-weight: 700; border-bottom: 1px solid #fecdd3;">Plan</th>
                      <th style="padding: 10px 12px; font-weight: 700; border-bottom: 1px solid #fecdd3;">Cycle</th>
                      <th style="padding: 10px 12px; font-weight: 700; border-bottom: 1px solid #fecdd3;">Amount</th>
                      <th style="padding: 10px 12px; font-weight: 700; border-bottom: 1px solid #fecdd3;">Date</th>
                      <th style="padding: 10px 12px; font-weight: 700; border-bottom: 1px solid #fecdd3;">Status</th>
                    </tr>
                  </thead>
                  <tbody>
            )";

            for (const auto& record : history) {
              std::string dateStr = record.getValueOfCreatedAt().toCustomFormattedStringLocal("%d %b, %Y");
              bool isSuccess = (record.getValueOfTransactionStatus() == "Successful");

              std::string statusBg    = isSuccess ? "#dcfce7" : "#fee2e2";
              std::string statusColor = isSuccess ? "#16a34a" : "#dc2626";
              std::string amountStr   = record.getValueOfAmountPaid().empty() ? "N/A" : record.getValueOfAmountPaid();

              historyHtml += R"(
                <tr style="border-bottom: 1px solid #f8fafc; background-color: #ffffff;">
                  <td style="padding: 10px 12px; color: #374151; font-weight: 500;">)" + record.getValueOfSubscriptionPlanName() + R"(</td>
                  <td style="padding: 10px 12px; color: #6b7280;">)" + record.getValueOfCurrentBillingCycle() + R"(</td>
                  <td style="padding: 10px 12px; color: #374151; font-weight: 600;">)" + amountStr + R"(</td>
                  <td style="padding: 10px 12px; color: #6b7280;">)" + dateStr + R"(</td>
                  <td style="padding: 10px 12px;">
                    <span style="display: inline-block; padding: 2px 8px; border-radius: 12px; font-size: 11px; font-weight: 700; background-color: )" + statusBg + R"(; color: )" + statusColor + R"(;">
                      )" + record.getValueOfTransactionStatus() + R"(
                    </span>
                  </td>
                </tr>
              )";
            }

            historyHtml += R"(
                  </tbody>
                </table>
              </div>
            )";
          }
        } catch (...) {
          // Fall back silently
        }
      }

      std::string emailBody = R"(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <meta http-equiv="X-UA-Compatible" content="IE=edge" />
  <title>Graphic News Plus - Subscription Renewal Reminder</title>
</head>
<body style="margin: 0; padding: 0; background-color: #fcf8f8; -webkit-font-smoothing: antialiased;">
  <table border="0" cellpadding="0" cellspacing="0" width="100%" style="background-color: #fcf8f8; padding: 40px 10px;">
    <tr>
      <td align="center">
        <!-- Main Container -->
        <table border="0" cellpadding="0" cellspacing="0" width="100%" style="max-width: 600px; background-color: #ffffff; border-radius: 16px; overflow: hidden; box-shadow: 0 10px 30px rgba(185, 28, 28, 0.08); border: 1px solid #fee2e2;">

          <!-- Crimson Header -->
          <tr>
            <td style="background-color: #991b1b; background: linear-gradient(135deg, #7f1d1d 0%, #b91c1c 100%); padding: 36px 32px; text-align: left;">
              <table border="0" cellpadding="0" cellspacing="0" width="100%">
                <tr>
                  <td>
                    <span style="display: inline-block; font-size: 11px; font-weight: 700; letter-spacing: 1.5px; text-transform: uppercase; color: #fecdd3; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;">
                      Graphic News Plus
                    </span>
                    <h1 style="margin: 6px 0 0 0; font-size: 24px; font-weight: 700; color: #ffffff; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; letter-spacing: -0.3px;">
                      Subscription Renewal
                    </h1>
                  </td>
                </tr>
              </table>
            </td>
          </tr>

          <!-- Red Decorative Accent Line -->
          <tr>
            <td style="background-color: #dc2626; height: 4px; line-height: 4px; font-size: 4px;">&nbsp;</td>
          </tr>

          <!-- Body Content -->
          <tr>
            <td style="padding: 32px;">
              <p style="margin: 0 0 20px 0; font-size: 15px; line-height: 1.6; color: #374151; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;">
                Hi <strong style="color: #991b1b;">)" + userName + R"(</strong>,
              </p>

              <p style="margin: 0 0 24px 0; font-size: 15px; line-height: 1.6; color: #374151; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;">
                We hope you are enjoying your experience on <strong>Graphic News Plus</strong>! This is a friendly reminder that your subscription is coming up for renewal soon.
              </p>

              <!-- Notice Callout Box -->
              <table border="0" cellpadding="0" cellspacing="0" width="100%" style="background-color: #fff1f2; border: 1px solid #fecdd3; border-left: 4px solid #b91c1c; border-radius: 8px; margin-bottom: 24px;">
                <tr>
                  <td style="padding: 16px 20px;">
                    <p style="margin: 0; font-size: 14px; line-height: 1.5; color: #991b1b; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;">
                      <strong>Action Recommended:</strong> Please verify that your payment method and billing details are up to date to prevent any interruption to your access.
                    </p>
                  </td>
                </tr>
              </table>

              <!-- Action Button -->
              <div style="text-align: center; margin: 28px 0;">
                <a href="https://new.graphicnewsplus.com/account/subscription" target="_blank" style="display: inline-block; padding: 12px 28px; background-color: #b91c1c; color: #ffffff; text-decoration: none; border-radius: 8px; font-size: 14px; font-weight: 700; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; box-shadow: 0 4px 12px rgba(185, 28, 28, 0.2);">
                  Manage Subscription &rarr;
                </a>
              </div>

              <!-- History Block Injection -->
              )" + historyHtml + R"(

            </td>
          </tr>

          <!-- Footer -->
          <tr>
            <td style="background-color: #fff1f2; padding: 24px 32px; text-align: center; border-top: 1px solid #ffe4e6;">
              <p style="margin: 0 0 8px 0; font-size: 12px; line-height: 1.5; color: #9f1239; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;">
                Thank you for being a valued subscriber to <strong>Graphic News Plus</strong>.
              </p>
              <p style="margin: 0; font-size: 12px; color: #f43f5e; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;">
                &copy; Graphic News Plus. All rights reserved.
              </p>
            </td>
          </tr>

        </table>
      </td>
    </tr>
  </table>
</body>
</html>)";

      gnp::dto::SendEmailDto emailDto;
      emailDto.setTo(sub.getValueOfEmail());
      emailDto.setSubject("Subscription Renewal Reminder — Graphic News Plus");
      emailDto.setBody(emailBody);

      // Dispatch email
      co_await emailService.sendEmailAsync(emailDto);
    }
  } catch (const std::exception &e) {
    LOG_ERROR << "Failed to dispatch subscription renewal reminder: " << e.what();
  }
}



drogon::Task<void> SubscriptionService::unsubscribeNotifications(const std::string &userId, int notificationType) {

  auto dbClient = drogon::app().getDbClient();

  try {
    CoroMapper<UserNotificationSubscriptions> notifMapper(dbClient);

    // Look up an existing record for this user + notification type
    Criteria criteria =
        Criteria(UserNotificationSubscriptions::Cols::_user_id, CompareOperator::EQ, userId) &&
        Criteria(UserNotificationSubscriptions::Cols::_notification_type, CompareOperator::EQ, notificationType);

    auto records = co_await notifMapper.findBy(criteria);

    if (!records.empty()) {
      // Update the existing record to UNSUBSCRIBED
      auto &record = records.front();
      record.setSubscriptionStatus(
          static_cast<int32_t>(gnp::constants::SubscriptionStatus::UNSUBSCRIBED));
      record.setUpdatedAt(trantor::Date::now());
      co_await notifMapper.update(record);

      LOG_INFO << "User " << userId
               << " unsubscribed from notification type " << notificationType;
    } else {
      // Insert a new record with UNSUBSCRIBED status (opt-out from the start)
      UserNotificationSubscriptions newRecord;
      newRecord.setId(gnp::utils::IdGeneratorUtils::generateGuid());
      newRecord.setUserId(userId);
      newRecord.setNotificationType(static_cast<int32_t>(notificationType));
      newRecord.setSubscriptionStatus(static_cast<int32_t>(gnp::constants::SubscriptionStatus::UNSUBSCRIBED));
      newRecord.setCreatedAt(trantor::Date::now());
      newRecord.setUpdatedAt(trantor::Date::now());
      co_await notifMapper.insert(newRecord);

      LOG_INFO << "User " << userId
               << " inserted as unsubscribed for notification type "
               << notificationType;
    }
  } catch (const std::exception &e) {
    LOG_ERROR << "Failed to unsubscribe notifications for user " << userId
              << ": " << e.what();
  }
}


} // namespace gnp::services