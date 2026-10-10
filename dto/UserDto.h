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

    class SubscriberDto {

    public:

        SubscriberDto() = default;

        void fromJson(const Json::Value& json);

        // Getters
        [[nodiscard]] const std::string& getFirstName() const { return first_name_; }
        [[nodiscard]] const std::string& getLastName() const { return last_name_; }
        [[nodiscard]] const std::string& getEmail() const { return email_; }
        [[nodiscard]] const std::string& getUsername() const { return username_; }
        [[nodiscard]] const std::string& getPhoneNumber() const { return phone_number_; }
        [[nodiscard]] const std::string& getCountry() const { return country_; }
        [[nodiscard]] const std::string& getBillingCycle() const { return billingCycle_; }
        [[nodiscard]] const std::string& getSubscriptionBillAmount() const { return subscriptionBillAmount_; }
        [[nodiscard]] const std::string& getSubscriptionStartDate() const { return subscriptionStartDate_; }
        [[nodiscard]] const std::string& getSubscriptionEndDate() const { return subscriptionEndDate_; }
        [[nodiscard]] const std::string& getSubscriptionPlanId() const { return subscriptionPlanId_; }
        [[nodiscard]] const std::string& getSubscriptionPlanName() const { return subscriptionPlanName_; }
        [[nodiscard]] bool getByPassPayment() const { return byPassPayment_; }

        // Setters
        void setFirstName(const std::string& value) { first_name_ = value; }
        void setLastName(const std::string& value) { last_name_ = value; }
        void setEmail(const std::string& value) { email_ = value; }
        void setUsername(const std::string& value) { username_ = value; }
        void setPhoneNumber(const std::string& value) { phone_number_ = value; }
        void setCountry(const std::string& value) { country_ = value; }
        void setBillingCycle(const std::string& value) { billingCycle_ = value; }
        void setSubscriptionBillAmount(const std::string& value) { subscriptionBillAmount_ = value; }
        void setSubscriptionStartDate(const std::string& value) { subscriptionStartDate_ = value; }
        void setSubscriptionEndDate(const std::string& value) { subscriptionEndDate_ = value; }
        void setSubscriptionPlanId(const std::string& value) { subscriptionPlanId_ = value; }
        void setSubscriptionPlanName(const std::string& value) { subscriptionPlanName_ = value; }
        void setByPassPayment(const bool value) { byPassPayment_ = value; }


    private:

        std::string first_name_;
        std::string last_name_;
        std::string username_;
        std::string email_;
        std::string phone_number_;
        std::string country_ = "GH";
        std::string billingCycle_;
        std::string subscriptionBillAmount_;
        std::string subscriptionStartDate_;
        std::string subscriptionEndDate_;
        std::string subscriptionPlanId_;
        std::string subscriptionPlanName_;
        bool byPassPayment_ {false};
    };

    inline void SubscriberDto::fromJson(const Json::Value& json) {

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

        if (json.isMember("billingCycle") && !json["billingCycle"].isNull()) {
            billingCycle_ = json["billingCycle"].asString();
        }

        if (json.isMember("phoneNumber") && !json["phoneNumber"].isNull()) {
            phone_number_ = json["phoneNumber"].asString();
        }

        if (json.isMember("price") && !json["price"].isNull()) {
            subscriptionBillAmount_ = json["price"].asString();
        }

        if (json.isMember("subscriptionStartDate") && !json["subscriptionStartDate"].isNull()) {
            subscriptionStartDate_ = json["subscriptionStartDate"].asString();
        }

        if (json.isMember("subscriptionEndDate") && !json["subscriptionEndDate"].isNull()) {
            subscriptionEndDate_ = json["subscriptionEndDate"].asString();
        }

        if (json.isMember("subscriptionPlanId") && !json["subscriptionPlanId"].isNull()) {
            subscriptionPlanId_ = json["subscriptionPlanId"].asString();
        }

        if (json.isMember("subscriptionPlanName") && !json["subscriptionPlanName"].isNull()) {
            subscriptionPlanName_ = json["subscriptionPlanName"].asString();
        }

        if (json.isMember("byPassPayment") && !json["byPassPayment"].isNull()) {
           setByPassPayment(json["byPassPayment"].asBool());
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

    class UserStatusDto {

    public:

        UserStatusDto() = default;

        void fromJson(const Json::Value& json);

        // Getters
        [[nodiscard]] const std::string& getSubscriberId() const { return subscriberId_; }
        [[nodiscard]] const std::string& getStatus() const { return status_; }


        // Setters
        void setSubscriberId(const std::string& value) { subscriberId_ = value; }
        void setStatus(const std::string& value) { status_ = value; }


    private:

        std::string subscriberId_;
        std::string status_;

    };

    inline void UserStatusDto::fromJson(const Json::Value& json) {

        if (json.isMember("subscriberId") && !json["subscriberId"].isNull()) {
            subscriberId_ = json["subscriberId"].asString();
        }

        if (json.isMember("status") && !json["status"].isNull()) {
            status_ = json["status"].asString();
        }



    }


    class AssignSubscriberPlanDto {

    public:

        AssignSubscriberPlanDto() = default;

        void fromJson(const Json::Value& json);

        // Getters
        [[nodiscard]] const std::string& getSubscriberId() const { return subscriberId_; }
        [[nodiscard]] const std::string& getPlanId() const { return planId_; }
        [[nodiscard]] const std::string& getBillingCycle() const { return billingCycle_; }
        [[nodiscard]] const std::string& getPlanName() const { return planName_; }
        [[nodiscard]] const std::string& getPrice() const { return price_; }
        [[nodiscard]] const std::string& getAction() const { return action_; }


        // Setters
        void setSubscriberId(const std::string& value) { subscriberId_ = value; }
        void setPlanId(const std::string& value) { planId_ = value; }
        void setBillingCycle(const std::string& value) { billingCycle_ = value; }
        void setPlanName(const std::string& value) { planName_ = value; }
        void setPrice(const std::string& value) { price_ = value; }
        void setAction(const std::string& value) { action_ = value; }


    private:

        std::string subscriberId_;
        std::string planId_;
        std::string billingCycle_;
        std::string planName_;
        std::string price_;
        std::string action_; // 'replace', 'extend', '' -> if empty it means its the first time a subscription is being assigned

    };

    inline void AssignSubscriberPlanDto::fromJson(const Json::Value& json) {

        if (json.isMember("subscriberId") && !json["subscriberId"].isNull()) {
            subscriberId_ = json["subscriberId"].asString();
        }

        if (json.isMember("planId") && !json["planId"].isNull()) {
            setPlanId(json["planId"].asString());
        }

        if (json.isMember("billingCycle") && !json["billingCycle"].isNull()) {
            setBillingCycle(json["billingCycle"].asString());
        }

        if (json.isMember("planName") && !json["planName"].isNull()) {
            setPlanName(json["planName"].asString());
        }

        if (json.isMember("price") && !json["price"].isNull()) {
            setPrice(json["price"].asString());
        }

        if (json.isMember("action") && !json["action"].isNull()) {
            setAction(json["action"].asString());
        }



    }

}


#endif //CREATEUSERDTO_H
