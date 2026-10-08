//
// Created by Emmanuel Addo-Odame on 27/04/2026.
//
#include "RoleService.h"
#include "Roles.h"
#include "Users.h"

using namespace drogon::orm;
using drogon_model::Gnp::Roles;

namespace gnp::services {


drogon::Task<dto::BaseApiResponse> RoleService::getAll(int pageNo, int pageSize, const std::string &query) {
  auto dbClient = drogon::app().getDbClient();
  auto mp = CoroMapper<Roles>(dbClient);

  // 1. Build the search criteria
  Criteria searchCriteria = Criteria(Roles::Cols::_partner_id, CompareOperator::IsNull);
  if (!query.empty()) {
    std::string likeQuery = "%" + query + "%";
    searchCriteria = searchCriteria && Criteria(Roles::Cols::_name, CompareOperator::Like, likeQuery) || Criteria(Roles::Cols::_description, CompareOperator::Like, likeQuery);
  }

  dto::BaseApiResponse response;

  try {
    // 2. Get the total count matching the criteria
    size_t totalCount = co_await mp.count(searchCriteria);
    if (totalCount == 0) {
      response.success = true;
      response.result["data"] = Json::arrayValue;
      response.result["totalCount"] = 0;
      response.result["totalPages"] = 0;
      response.result["pageNo"] = pageNo;
      response.result["pageSize"] = pageSize;
      co_return response;
    }

    // 3. Find the paginated data
    int offset = (pageNo - 1) * pageSize;
    auto users = co_await mp.limit(pageSize).offset(offset).findBy(searchCriteria);

    auto totalPages = (totalCount + pageSize - 1) / pageSize;

    // 4. Build the final response
    response.success = true;
    response.result["totalCount"] = (Json::UInt64)totalCount;
    response.result["pageNo"] = pageNo;
    response.result["pageSize"] = pageSize;
    response.result["totalPages"] = (int)((totalCount + pageSize - 1) / pageSize);
    response.result["lowerBound"] = pageSize * (pageNo - 1) + 1;
    response.result["upperBound"] = (int)totalPages == pageNo ? (Json::UInt64)totalCount  : (Json::UInt64)(pageNo * pageSize);

    Json::Value data = Json::arrayValue;

    for (const auto &role : users) {
      Json::Value roleJson = role.toJson();
      Json::Value camelCaseRole;
      camelCaseRole["id"] = roleJson["id"];
      camelCaseRole["name"] = roleJson["name"];
      camelCaseRole["description"] = roleJson["description"];
      //camelCaseRole["partnerId"] = roleJson["partner_id"];

      // Parse permissions JSON string into array
      Json::Value permissionsArray = Json::arrayValue;
      if (!roleJson["permissions"].isNull() && !roleJson["permissions"].asString().empty()) {
        Json::CharReaderBuilder builder;
        std::string errs;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        std::string permStr = roleJson["permissions"].asString();
        reader->parse(permStr.c_str(), permStr.c_str() + permStr.size(), &permissionsArray, &errs);
      }
      camelCaseRole["permissions"] = permissionsArray;

      camelCaseRole["createdAt"] = roleJson["created_at"];
      camelCaseRole["updatedAt"] = roleJson["updated_at"];
      data.append(camelCaseRole);
    }
    response.result["data"] = data;

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.error["message"] = "Database error while fetching roles.";
    response.error["detail"] = e.base().what();
  }
  co_return response;
}


drogon::Task<dto::BaseApiResponse> RoleService::getAllPermissions() {
  dto::BaseApiResponse response;
  try {
    auto customConfig = drogon::app().getCustomConfig();
    if (customConfig.isMember("AdminPermissions")) {
      response.success = true;
      response.result = customConfig["AdminPermissions"];
    } else {
      response.success = false;
      response.error["message"] = "AdminPermissions not found in configuration.";
    }
  } catch (const std::exception &e) {
    response.success = false;
    response.error["message"] = "Error retrieving permissions.";
    response.error["detail"] = e.what();
  }
  co_return response;
}

drogon::Task<dto::BaseApiResponse> RoleService::create(const dto::RoleDto &roleDto) {
  auto dbClient = drogon::app().getDbClient();
  auto mp = CoroMapper<Roles>(dbClient);

  dto::BaseApiResponse response;
  try {
    Roles role;
    role.setName(roleDto.getName());
    role.setDescription(roleDto.getDescription());
    if (!roleDto.getPartnerId().empty()) {
      role.setPartnerId(roleDto.getPartnerId());
    }

    // Convert permissions vector to JSON string
    Json::Value permissionsJson = Json::arrayValue;
    for (const auto &perm : roleDto.getPermissions()) {
      permissionsJson.append(perm);
    }
    Json::StreamWriterBuilder writer;
    role.setPermissions(Json::writeString(writer, permissionsJson));

    auto result = co_await mp.insert(role);
    response.success = true;
    response.result = result.toJson();

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.error["message"] = "Database error while creating role.";
    response.error["detail"] = e.base().what();
  }
  co_return response;
}


drogon::Task<dto::BaseApiResponse> RoleService::update(const dto::RoleDto &roleDto, const std::string &roleId) {
  auto dbClient = drogon::app().getDbClient();
  auto mp = CoroMapper<Roles>(dbClient);

  dto::BaseApiResponse response;
  try {
    auto role = co_await mp.findByPrimaryKey(roleId);

    role.setName(roleDto.getName());
    role.setDescription(roleDto.getDescription());
    if (!roleDto.getPartnerId().empty()) {
      role.setPartnerId(roleDto.getPartnerId());
    }

    // Convert permissions vector to JSON string
    Json::Value permissionsJson = Json::arrayValue;
    for (const auto &perm : roleDto.getPermissions()) {
      permissionsJson.append(perm);
    }
    Json::StreamWriterBuilder writer;
    role.setPermissions(Json::writeString(writer, permissionsJson));

    co_await mp.update(role);
    response.success = true;
    response.result = role.toJson();

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.error["message"] = "Database error while updating role.";
    response.error["detail"] = e.base().what();
  }
  co_return response;
}


drogon::Task<dto::BaseApiResponse> RoleService::deleteRole(const std::string &roleId) {

  auto dbClient = drogon::app().getDbClient();
  auto mp = CoroMapper<Roles>(dbClient);

  dto::BaseApiResponse response;
  try {
    co_await mp.deleteBy(Criteria(Roles::Cols::_id, CompareOperator::EQ, roleId));
    response.success = true;
    response.result["message"] = "Role deleted successfully";

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.error["message"] = "Database error while deleting role.";
    response.error["detail"] = e.base().what();
  }
  co_return response;
}

//partner roles

drogon::Task<dto::BaseApiResponse> RoleService::getAll(int pageNo, int pageSize, const std::string &partnerId, const std::string &query) {

  auto dbClient = drogon::app().getDbClient();
  auto mp = CoroMapper<Roles>(dbClient);

  // 1. Build the search criteria
  Criteria searchCriteria = Criteria(Roles::Cols::_partner_id, CompareOperator::EQ, partnerId);
  if (!query.empty()) {
    std::string likeQuery = "%" + query + "%";
    searchCriteria =  searchCriteria && Criteria(Roles::Cols::_name, CompareOperator::Like, likeQuery) || Criteria(Roles::Cols::_description, CompareOperator::Like, likeQuery);
  }

  dto::BaseApiResponse response;
  try {
    // 2. Get the total count matching the criteria
    size_t totalCount = co_await mp.count(searchCriteria);
    if (totalCount == 0) {
      response.success = true;
      response.result["data"] = Json::arrayValue;
      response.result["totalCount"] = 0;
      co_return response;
    }

    // 3. Find the paginated data
    int offset = (pageNo - 1) * pageSize;
    auto users = co_await mp.limit(pageSize).offset(offset).findBy(searchCriteria);

    // 4. Build the final response
    response.success = true;
    response.result["totalCount"] = (Json::UInt64)totalCount;
    response.result["pageNo"] = pageNo;
    response.result["pageSize"] = pageSize;
    response.result["totalPages"] = (int)((totalCount + pageSize - 1) / pageSize);

    Json::Value data = Json::arrayValue;

    for (const auto &role : users) {
      Json::Value roleJson = role.toJson();
      Json::Value camelCaseRole;
      camelCaseRole["id"] = roleJson["id"];
      camelCaseRole["name"] = roleJson["name"];
      camelCaseRole["description"] = roleJson["description"];
      camelCaseRole["partnerId"] = roleJson["partner_id"];

      // Parse permissions JSON string into array
      Json::Value permissionsArray = Json::arrayValue;
      if (!roleJson["permissions"].isNull() && !roleJson["permissions"].asString().empty()) {
        Json::CharReaderBuilder builder;
        std::string errs;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        std::string permStr = roleJson["permissions"].asString();
        reader->parse(permStr.c_str(), permStr.c_str() + permStr.size(), &permissionsArray, &errs);
      }
      camelCaseRole["permissions"] = permissionsArray;

      camelCaseRole["createdAt"] = roleJson["created_at"];
      camelCaseRole["updatedAt"] = roleJson["updated_at"];
      data.append(camelCaseRole);
    }
    response.result["data"] = data;

  } catch (const DrogonDbException &e) {
    response.success = false;
    response.error["message"] = "Database error while fetching roles.";
    response.error["detail"] = e.base().what();
  }
  co_return response;
}



}