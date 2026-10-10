//
// Created by Emmanuel Addo-Odame on 28/09/2026.
//

#ifndef GNPAPI_PERMISSIONSERVICE_H
#define GNPAPI_PERMISSIONSERVICE_H

#include <drogon/drogon.h>
#include <drogon/utils/coroutine.h>
#include <unordered_set>
#include <string>
#include <json/json.h>

namespace gnp::services {

    class PermissionService {
    public:
        // Fetches the set of permissions for a given user from the database
        static drogon::Task<std::unordered_set<std::string>> getUserPermissions(const std::string& userId);

        // Checks if the current request (and its associated user) has the required permission
        static bool hasPermission(const drogon::HttpRequestPtr &req, const std::string &requiredPermission);

        // Returns all PermissionGroups from config.json
        static Json::Value getAllPermissionGroups();
    };

}
#endif //GNPAPI_PERMISSIONSERVICE_H
