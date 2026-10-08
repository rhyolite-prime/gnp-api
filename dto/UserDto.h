//
// Created by Emmanuel Addo-Odame on 13/11/2025.
//

#ifndef CREATEUSERDTO_H
#define CREATEUSERDTO_H

#include <json/json.h>

namespace gnp::dto {

    class UserDto {

    public:

        UserDto() = default;

        void fromJson(const Json::Value& json);

        // Getters
        [[nodiscard]] const std::string& getFirstName() const { return first_name_; }
        [[nodiscard]] const std::string& getLastName() const { return last_name_; }
        [[nodiscard]] const std::string& getEmail() const { return email_; }
        [[nodiscard]] const std::string& getUsername() const { return username_; }
        [[nodiscard]] const std::string& getPassword() const { return password_; }
        [[nodiscard]] const std::string& getPhoneNumber() const { return phone_number_; }
        [[nodiscard]] const std::string& getCountry() const { return country_; }

        // Setters
        void setFirstName(const std::string& value) { first_name_ = value; }
        void setLastName(const std::string& value) { last_name_ = value; }
        void setEmail(const std::string& value) { email_ = value; }
        void setUsername(const std::string& value) { username_ = value; }
        void setPassword(const std::string& value) { password_ = value; }
        void setPhoneNumber(const std::string& value) { phone_number_ = value; }
        void setCountry(const std::string& value) { country_ = value; }


    private:

        std::string first_name_;
        std::string last_name_;
        std::string username_;
        std::string email_;
        std::string password_;
        std::string phone_number_;
        std::string country_;
    };

    inline void UserDto::fromJson(const Json::Value& json) {

        if (json.isMember("firstName") && !json["firstName"].isNull()) {
            first_name_ = json["firstName"].asString();
        }

        if (json.isMember("lastName") && !json["lastName"].isNull()) {
            last_name_ = json["lastName"].asString();
        }

        if (json.isMember("username") && !json["username"].isNull()) {
            username_ = json["username"].asString();
        }

         if (json.isMember("email") && !json["email"].isNull()) {
             email_ = json["email"].asString();
         }

        if (json.isMember("password") && !json["password"].isNull()) {
            password_ = json["password"].asString();
        }

        if (json.isMember("phoneNumber") && !json["phoneNumber"].isNull()) {
            phone_number_ = json["phoneNumber"].asString();
        }

        if (json.isMember("country") && !json["country"].isNull()) {
            country_ = json["country"].asString();
        }

    }

    class AdminUserDto {

    public:

        AdminUserDto() = default;

        void fromJson(const Json::Value& json);

        // Getters
        [[nodiscard]] const std::string& getFirstName() const { return first_name_; }
        [[nodiscard]] const std::string& getLastName() const { return last_name_; }
        [[nodiscard]] const std::string& getEmail() const { return email_; }
        [[nodiscard]] const std::string& getUsername() const { return username_; }
        [[nodiscard]] const std::string& getPassword() const { return password_; }
        [[nodiscard]] const std::string& getPhoneNumber() const { return phone_number_; }
        [[nodiscard]] const std::string& getCountry() const { return country_; }
        [[nodiscard]] const std::vector<std::string> &getUserGroups() const { return user_groups_; }
        [[nodiscard]] const std::vector<std::string> &getRoles() const { return roles_; }

        // Setters
        void setFirstName(const std::string& value) { first_name_ = value; }
        void setLastName(const std::string& value) { last_name_ = value; }
        void setEmail(const std::string& value) { email_ = value; }
        void setUsername(const std::string& value) { username_ = value; }
        void setPassword(const std::string& value) { password_ = value; }
        void setPhoneNumber(const std::string& value) { phone_number_ = value; }
        void setCountry(const std::string& value) { country_ = value; }
        void setUserGroups(const std::vector<std::string> &value) { user_groups_ = value; }
        void setRoles(const std::vector<std::string> &value) { roles_ = value; }


    private:

        std::string first_name_;
        std::string last_name_;
        std::string username_;
        std::string email_;
        std::string password_;
        std::string phone_number_;
        std::string country_;
        std::vector<std::string> user_groups_;
        std::vector<std::string> roles_;
    };

    inline void AdminUserDto::fromJson(const Json::Value& json) {

        if (json.isMember("firstName") && !json["firstName"].isNull()) {
            first_name_ = json["firstName"].asString();
        }

        if (json.isMember("lastName") && !json["lastName"].isNull()) {
            last_name_ = json["lastName"].asString();
        }

        if (json.isMember("username") && !json["username"].isNull()) {
            username_ = json["username"].asString();
        }

        if (json.isMember("email") && !json["email"].isNull()) {
            email_ = json["email"].asString();
        }

        if (json.isMember("password") && !json["password"].isNull()) {
            password_ = json["password"].asString();
        }

        if (json.isMember("phoneNumber") && !json["phoneNumber"].isNull()) {
            phone_number_ = json["phoneNumber"].asString();
        }


        if (json.isMember("userGroups") && json["userGroups"].isArray()) {
            for (const auto &ip : json["userGroups"]) {
                user_groups_.push_back(ip.asString());
            }
        }

        if (json.isMember("roles") && json["roles"].isArray()) {
            for (const auto &ip : json["roles"]) {
                roles_.push_back(ip.asString());
            }
        }


    }

}


#endif //CREATEUSERDTO_H
