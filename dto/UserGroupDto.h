//
// Created by Emmanuel Addo-Odame on 08/10/2026.
//

#ifndef GNPAPI_USERGROUPDTO_H
#define GNPAPI_USERGROUPDTO_H
#include <json/json.h>
#include <string>
#include <vector>

namespace gnp::dto {

    struct RoleItem {
        std::string id;
        std::string name;
    };

    class UserGroupDto {

    public:

        UserGroupDto() = default;

        void fromJson(const Json::Value& json);

        // Getters
        [[nodiscard]] const std::string& getTitle() const { return title_; }
        [[nodiscard]] const std::string& getDescription() const { return description_; }
        [[nodiscard]] const std::string& getParentId() const { return parent_id_; }
        [[nodiscard]] const std::vector<RoleItem> &getRoles() const { return roles_; }
        [[nodiscard]] bool hasParentId() const { return has_parent_id_; }
        [[nodiscard]] bool hasRoles() const { return has_roles_; }

        // Setters
        void setTitle(const std::string& title) { title_ = title; }
        void setDescription(const std::string& description) { description_ = description; }
        void setParentId(const std::string& parent_id) { parent_id_ = parent_id; }
        void setRoles(const std::vector<RoleItem> &value) { roles_ = value; }

    private:

        std::string title_;
        std::string description_;
        std::string parent_id_;
        std::vector<RoleItem> roles_;
        bool has_parent_id_ = false;
        bool has_roles_ = false;

    };


    inline void UserGroupDto::fromJson(const Json::Value& json) {

        if (json.isMember("title") && !json["title"].isNull()) {
            title_ = json["title"].asString();
        }

        if (json.isMember("description") && !json["description"].isNull()) {
            description_ = json["description"].asString();
        }

        if (json.isMember("parentId")) {
            has_parent_id_ = true;
            if (!json["parentId"].isNull()) {
                parent_id_ = json["parentId"].asString();
            } else {
                parent_id_ = "";
            }
        }

        if (json.isMember("roles")) {
            has_roles_ = true;
            if (json["roles"].isArray()) {
                roles_.clear();
                for (const auto &item : json["roles"]) {
                    RoleItem role;
                    if (item.isMember("id") && !item["id"].isNull()) {
                        role.id = item["id"].asString();
                    }
                    if (item.isMember("name") && !item["name"].isNull()) {
                        role.name = item["name"].asString();
                    }
                    roles_.push_back(role);
                }
            }
        }

    };

}

#endif //GNPAPI_USERGROUPDTO_H
