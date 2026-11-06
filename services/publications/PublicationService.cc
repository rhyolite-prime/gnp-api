//
// Created by Emmanuel Addo-Odame on 03/09/2025.
//
#include "PublicationService.h"
#include "Publications.h"
#include "constants/ErrorCodes.h"
#include "constants/StatusTypes.h"
#include <drogon/drogon.h>
#include <drogon/orm/CoroMapper.h>
#include <drogon/orm/Criteria.h>
#include <drogon/orm/Mapper.h>
#include <json/json.h>
#include <string>

#include <drogon/orm/Exception.h>
#include "constants/PublicationTypes.h"

using namespace drogon::orm;
using namespace gnp::dto;

namespace gnp::services {

drogon::Task<gnp::dto::BaseApiResponse> PublicationService::getAllPublications(int pageNo, int pageSize, const std::string &query) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<drogon_model::Gnp::Publications> mp(dbClient);

  // 1. Build the search criteria
  Criteria searchCriteria = Criteria(drogon_model::Gnp::Publications::Cols::_type, CompareOperator::EQ, static_cast<int>(constants::News));

  if (!query.empty()) {
    std::string likeQuery = "%" + query + "%";
    searchCriteria = Criteria(drogon_model::Gnp::Publications::Cols::_name, CompareOperator::Like, likeQuery) ||
        Criteria(drogon_model::Gnp::Publications::Cols::_description, CompareOperator::Like, likeQuery);
  }

  try {
    // 2. Asynchronously get the total count matching the criteria
    size_t totalCount = co_await mp.count(searchCriteria);

    if (totalCount == 0) {
      gnp::dto::BaseApiResponse response;
      response.success = true;
      response.result["data"] = Json::arrayValue;
      response.result["totalCount"] = 0;
      co_return response;
    }

    // 3. Asynchronously find the paginated data
    int offset = (pageNo - 1) * pageSize;
    auto publications =  co_await mp.limit(pageSize).offset(offset).findBy(searchCriteria);

    gnp::dto::BaseApiResponse response;
    response.success = true;
    response.result["totalCount"] = (Json::UInt64)totalCount;
    response.result["pageNo"] = pageNo;
    response.result["pageSize"] = pageSize;
    response.result["totalPages"] =  (int)((totalCount + pageSize - 1) / pageSize);

    Json::Value data = Json::arrayValue;
    for (const auto &publication : publications) {
      Json::Value publicationJson = publication.toJson();

      // Convert snake_case to camelCase
      Json::Value camelCasePublication;
      camelCasePublication["id"] = publicationJson["id"];
      camelCasePublication["name"] = publicationJson["name"];
      camelCasePublication["isActive"] = publicationJson["is_active"];
      camelCasePublication["description"] = publicationJson["description"];
      camelCasePublication["price"] = publicationJson["price"];
      camelCasePublication["sortOrder"] = publicationJson["sort_order"];

      data.append(camelCasePublication);
    }
    response.result["data"] = data;
    co_return response;
  } catch (const DrogonDbException &e) {
    gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] = "Database error while fetching publications.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
} // namespace gnp::services

drogon::Task<BaseApiResponse> PublicationService::create(const gnp::dto::PublicationDto &dto) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<drogon_model::Gnp::Publications> mp(dbClient);

  drogon_model::Gnp::Publications newPublication;

  newPublication.setName(dto.getName());
  newPublication.setDescription(dto.getDescription());
  newPublication.setPrice(dto.getPrice());
  newPublication.setIsActive(dto.getActive());

  try {
    auto publication = co_await mp.insert(newPublication);

    // 5. Prepare success response
    dto::BaseApiResponse successResponse;
    successResponse.success = true;
    successResponse.message = "Publication created successfully";
    successResponse.result["id"] = publication.getValueOfId();

    co_return successResponse;
  } catch (const drogon::orm::DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Database error while creating Publication";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    co_return errorResponse;
  }
}

