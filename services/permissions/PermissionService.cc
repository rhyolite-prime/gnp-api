//
// Created by Emmanuel Addo-Odame on 28/09/2026.
//

#include "PermissionService.h"
#include "Users.h"
#include "Roles.h"
#include "UserGroups.h"
#include <drogon/orm/CoroMapper.h>

namespace gnp::services {


    drogon::Task<std::unordered_set<std::string>> PermissionService::getUserPermissions(const std::string& userId) {
    std::unordered_set<std::string> allPermissions;
    auto dbClient = drogon::app().getDbClient();

    try {
        drogon::orm::CoroMapper<drogon_model::Gnp::Users> userMapper(dbClient);
        auto user = co_await userMapper.findByPrimaryKey(userId);

        std::unordered_set<std::string> uniqueRoleIds;
        Json::Reader reader;

        // Extract direct roles
        Json::Value userRolesJson;
        std::string userRolesStr = user.getValueOfRoles();
        if (!userRolesStr.empty() && reader.parse(userRolesStr, userRolesJson) && userRolesJson.isArray()) {
            for (const auto& roleId : userRolesJson) {
                if (roleId.isString()) {
                    uniqueRoleIds.insert(roleId.asString());
                }
            }
        }

        // Extract roles from user groups
        Json::Value userGroupsJson;
        std::string userGroupsStr = user.getValueOfUserGroups();
        if (!userGroupsStr.empty() && reader.parse(userGroupsStr, userGroupsJson) && userGroupsJson.isArray()) {
            drogon::orm::CoroMapper<drogon_model::Gnp::UserGroups> userGroupMapper(dbClient);
            for (const auto& groupId : userGroupsJson) {
                if (groupId.isString()) {
                    try {
                        auto group = co_await userGroupMapper.findByPrimaryKey(groupId.asString());
                        Json::Value groupRolesJson;
                        std::string groupRolesStr = group.getValueOfRoles();
                        if (!groupRolesStr.empty() && reader.parse(groupRolesStr, groupRolesJson) && groupRolesJson.isArray()) {
                            for (const auto& roleId : groupRolesJson) {
                                if (roleId.isString()) {
                                    uniqueRoleIds.insert(roleId.asString());
                                }
                            }
                        }
                    } catch (const drogon::orm::DrogonDbException &) {
                        // ignore missing user groups
                    }
                }
            }
        }

        // Extract permissions from roles
        if (!uniqueRoleIds.empty()) {
            drogon::orm::CoroMapper<drogon_model::Gnp::Roles> roleMapper(dbClient);
            for (const auto& roleId : uniqueRoleIds) {
                try {
                    auto role = co_await roleMapper.findByPrimaryKey(roleId);
                    Json::Value rolePermissionsJson;
                    std::string rolePermissionsStr = role.getValueOfPermissions();
                    if (!rolePermissionsStr.empty() && reader.parse(rolePermissionsStr, rolePermissionsJson) && rolePermissionsJson.isArray()) {
                        for (const auto& perm : rolePermissionsJson) {
                            if (perm.isString()) {
                                allPermissions.insert(perm.asString());
                            }
                        }
                    }
                } catch (const drogon::orm::DrogonDbException &) {
                    // ignore missing roles
                }
            }
        }
    } catch (const drogon::orm::DrogonDbException &e) {
        LOG_ERROR << "Failed to get user permissions: " << e.base().what();
    }

    co_return allPermissions;
}

    bool PermissionService::hasPermission(const drogon::HttpRequestPtr &req, const std::string &requiredPermission) {
        if (req->attributes()->find("permissions")) {
            auto permissions = req->attributes()->get<std::unordered_set<std::string>>("permissions");
            return permissions.find(requiredPermission) != permissions.end();
        }
        return false;
    }

    Json::Value PermissionService::getAllPermissionGroups() {
        auto &app = drogon::app();
        auto customConfig = app.getCustomConfig();
        if (customConfig.isMember("PermissionGroups")) {
            return customConfig["PermissionGroups"];
        }
        return Json::arrayValue;
    }

}