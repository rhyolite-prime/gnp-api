//
// Created by Emmanuel Addo-Odame on 27/08/2026.
//

#ifndef GNPAPI_AFFILIATESIGNUPDTO_H
#define GNPAPI_AFFILIATESIGNUPDTO_H
#include <json/json.h>
#include <string>

namespace gnp::dto {

    class AffiliateSignupDto {

    public:

        AffiliateSignupDto() = default;

        void fromJson(const Json::Value& json);

        // Getters
        [[nodiscard]] const std::string& getFirstName() const { return first_name_; }
        [[nodiscard]] const std::string& getLastName() const { return last_name_; }
        [[nodiscard]] const std::string& getEmail() const { return email_; }
        [[nodiscard]] const std::string& getPassword() const { return password_; }
        [[nodiscard]] const std::string& getPhoneNumber() const { return phone_number_; }

        // Setters
        void setFirstName(const std::string& value) { first_name_ = value; }
        void setLastName(const std::string& value) { last_name_ = value; }
        void setEmail(const std::string& value) { email_ = value; }
        void setPassword(const std::string& value) { password_ = value; }
        void setPhoneNumber(const std::string& value) { phone_number_ = value; }


    private:

        std::string first_name_;
        std::string last_name_;
        std::string email_;
        std::string password_;
        std::string phone_number_;

    };

    inline void AffiliateSignupDto::fromJson(const Json::Value& json) {

        if (json.isMember("firstName") && !json["firstName"].isNull()) {
            setFirstName(json["firstName"].asString());
        }

        if (json.isMember("lastName") && !json["lastName"].isNull()) {
            setLastName(json["lastName"].asString());
        }

         if (json.isMember("email") && !json["email"].isNull()) {
             setEmail(json["email"].asString());
         }

        if (json.isMember("password") && !json["password"].isNull()) {
            setPassword(json["password"].asString());
        }

        if (json.isMember("phoneNo") && !json["phoneNo"].isNull()) {
           setPhoneNumber(json["phoneNo"].asString());
        }

    }

}

#endif //GNPAPI_AFFILIATESIGNUPDTO_H