drogon::Task<gnp::dto::BaseApiResponse> PublicationService::update(const dto::PublicationDto &dto, const std::string &publicationId) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<drogon_model::Gnp::Publications> mp(dbClient);

  Criteria criteria = Criteria(drogon_model::Gnp::Publications::Cols::_id, CompareOperator::EQ, publicationId);

  try {
    auto publication = co_await mp.findOne(criteria);

    if (!dto.getName().empty())
      publication.setName(dto.getName());
    if (!dto.getDescription().empty())
      publication.setDescription(dto.getDescription());
    if (!dto.getPrice().empty())
      publication.setPrice(dto.getPrice());

    publication.setIsActive(dto.getActive());

    try {
      co_await mp.update(publication);

      dto::BaseApiResponse response;
      response.success = true;
      response.message = "Publication updated successfully";
      co_return response;
    } catch (const DrogonDbException &e) {
      dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "Failed to update publication";
      errorResponse.error["code"] = constants::ERR_DB_QUERY;
      errorResponse.error["detail"] = e.base().what();
      co_return errorResponse;
    }
  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Publication not found";
    errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<gnp::dto::BaseApiResponse> PublicationService::deletePublication(const std::string &publicationId) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<drogon_model::Gnp::Publications> mp(dbClient);

  Criteria criteria = Criteria(drogon_model::Gnp::Publications::Cols::_id, CompareOperator::EQ, publicationId);

  try {
    // First verify the user exists
    auto publication = co_await mp.findOne(criteria);

    try {
      // User found, proceed with deletion
      size_t count = co_await mp.deleteBy(criteria);

      if (count > 0) {
        // Successfully deleted
        dto::BaseApiResponse response;
        response.success = true;
        response.message = "Publication deleted successfully";
        co_return response;
      } else {
        // No rows were deleted (shouldn't happen if we found the user)
        dto::BaseApiResponse errorResponse;
        errorResponse.success = false;
        errorResponse.message = "Failed to delete publication";
        errorResponse.error["code"] = constants::ERR_DB_QUERY;
        co_return errorResponse;
      }
    } catch (const DrogonDbException &e) {
      // Error during deletion
      dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "Failed to delete publication";
      errorResponse.error["code"] = constants::ERR_DB_QUERY;
      errorResponse.error["detail"] = e.base().what();
      co_return errorResponse;
    }
  } catch (const DrogonDbException &e) {
    // User not found
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Publication not found";
    errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<gnp::dto::BaseApiResponse> PublicationService::activate(const std::string &publicationId) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<drogon_model::Gnp::Publications> mp(dbClient);

  // Create criteria to find the user with specified ID in the tenant
  Criteria criteria = Criteria(drogon_model::Gnp::Publications::Cols::_id,
                               CompareOperator::EQ, publicationId);

  try {
    // Find the user first
    auto publication = co_await mp.findOne(criteria);

    if (publication.getValueOfIsActive()) {
      // Tenant is already inactive / active check
      dto::BaseApiResponse response;
      response.success = true;
      response.message = "Publication is already active.";
      co_return response;
    }

    // Set the user as active
    publication.setIsActive(true);

    try {
      // Update the user in the database
      co_await mp.update(publication);

      // Successfully updated
      dto::BaseApiResponse response;
      response.success = true;
      response.message = "Publication activated successfully";
      co_return response;
    } catch (const DrogonDbException &e) {
      // Error during update
      dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "Failed to activate publication";
      errorResponse.error["code"] = constants::ERR_DB_QUERY;
      errorResponse.error["detail"] = e.base().what();
      co_return errorResponse;
    }
  } catch (const DrogonDbException &e) {
    // User not found
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Publication not found";
    errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<gnp::dto::BaseApiResponse> PublicationService::deactivate(const std::string &publicationId) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<drogon_model::Gnp::Publications> mp(dbClient);

  // Create criteria to find the user with specified ID in the tenant
  Criteria criteria = Criteria(drogon_model::Gnp::Publications::Cols::_id, CompareOperator::EQ, publicationId);

  try {
    // Find the user first
    auto publication = co_await mp.findOne(criteria);

    // Publication found, check if it's already inactive
    if (!publication.getValueOfIsActive()) {
      // Tenant is already inactive
      dto::BaseApiResponse response;
      response.success = true;
      response.message = "Publication is already inactive.";
      co_return response;
    }

    // Set the user as inactive
    publication.setIsActive(false);

    try {
      // Update the user in the database
      co_await mp.update(publication);

      // Successfully updated
      dto::BaseApiResponse response;
      response.success = true;
      response.message = "Publication deactivated successfully";
      co_return response;
    } catch (const DrogonDbException &e) {
      // Error during update
      dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "Failed to deactivate publication";
      errorResponse.error["code"] = constants::ERR_DB_QUERY;
      errorResponse.error["detail"] = e.base().what();
      co_return errorResponse;
    }
  } catch (const DrogonDbException &e) {
    // User not found
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Publication not found";
    errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}


} // namespace gnp::services
