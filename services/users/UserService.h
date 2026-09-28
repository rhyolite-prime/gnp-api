#pragma once

#ifndef USERSERVICE_H
#define USERSERVICE_H

#include "dto/BaseApiResponse.h"
#include "dto/UserDto.h"
#include "dto/SigninDto.h"
#include <drogon/drogon.h>

#include "dto/AdminUserDto.h"
#include "dto/ChangePasswordDto.h"
#include "dto/LoginUserPasskeyDto.h"
#include "dto/RegisterUserPasskeysDto.h"
#include "dto/VerifyPartnerUserOtpDto.h"

namespace gnp::services {

class UserService {
public: 

    // use redis to track the number of users who have logged in to a particular account

  drogon::Task<dto::BaseApiResponse> getAll(int pageNo, int pageSize, const std::string &query);

  drogon::Task<dto::BaseApiResponse> getAllSubscribers(int pageNo, int pageSize, const std::string &query, const std::string &dateJoinedStartDate, const std::string &dateJoinedEndDate, const std::string &lastActiveStartDate, const std::string &lastActiveEndDate, const std::string &accountType);

  drogon::Task<dto::BaseApiResponse> getDetails(const std::string &userId);

  drogon::Task<dto::BaseApiResponse> getAdminUsers(int pageNo, int pageSize, const std::string &query);

  drogon::Task<dto::BaseApiResponse> getPartnerAdminUsers(const std::string &partnerId, int pageNo, int pageSize, const std::string &query);

  drogon::Task<dto::BaseApiResponse> getPartnerSubscribers(const std::string &partnerId, int pageNo, int pageSize, const std::string &query);

  drogon::Task<dto::BaseApiResponse> create(const dto::UserDto &userDto);

  drogon::Task<dto::BaseApiResponse> update(const dto::UserDto &userDto, const std::string &userId);

  drogon::Task<dto::BaseApiResponse> invitePartnerAdminUser(const dto::AdminUserDto &adminUserDto, const std::string &partnerId);

  drogon::Task<dto::BaseApiResponse> updatePartnerAdminUser(const dto::AdminUserDto &adminUserDto, const std::string &adminUserId, const std::string &partnerId);

  drogon::Task<dto::BaseApiResponse> registerUserPasskeys(const dto::RegisterUserPasskeysDto &passKeysDto);

  drogon::Task<dto::BaseApiResponse> updateProfileImage(const std::string &userId, const std::string &logoContent);

  drogon::Task<gnp::dto::BaseApiResponse> lockUserAccount(const std::string &userId);

  drogon::Task<gnp::dto::BaseApiResponse> registerProspectiveUser(const dto::UserDto &userDto);

  drogon::Task<gnp::dto::BaseApiResponse> unlockUserAccount(const std::string &userId);

  drogon::Task<gnp::dto::BaseApiResponse> activateUserAccount(const std::string &userId);

  drogon::Task<gnp::dto::BaseApiResponse> deactivateUserAccount(const std::string &userId);

  drogon::Task<gnp::dto::BaseApiResponse> deleteUser(const std::string &userId);

  drogon::Task<gnp::dto::BaseApiResponse> resetUserPassword(const std::string &userId);

  drogon::Task<gnp::dto::BaseApiResponse> deletePartnerAdminUser(const std::string &userId, const std::string &partnerId);


  // auth
  drogon::Task<dto::BaseApiResponse> validateUserCredentials(const dto::SigninDto &signin_dto);

  drogon::Task<dto::BaseApiResponse> validateAffiliateUserCredentials(const dto::SigninDto &signin_dto);

  drogon::Task<dto::BaseApiResponse> validateUserPasskeys(const dto::LoginUserPasskeyDto &passkeyDto);

  drogon::Task<dto::BaseApiResponse> validateAdminUserCredentials(const dto::SigninDto &signin_dto);

  drogon::Task<dto::BaseApiResponse> validatePartnerUserCredentials(const dto::SigninDto &signin_dto);

  drogon::Task<dto::BaseApiResponse> validatePartnerUserOtp(const dto::VerifyPartnerUserOtpDto &dto);

    // user profile
    drogon::Task<dto::BaseApiResponse> getUserMetaData(const std::string &userId);

    drogon::Task<dto::BaseApiResponse> changeUserPassword(std::string userId, std::string accountType, const dto::ChangePasswordDto &dto);


  void checkAccountStatus(
      const std::string &identifier, const std::string &identifierType,
      const std::function<void(const gnp::dto::BaseApiResponse &)> &callback);

  void sendOtp(
      const std::string &userEmail,
      const std::function<void(const gnp::dto::BaseApiResponse &)> &callback);

  void verifyOtp(
      const std::string &userEmail, const std::string &otp,
      const std::string &requestId,
      const std::function<void(const gnp::dto::BaseApiResponse &)> &callback);

  void setPassword(
      const std::string &sessionId, const std::string &password,
      const std::string &confirmPassword,
      const std::function<void(const gnp::dto::BaseApiResponse &)> &callback);
};

} // namespace gnp::services

#endif // USERSERVICE_H
