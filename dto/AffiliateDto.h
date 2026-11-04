//
// Created by Emmanuel Addo-Odame on 11/02/2026.
//

#ifndef GNPAPI_CREATEAFFILIATEDTO_H
#define GNPAPI_CREATEAFFILIATEDTO_H
#include <json/json.h>
#include <optional>
#include <string>
#include <trantor/utils/Date.h>

namespace gnp::dto {

class AffiliateDto {

public:
  AffiliateDto() = default;

  void fromJson(const Json::Value &json);

  // Getters
  [[nodiscard]] const std::string &getFirstName() const { return first_name_; }
  [[nodiscard]] const std::string &getLastName() const { return last_name_; }
  [[nodiscard]] const std::string &getEmail() const { return email_; }
  [[nodiscard]] const std::string &getPhone() const { return phone_; }
  [[nodiscard]] int getStatus() const { return status_; }
  [[nodiscard]] const trantor::Date &getDateJoined() const { return date_joined_; }

  // Setters
  void setFirstName(const std::string &value) { first_name_ = value; }
  void setLastName(const std::string &value) { last_name_ = value; }
  void setEmail(const std::string &value) { email_ = value; }
  void setPhone(const std::string &value) { phone_ = value; }
  void setStatus(const int status) { status_ = status; }
  void setDateJoined(const trantor::Date &dateJoined) { date_joined_ = dateJoined; }


private:
  std::string first_name_;
  std::string last_name_;
  std::string email_;
  std::string phone_;
  trantor::Date date_joined_;
  int status_ {0};

};

inline void AffiliateDto::fromJson(const Json::Value &json) {

  if (json.isMember("firstName") && !json["firstName"].isNull()) {
    setFirstName(json["firstName"].asString());
  }

  if (json.isMember("lastName") && !json["lastName"].isNull()) {
    setLastName(json["lastName"].asString());
  }

  if (json.isMember("email") && !json["email"].isNull()) {
    setEmail(json["email"].asString());
  }

  if (json.isMember("phone") && !json["phone"].isNull()) {
    setPhone(json["phone"].asString());
  }


}

} // namespace gnp::dto

#endif // GNPAPI_CREATEAFFILIATEDTO_H