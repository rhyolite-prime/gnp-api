//
// Created by Emmanuel Addo-Odame on 03/09/2025.
//

#include "NewspaperService.h"
#include "Newspapers.h"
#include "constants/ErrorCodes.h"
#include <drogon/orm/Mapper.h>
#include <jwt-cpp/jwt.h>
#include "NewspaperDetails.h"
#include "UserNotificationSubscriptions.h"
#include "UserSubscriptions.h"
#include "Users.h"
#include "constants/NotificationTypes.h"
#include "utils/IdGeneratorUtils.h"
#include "plugins/GnpServicePlugin.h"
#include <drogon/drogon.h>
#include <fstream>
#include <vector>

#include "CommercialPartners.h"
#include "NewspaperEngagement.h"
#include "constants/StatusTypes.h"

using namespace drogon::orm;
using drogon_model::Gnp::Newspapers;

namespace {

  bool isUsableThumbnailUrl(const std::string &url) {
    if (url.empty()) return false;
    if (url == "null" || url == "undefined") return false;

    // Must be an absolute http/https URL.
    constexpr std::string_view http  = "http://";
    constexpr std::string_view https = "https://";
    if (url.compare(0, http.size(),  http)  != 0 &&
        url.compare(0, https.size(), https) != 0) {
      return false;
        }

    // Must have at least one character after the scheme://
    const auto schemeLen = (url.rfind(https, 0) == 0) ? https.size() : http.size();
    if (url.size() <= schemeLen) return false;

    return true;
  }

  constexpr std::string_view kDefaultNewsThumbnail =
      "https://archive.graphic.com.gh/img/news-avatar.png";

}

namespace gnp::services {

// reduced information for public a
drogon::Task<gnp::dto::BaseApiResponse> NewspaperService::getAllAsync(
    int pageNo, int pageSize, const std::string &publicationId,
    const std::string &startDate, const std::string &endDate,
    const std::string &query) {
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Newspapers> mp(dbClient);

  // 1. Build the search criteria
  Criteria searchCriteria = Criteria(Newspapers::Cols::_is_published, CompareOperator::EQ, true) && Criteria(Newspapers::Cols::_is_archived, CompareOperator::EQ, false);

  // text search
  if (!query.empty()) {
    std::string likeQuery = "%" + query + "%";
    searchCriteria =
        searchCriteria &&
        (Criteria(Newspapers::Cols::_title, CompareOperator::Like, likeQuery) ||
         Criteria(Newspapers::Cols::_full_description, CompareOperator::Like,
                  likeQuery));
  }

  // filter by publicationId
  if (!publicationId.empty()) {
    searchCriteria =
        searchCriteria && Criteria(Newspapers::Cols::_publication_id,
                                   CompareOperator::EQ, publicationId);
  }

  // filter by startDate (created_at >= startDate)
  if (!startDate.empty()) {
    searchCriteria =
        searchCriteria && Criteria(Newspapers::Cols::_published_date,
                                   CompareOperator::GE, startDate);
  }

  // filter by endDate (created_at <= endDate)
  if (!endDate.empty()) {
    searchCriteria =
        searchCriteria && Criteria(Newspapers::Cols::_published_date,
                                   CompareOperator::LE, endDate);
  }

  try {
    size_t totalCount = co_await mp.count(searchCriteria);

    if (totalCount == 0) {
      dto::BaseApiResponse response;
      response.success = true;
      response.result["data"] = Json::arrayValue;
      response.result["totalCount"] = 0;
      co_return response;
    }

    int offset = (pageNo - 1) * pageSize;
    auto publications =
        co_await mp.limit(pageSize)
            .offset(offset)
            .orderBy(Newspapers::Cols::_publication_date, SortOrder::DESC)
            .findBy(searchCriteria);

    dto::BaseApiResponse response;
    auto totalPages = (totalCount + pageSize - 1) / pageSize;

    response.success = true;
    response.result["totalCount"] = (Json::UInt64)totalCount;
    response.result["pageNo"] = pageNo;
    response.result["pageSize"] = pageSize;
    response.result["lowerBound"] = pageSize * (pageNo - 1) + 1;
    response.result["upperBound"] = Json::Value(
        (int)totalPages == pageNo ? (Json::UInt64)totalCount
                                  : (Json::UInt64)(pageNo * pageSize));
    response.result["totalPages"] = (int)totalPages;

    Json::Value data = Json::arrayValue;
    for (const auto &newspaper : publications) {
      Json::Value newsPaperJson = newspaper.toJson();

      // Convert snake_case to camelCase
      Json::Value camelCaseRole;
      camelCaseRole["id"] = newsPaperJson["id"];
      camelCaseRole["title"] = newsPaperJson["title"];
      camelCaseRole["slug"] = newsPaperJson["slug"];
      camelCaseRole["price"] = newsPaperJson["price"];
      camelCaseRole["editionNumber"] = newsPaperJson["edition_number"];
      camelCaseRole["shortDescription"] = newsPaperJson["short_description"];
      camelCaseRole["fullDescription"] = newsPaperJson["full_description"];
      camelCaseRole["thumbnailId"] = newsPaperJson["thumbnail_id"];
      camelCaseRole["fileType"] = newsPaperJson["file_type"];
      camelCaseRole["isFree"] = newsPaperJson["is_free"];
      camelCaseRole["publicationDate"] = newsPaperJson["publication_date"];

      std::string featuredStoriesStr = newspaper.getValueOfFeaturedStories();
      Json::Value featuredStoriesJson;
      Json::Reader reader;

      if (!featuredStoriesStr.empty() &&
          reader.parse(featuredStoriesStr, featuredStoriesJson)) {
        camelCaseRole["featuredStories"] = featuredStoriesJson;
      } else {
        camelCaseRole["featuredStories"] = Json::arrayValue;
      }

      data.append(camelCaseRole);
    }

    response.result["data"] = data;
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] =
        "Database error while fetching newspapers.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}



drogon::Task<gnp::dto::BaseApiResponse> NewspaperService::getLatestNewsPapers(int pageNo, int pageSize) {
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Newspapers> mp(dbClient);

  // 1. Build the search criteria: Only published and non archived newspapers
  Criteria searchCriteria = Criteria(Newspapers::Cols::_is_published, CompareOperator::EQ, true) && Criteria(Newspapers::Cols::_is_archived, CompareOperator::EQ, false);

  try {
    size_t totalCount = co_await mp.count(searchCriteria);

    if (totalCount == 0) {
      dto::BaseApiResponse response;
      response.success = true;
      response.result["data"] = Json::arrayValue;
      response.result["totalCount"] = 0;
      co_return response;
    }

    int offset = (pageNo - 1) * pageSize;
    auto publications =
        co_await mp.limit(pageSize)
            .offset(offset)
            .orderBy(Newspapers::Cols::_publication_date, SortOrder::DESC)
            .findBy(searchCriteria);

    dto::BaseApiResponse response;
    auto totalPages = (totalCount + pageSize - 1) / pageSize;

    response.success = true;
    response.result["totalCount"] = (Json::UInt64)totalCount;
    response.result["pageNo"] = pageNo;
    response.result["pageSize"] = pageSize;
    response.result["lowerBound"] = pageSize * (pageNo - 1) + 1;
    response.result["upperBound"] = Json::Value(
        (int)totalPages == pageNo ? (Json::UInt64)totalCount
                                  : (Json::UInt64)(pageNo * pageSize));
    response.result["totalPages"] = (int)totalPages;

    Json::Value data = Json::arrayValue;
    for (const auto &newspaper : publications) {
      Json::Value newsPaperJson = newspaper.toJson();

      // Convert snake_case to camelCase
      Json::Value camelCaseRole;
      camelCaseRole["id"] = newsPaperJson["id"];
      camelCaseRole["title"] = newsPaperJson["title"];
      camelCaseRole["slug"] = newsPaperJson["slug"];
      camelCaseRole["price"] = newsPaperJson["price"];
      camelCaseRole["editionNumber"] = newsPaperJson["edition_number"];
      camelCaseRole["thumbnailId"] = newsPaperJson["thumbnail_id"];
      camelCaseRole["fileType"] = newsPaperJson["file_type"];
      camelCaseRole["isFree"] = newsPaperJson["is_free"];
      camelCaseRole["publicationDate"] = newsPaperJson["publication_date"];

      data.append(camelCaseRole);
    }

    response.result["data"] = data;
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] =  "Database error while fetching latest newspapers.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}



drogon::Task<gnp::dto::BaseApiResponse> NewspaperService::getRecentNewspapersForAffiliate(int pageNo, int pageSize, const std::string &affiliateId) {
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Newspapers> mp(dbClient);

  //check if partner id exists
  //track number of times the affiliate link was called

  // 1. Build the search criteria: Only published and non archived newspapers
  Criteria searchCriteria = Criteria(Newspapers::Cols::_is_published, CompareOperator::EQ, true) && Criteria(Newspapers::Cols::_is_archived, CompareOperator::EQ, false);

  try {
    size_t totalCount = co_await mp.count(searchCriteria);

    if (totalCount == 0) {
      dto::BaseApiResponse response;
      response.success = true;
      response.result["data"] = Json::arrayValue;
      response.result["totalCount"] = 0;
      co_return response;
    }

    int offset = (pageNo - 1) * pageSize;
    auto publications = co_await mp.limit(pageSize)
            .offset(offset)
            .orderBy(Newspapers::Cols::_publication_date, SortOrder::DESC)
            .findBy(searchCriteria);

    dto::BaseApiResponse response;
    auto totalPages = (totalCount + pageSize - 1) / pageSize;

    response.success = true;
    response.result["totalCount"] = (Json::UInt64)totalCount;
    response.result["pageNo"] = pageNo;
    response.result["pageSize"] = pageSize;
    response.result["lowerBound"] = pageSize * (pageNo - 1) + 1;
    response.result["upperBound"] = Json::Value(
        (int)totalPages == pageNo ? (Json::UInt64)totalCount
                                  : (Json::UInt64)(pageNo * pageSize));
    response.result["totalPages"] = (int)totalPages;

    Json::Value data = Json::arrayValue;
    for (const auto &newspaper : publications) {
      Json::Value newsPaperJson = newspaper.toJson();

      // Convert snake_case to camelCase
      Json::Value camelCaseRole;
      camelCaseRole["id"] = newsPaperJson["id"];
      camelCaseRole["title"] = newsPaperJson["title"];
      camelCaseRole["slug"] = newsPaperJson["slug"];
      camelCaseRole["price"] = newsPaperJson["price"];
      camelCaseRole["editionNumber"] = newsPaperJson["edition_number"];
      camelCaseRole["thumbnailId"] = newsPaperJson["thumbnail_id"];
      camelCaseRole["fileType"] = newsPaperJson["file_type"];
      camelCaseRole["isFree"] = newsPaperJson["is_free"];
      camelCaseRole["publicationDate"] = newsPaperJson["publication_date"];

      data.append(camelCaseRole);
    }

    response.result["data"] = data;
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] =  "Database error while fetching latest newspapers.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}



drogon::Task<gnp::dto::BaseApiResponse> NewspaperService::getRedactedDetailsAsync(const std::string &id) {
  auto dbClient = drogon::app().getDbClient();
  auto mp = drogon::orm::CoroMapper<drogon_model::Gnp::Newspapers>(dbClient);

  try {
    // Only published newspapers are visible here
    Criteria criteria =
        Criteria(Newspapers::Cols::_id, CompareOperator::EQ, id) &&
        Criteria(Newspapers::Cols::_is_published, CompareOperator::EQ, true);

    auto newspaper = co_await mp.findOne(criteria);

    dto::BaseApiResponse response;
    response.success = true;

    Json::Value src = newspaper.toJson();
    Json::Value data;

    // Basic fields
    data["id"] = src["id"];
    data["title"] = src["title"];
    data["slug"] = src["slug"];
    data["price"] = src["price"];
    data["editionNumber"] = src["edition_number"];
    data["shortDescription"] = src["short_description"];
    data["fullDescription"] = src["full_description"];
    data["thumbnailId"] = src["thumbnail_id"];
    data["fileType"] = src["file_type"];

    data["isFree"] = src["is_free"];
    data["isPopular"] = src["is_popular"];
    data["publishedDate"] = src["published_date"];

    // Category / publication info
    data["categoryId"] = src["category_id"];
    data["categoryName"] = src["category_name"];
    data["publicationId"] = src["publication_id"];
    data["publicationName"] = src["publication_name"];

    // Copyright
    data["copyrightOwner"] = src["copyright_owner"];

    // Featured stories (stored as JSON string)
    std::string featuredStoriesStr = newspaper.getValueOfFeaturedStories();
    Json::Value featuredStoriesJson;
    Json::Reader reader;
    if (!featuredStoriesStr.empty() &&
        reader.parse(featuredStoriesStr, featuredStoriesJson)) {
      data["featuredStories"] = featuredStoriesJson;
    } else {
      data["featuredStories"] = Json::arrayValue;
    }

    response.result = data;
    co_return response;
  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    errorResponse.error["message"] = "Newspaper not found.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<gnp::dto::BaseApiResponse> NewspaperService::getDetails(const std::string &id) {
  auto dbClient = drogon::app().getDbClient();
  auto mp = drogon::orm::CoroMapper<Newspapers>(dbClient);

  try {

    auto newspaper = co_await mp.findByPrimaryKey(id);

    dto::BaseApiResponse response;
    response.success = true;

    Json::Value src = newspaper.toJson();
    Json::Value data;

    // Basic fields
    data["id"] = src["id"];
    data["title"] = src["title"];
    data["slug"] = src["slug"];
    data["price"] = src["price"];
    data["editionNumber"] = src["edition_number"];
    data["shortDescription"] = src["short_description"];
    data["fullDescription"] = src["full_description"];
    data["thumbnailId"] = src["thumbnail_id"];
    data["fileType"] = src["file_type"];
    data["storageService"] = src["storage_service"];
    data["documentId"] = src["document_id"];
    data["isFree"] = src["is_free"];
    data["isPopular"] = src["is_popular"];
    data["publishedDate"] = src["published_date"];
    data["publicationDate"] = src["publication_date"];
    data["isPublished"] = src["is_published"];
    data["isArchived"] = src["is_archived"];
    data["sales"] = src["sales"];
    data["views"] = src["views"];
    data["fileType"] = src["file_type"];

    // Category / publication info
    data["publicationId"] = src["publication_id"];
    data["publicationName"] = src["publication_name"];

    // Copyright
    data["copyrightOwner"] = src["copyright_owner"];

    // Featured stories (stored as JSON string)
    std::string featuredStoriesStr = newspaper.getValueOfFeaturedStories();
    Json::Value featuredStoriesJson;
    Json::Reader reader;
    if (!featuredStoriesStr.empty() &&
        reader.parse(featuredStoriesStr, featuredStoriesJson)) {
      data["featuredStories"] = featuredStoriesJson;
    } else {
      data["featuredStories"] = Json::arrayValue;
    }


    std::string tagsStr = newspaper.getValueOfTags();
    Json::Value tagsJson;
    if (!tagsStr.empty() &&  reader.parse(tagsStr, tagsJson)) {
      data["tags"] = tagsJson;
    } else {
          data["tags"] = Json::arrayValue;
    }

    std::string categoriesStr = newspaper.getValueOfCategories();
    Json::Value categoriesJson;
    if (!tagsStr.empty() &&  reader.parse(categoriesStr, categoriesJson)) {
      data["categories"] = categoriesJson;
    } else {
      data["categories"] = Json::arrayValue;
    }

    response.result = data;
    co_return response;
  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    errorResponse.error["message"] = "Newspaper not found.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<gnp::dto::BaseApiResponse> NewspaperService::getFreeNewsPaperDetailsByPublicationAsync(
    const std::string &publicationId, const std::string &date) {
  auto dbClient = drogon::app().getDbClient();
  auto mp = drogon::orm::CoroMapper<drogon_model::Gnp::Newspapers>(dbClient);

  try {
    // Only published and free newspapers are visible here
    Criteria criteria =
        Criteria(Newspapers::Cols::_is_published, CompareOperator::EQ, true) &&
        Criteria(Newspapers::Cols::_is_free, CompareOperator::EQ, true);

    if (!publicationId.empty()) {
      criteria = criteria && Criteria(Newspapers::Cols::_publication_id,
                                      CompareOperator::EQ, publicationId);
    }

    drogon_model::Gnp::Newspapers newspaper;

    // If date is provided, use exact match
    if (!date.empty()) {
      criteria = criteria && Criteria(Newspapers::Cols::_publication_date,
                                      CompareOperator::EQ, date);
      newspaper = co_await mp.findOne(criteria);
    } else {
      // If no date, get the latest one
      auto newspapers =
          co_await mp
              .orderBy(Newspapers::Cols::_publication_date, SortOrder::DESC)
              .limit(1)
              .findBy(criteria);

      if (newspapers.empty()) {
        dto::BaseApiResponse errorResponse;
        errorResponse.success = false;
        errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
        errorResponse.error["message"] = "Newspaper not found.";
        co_return errorResponse;
      }
      newspaper = newspapers[0];
    }

    dto::BaseApiResponse response;
    response.success = true;

    Json::Value src = newspaper.toJson();
    Json::Value data;

    // Basic fields
    data["id"] = src["id"];
    data["title"] = src["title"];
    data["slug"] = src["slug"];
    data["price"] = src["price"];
    data["editionNumber"] = src["edition_number"];
    data["shortDescription"] = src["short_description"];
    data["fullDescription"] = src["full_description"];
    data["thumbnailId"] = src["thumbnail_id"];
    data["fileType"] = src["file_type"];
    data["storageType"] = src["storage_type"];
    data["documentId"] = src["document_id"];
    data["isFree"] = src["is_free"];
    data["isPopular"] = src["is_popular"];
    data["publishedDate"] = src["published_date"];
    data["isPublished"] = src["is_published"];

    // Category / publication info
    data["categoryId"] = src["category_id"];
    data["categoryName"] = src["category_name"];
    data["publicationId"] = src["publication_id"];
    data["publicationName"] = src["publication_name"];

    // Copyright
    data["copyrightOwner"] = src["copyright_owner"];

    // Featured stories (stored as JSON string)
    std::string featuredStoriesStr = newspaper.getValueOfFeaturedStories();
    Json::Value featuredStoriesJson;
    Json::Reader reader;
    if (!featuredStoriesStr.empty() &&
        reader.parse(featuredStoriesStr, featuredStoriesJson)) {
      data["featuredStories"] = featuredStoriesJson;
    } else {
      data["featuredStories"] = Json::arrayValue;
    }

    response.result = data;
    co_return response;
  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    errorResponse.error["message"] = "Newspaper not found.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}


 drogon::Task<gnp::dto::BaseApiResponse> NewspaperService::getPaidNewsPaperDetailsByPublicationAsync(
    const std::string &publicationId, const std::string &date) {
  auto dbClient = drogon::app().getDbClient();
  auto mp = drogon::orm::CoroMapper<drogon_model::Gnp::Newspapers>(dbClient);

  try {
    // Only published and free newspapers are visible here
    Criteria criteria =
        Criteria(Newspapers::Cols::_is_published, CompareOperator::EQ, true) &&
        Criteria(Newspapers::Cols::_is_free, CompareOperator::EQ, false);

    if (!publicationId.empty()) {
      criteria = criteria && Criteria(Newspapers::Cols::_publication_id,
                                      CompareOperator::EQ, publicationId);
    }

    drogon_model::Gnp::Newspapers newspaper;

    // If date is provided, use exact match
    if (!date.empty()) {
      criteria = criteria && Criteria(Newspapers::Cols::_publication_date,
                                      CompareOperator::EQ, date);
      newspaper = co_await mp.findOne(criteria);
    } else {
      // If no date, get the latest one
      auto newspapers =
          co_await mp
              .orderBy(Newspapers::Cols::_publication_date, SortOrder::DESC)
              .limit(1)
              .findBy(criteria);

      if (newspapers.empty()) {
        dto::BaseApiResponse errorResponse;
        errorResponse.success = false;
        errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
        errorResponse.error["message"] = "Newspaper not found.";
        co_return errorResponse;
      }
      newspaper = newspapers[0];
    }

    dto::BaseApiResponse response;
    response.success = true;

    Json::Value src = newspaper.toJson();
    Json::Value data;

    // Basic fields
    data["id"] = src["id"];
    data["title"] = src["title"];
    data["slug"] = src["slug"];
    data["price"] = src["price"];
    data["editionNumber"] = src["edition_number"];
    data["shortDescription"] = src["short_description"];
    data["fullDescription"] = src["full_description"];
    data["thumbnailId"] = src["thumbnail_id"];
    data["fileType"] = src["file_type"];
    data["storageType"] = src["storage_type"];
    data["documentId"] = src["document_id"];
    data["isFree"] = src["is_free"];
    data["isPopular"] = src["is_popular"];
    data["publishedDate"] = src["published_date"];
    data["isPublished"] = src["is_published"];

    // Category / publication info
    data["categoryId"] = src["category_id"];
    data["categoryName"] = src["category_name"];
    data["publicationId"] = src["publication_id"];
    data["publicationName"] = src["publication_name"];

    // Copyright
    data["copyrightOwner"] = src["copyright_owner"];

    // Featured stories (stored as JSON string)
    std::string featuredStoriesStr = newspaper.getValueOfFeaturedStories();
    Json::Value featuredStoriesJson;
    Json::Reader reader;
    if (!featuredStoriesStr.empty() &&
        reader.parse(featuredStoriesStr, featuredStoriesJson)) {
      data["featuredStories"] = featuredStoriesJson;
    } else {
      data["featuredStories"] = Json::arrayValue;
    }

    response.result = data;
    co_return response;
  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    errorResponse.error["message"] = "Newspaper not found.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

// for admin use only
drogon::Task<dto::BaseApiResponse> NewspaperService::listAllAsync(int pageNo, int pageSize, const std::string &publicationId,
    const std::string &startDate, const std::string &endDate,
    const std::string &query, const std::string &status) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Newspapers> mp(dbClient);

  // 1. Build the search criteria
  Criteria searchCriteria;

  // text search
  if (!query.empty()) {
    std::string likeQuery = "%" + query + "%";

    searchCriteria =
        Criteria(Newspapers::Cols::_title, CompareOperator::Like, likeQuery) ||
        Criteria(Newspapers::Cols::_full_description, CompareOperator::Like, likeQuery) ||
        Criteria(Newspapers::Cols::_slug, CompareOperator::Like, likeQuery);


  }


  // filter by publicationId
  if (!publicationId.empty()) {
    searchCriteria =
        searchCriteria && Criteria(Newspapers::Cols::_publication_id,
                                   CompareOperator::EQ, publicationId);
  }

  // filter by startDate (created_at >= startDate)
  if (!startDate.empty()) {
    searchCriteria =
        searchCriteria && Criteria(Newspapers::Cols::_publication_date,
                                   CompareOperator::GE, startDate);
  }

  // filter by endDate (created_at <= endDate)
  if (!endDate.empty()) {
    searchCriteria =
        searchCriteria && Criteria(Newspapers::Cols::_publication_date,
                                   CompareOperator::LE, endDate);
  }

  // status
  if (!status.empty()) {
    bool isPublished = (status == "published");
    searchCriteria = searchCriteria &&
        Criteria(Newspapers::Cols::_is_published, CompareOperator::EQ, isPublished);
  }

  try {
    size_t totalCount = co_await mp.count(searchCriteria);

    if (totalCount == 0) {
      dto::BaseApiResponse response;
      response.success = true;
      response.result["data"] = Json::arrayValue;
      response.result["totalCount"] = 0;
      co_return response;
    }

    int offset = (pageNo - 1) * pageSize;
    auto publications =
        co_await mp.limit(pageSize)
            .offset(offset)
            .orderBy(Newspapers::Cols::_publication_date, SortOrder::DESC)
            .findBy(searchCriteria);

    dto::BaseApiResponse response;
    response.success = true;
    response.result["totalCount"] = (Json::UInt64)totalCount;
    response.result["pageNo"] = pageNo;
    response.result["pageSize"] = pageSize;
    response.result["totalPages"] =
        (int)((totalCount + pageSize - 1) / pageSize);

    Json::Value data = Json::arrayValue;
    for (const auto &role : publications) {
      Json::Value roleJson = role.toJson();

      // Convert snake_case to camelCase
      Json::Value camelCaseRole;
      camelCaseRole["id"] = roleJson["id"];
      camelCaseRole["title"] = roleJson["title"];
      camelCaseRole["slug"] = roleJson["slug"];
      camelCaseRole["price"] = roleJson["price"];
      camelCaseRole["editionNumber"] = roleJson["edition_number"];
      camelCaseRole["views"] = roleJson["views"];
      camelCaseRole["sales"] = roleJson["sales"];
      camelCaseRole["fullDescription"] = roleJson["full_description"];
      camelCaseRole["thumbnailId"] = roleJson["thumbnail_id"];
      camelCaseRole["documentId"] = roleJson["document_id"];
      camelCaseRole["isPublished"] = roleJson["is_published"];
      camelCaseRole["publicationId"] = roleJson["publication_id"];
      camelCaseRole["publicationName"] = roleJson["publication_name"];
      camelCaseRole["publicationDate"] = roleJson["publication_date"];
      camelCaseRole["isFree"] = roleJson["is_free"];
      camelCaseRole["createdAt"] = roleJson["created_at"];
      camelCaseRole["updatedAt"] = roleJson["updated_at"];

      data.append(camelCaseRole);
    }

    response.result["data"] = data;
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] =
        "Database error while fetching newspapers.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}



drogon::Task<dto::BaseApiResponse> NewspaperService::listAllArchivedAsync(int pageNo, int pageSize, const std::string &publicationId,
    const std::string &startDate, const std::string &endDate,
    const std::string &query, const std::string &status) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Newspapers> mp(dbClient);

  // 1. Build the search criteria
  Criteria searchCriteria = Criteria(Newspapers::Cols::_is_archived, CompareOperator::EQ, true);

  // text search
  if (!query.empty()) {
    std::string likeQuery = "%" + query + "%";

    searchCriteria = searchCriteria &&
        Criteria(Newspapers::Cols::_title, CompareOperator::Like, likeQuery) ||
        Criteria(Newspapers::Cols::_full_description, CompareOperator::Like, likeQuery) ||
        Criteria(Newspapers::Cols::_slug, CompareOperator::Like, likeQuery);


  }


  // filter by publicationId
  if (!publicationId.empty()) {
    searchCriteria =
        searchCriteria && Criteria(Newspapers::Cols::_publication_id,
                                   CompareOperator::EQ, publicationId);
  }

  // filter by startDate (created_at >= startDate)
  if (!startDate.empty()) {
    searchCriteria =
        searchCriteria && Criteria(Newspapers::Cols::_publication_date,
                                   CompareOperator::GE, startDate);
  }

  // filter by endDate (created_at <= endDate)
  if (!endDate.empty()) {
    searchCriteria =
        searchCriteria && Criteria(Newspapers::Cols::_publication_date,
                                   CompareOperator::LE, endDate);
  }

  // status
  if (!status.empty()) {
    bool isPublished = (status == "published");
    searchCriteria = searchCriteria &&
        Criteria(Newspapers::Cols::_is_published, CompareOperator::EQ, isPublished);
  }

  try {
    size_t totalCount = co_await mp.count(searchCriteria);

    if (totalCount == 0) {
      dto::BaseApiResponse response;
      response.success = true;
      response.result["data"] = Json::arrayValue;
      response.result["totalCount"] = 0;
      co_return response;
    }

    int offset = (pageNo - 1) * pageSize;
    auto publications =
        co_await mp.limit(pageSize)
            .offset(offset)
            .orderBy(Newspapers::Cols::_publication_date, SortOrder::DESC)
            .findBy(searchCriteria);

    dto::BaseApiResponse response;
    response.success = true;
    response.result["totalCount"] = (Json::UInt64)totalCount;
    response.result["pageNo"] = pageNo;
    response.result["pageSize"] = pageSize;
    response.result["totalPages"] =
        (int)((totalCount + pageSize - 1) / pageSize);

    Json::Value data = Json::arrayValue;
    for (const auto &role : publications) {
      Json::Value roleJson = role.toJson();

      // Convert snake_case to camelCase
      Json::Value camelCaseRole;
      camelCaseRole["id"] = roleJson["id"];
      camelCaseRole["title"] = roleJson["title"];
      camelCaseRole["slug"] = roleJson["slug"];
      camelCaseRole["price"] = roleJson["price"];
      camelCaseRole["editionNumber"] = roleJson["edition_number"];
      camelCaseRole["views"] = roleJson["views"];
      camelCaseRole["sales"] = roleJson["sales"];
      camelCaseRole["fullDescription"] = roleJson["full_description"];
      camelCaseRole["thumbnailId"] = roleJson["thumbnail_id"];
      camelCaseRole["documentId"] = roleJson["document_id"];
      camelCaseRole["isPublished"] = roleJson["is_published"];
      camelCaseRole["publicationId"] = roleJson["publication_id"];
      camelCaseRole["publicationName"] = roleJson["publication_name"];
      camelCaseRole["publicationDate"] = roleJson["publication_date"];
      camelCaseRole["isFree"] = roleJson["is_free"];
      camelCaseRole["createdAt"] = roleJson["created_at"];
      camelCaseRole["updatedAt"] = roleJson["updated_at"];

      data.append(camelCaseRole);
    }

    response.result["data"] = data;
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] =
        "Database error while fetching newspapers.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}


drogon::Task<gnp::dto::BaseApiResponse> NewspaperService::ingestAsync(const dto::IngestNewsPaperDto &dto) {
  auto dbClient = drogon::app().getDbClient();
  drogon::orm::CoroMapper<drogon_model::Gnp::Newspapers> mp(dbClient);

  drogon_model::Gnp::Newspapers newspaper;

  // Required fields
  newspaper.setTitle(dto.getTitle());
  newspaper.setSlug(dto.getSlug());
  newspaper.setPrice(std::to_string(dto.getPrice()));
  newspaper.setIsFree(dto.isFree());
  newspaper.setPublicationId(dto.getPublicationId());
  newspaper.setPublicationName(dto.getPublicationName());
  newspaper.setPublicationDate(dto.getPublicationDate());
  newspaper.setPublishedDateToNull();

  // Optional fields
  newspaper.setCopyrightOwner("Graphic Communications Group Limited");
  newspaper.setEditionNumber(dto.getEditionNumber());
  newspaper.setIsPopular(dto.getIsPopular());
  newspaper.setFullDescription(dto.getFullDescription());
  newspaper.setThumbnailId(dto.getThumbnailId());
  newspaper.setFileType(constants::FileTypes::PDF);
  newspaper.setStorageService(dto.getStorageService());
  newspaper.setDocumentId(dto.getDocumentId());
  newspaper.setIsPublished(false);
  newspaper.setCreatedAt(trantor::Date::now());
  newspaper.setFeaturedStories(dto.getFeaturedStories());

  try {
    auto insertedNewspaper = co_await mp.insert(newspaper);

    dto::BaseApiResponse successResponse;
    successResponse.success = true;
    successResponse.message = "Newspaper created successfully";
    successResponse.result["id"] = insertedNewspaper.getValueOfId();

    // Grant newspaper entitlements for the publication date in the background
    const std::string publicationDate = dto.getPublicationDate().toDbStringLocal();
    drogon::async_run([publicationDate]() -> drogon::Task<void> {
      try {
        auto plugin = drogon::app().getPlugin<plugins::GnpServicePlugin>();
        auto &newspaperService = plugin->getNewsPaperService();

        co_await newspaperService.regenerateNewspaperEntitlement(publicationDate);
      } catch (const std::exception &e) {
        LOG_ERROR << "Background entitlement grant failed for date " << publicationDate
                  << ": " << e.what();
      }
    });

    co_return successResponse;
  } catch (const drogon::orm::DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Database error while creating Newspaper";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}


drogon::Task<::gnp::dto::BaseApiResponse> NewspaperService::getNewspaperEngagementReport(const gnp::dto::ReportDto &dto) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<drogon_model::Gnp::CommercialPartners> partnerMapper(dbClient);

  try {


    auto partner = co_await partnerMapper.findByPrimaryKey(dto.getPartnerId());

    // Parse dates
    trantor::Date startDateObj = trantor::Date::fromDbStringLocal(dto.getStartDate() + " 00:00:00");
    trantor::Date endDateObj = trantor::Date::fromDbStringLocal(dto.getEndDate() + " 23:59:59");

    // Get partner's subscribers count
    CoroMapper<drogon_model::Gnp::Users> userMapper(dbClient);
    auto totalSubscribers = co_await userMapper.count(
        Criteria(drogon_model::Gnp::Users::Cols::_partner_id, CompareOperator::EQ, dto.getPartnerId()) &&
        Criteria(drogon_model::Gnp::Users::Cols::_is_active, CompareOperator::EQ, true));

    // Query newspaper engagement and sales data
    std::string sql =
        "SELECT "
        "  n.id as newspaper_id, "
        "  n.title as newspaper_title, "
        "  n.publication_date, "
        "  n.price, "
        "  n.views as total_views, "
        "  n.sales as total_sales, "
        "  COALESCE(n.sales / NULLIF(n.price, 0), 0) as copies_sold "
        "FROM newspapers n "
        "WHERE n.publication_date >= $1 "
        "  AND n.publication_date <= $2 "
        "  AND n.is_archived = false "
        "  AND n.is_published = true "
        "ORDER BY n.publication_date DESC, n.views DESC";

    auto result = co_await dbClient->execSqlCoro(
        sql, startDateObj.toDbStringLocal(), endDateObj.toDbStringLocal());

    // Build Response
    gnp::dto::BaseApiResponse response;
    response.success = true;
    response.message = "Newspaper engagement report retrieved successfully";

    Json::Value reportData;

    // Report Metadata
    reportData["partnerName"] = partner.getValueOfName();
    reportData["partnerId"] = dto.getPartnerId();
    reportData["reportPeriod"]["startDate"] = dto.getStartDate();
    reportData["reportPeriod"]["endDate"] = dto.getEndDate();
    reportData["reportPeriod"]["totalDays"] = (Json::Int64)((endDateObj.microSecondsSinceEpoch() - startDateObj.microSecondsSinceEpoch()) / (1000000LL * 3600 * 24)) + 1;
    reportData["totalSubscribers"] = (Json::UInt64)totalSubscribers;
    reportData["generatedAt"] = trantor::Date::now().toDbStringLocal();

    Json::Value items = Json::arrayValue;
    Json::Value summary;
    summary["totalNewspapers"] = 0;
    summary["totalViews"] = 0;
    summary["totalSales"] = 0.0;
    summary["totalCopiesSold"] = 0;
    summary["averagePrice"] = 0.0;

    double totalSales = 0.0;
    long totalCopies = 0;
    long totalViews = 0;
    double totalPriceSum = 0.0;

    for (const auto &row : result) {
      Json::Value item;

      std::string newspaperId = row["newspaper_id"].as<std::string>();
      std::string title = row["newspaper_title"].as<std::string>();
      std::string pubDate = row["publication_date"].as<std::string>();

      double price = row["price"].isNull() ? 0.0 : row["price"].as<double>();
      long views = row["total_views"].isNull() ? 0 : row["total_views"].as<long>();
      double sales = row["total_sales"].isNull() ? 0.0 : row["total_sales"].as<double>();
      long copiesSold = row["copies_sold"].isNull() ? 0 : row["copies_sold"].as<long>();

      item["newspaperId"] = newspaperId;
      item["title"] = title;
      item["publicationDate"] = pubDate;
      item["price"] = price;
      item["views"] = (Json::UInt64)views;
      item["sales"] = sales;
      item["copiesSold"] = (Json::UInt64)copiesSold;

      // Calculate engagement metrics
      double viewsPerSubscriber = 0.0;
      if (totalSubscribers > 0) {
        viewsPerSubscriber = (double)views / totalSubscribers;
      }
      item["viewsPerSubscriber"] = std::round(viewsPerSubscriber * 100) / 100.0;

      // Revenue per view
      double revenuePerView = 0.0;
      if (views > 0 && sales > 0) {
        revenuePerView = sales / views;
      }
      item["revenuePerView"] = std::round(revenuePerView * 100) / 100.0;

      items.append(item);

      // Update summary
      totalViews += views;
      totalSales += sales;
      totalCopies += copiesSold;
      totalPriceSum += price;
      summary["totalNewspapers"] = summary["totalNewspapers"].asInt() + 1;
    }

    summary["totalViews"] = (Json::UInt64)totalViews;
    summary["totalSales"] = totalSales;
    summary["totalCopiesSold"] = (Json::UInt64)totalCopies;

    if (result.size() > 0) {
      summary["averagePrice"] = std::round((totalPriceSum / result.size()) * 100) / 100.0;
    }

    // Calculate overall engagement rate
    double engagementRate = 0.0;
    if (totalSubscribers > 0 && totalViews > 0) {
      engagementRate = ((double)totalViews / (totalSubscribers * result.size())) * 100.0;
    }
    summary["engagementRate"] = std::round(engagementRate * 10) / 10.0;

    // Find best performing newspaper
    if (result.size() > 0) {
      Json::Value bestPerforming;
      std::string bestTitle;
      long bestViews = 0;
      double bestSales = 0.0;

      for (const auto &row : result) {
        long views = row["total_views"].isNull() ? 0 : row["total_views"].as<long>();
        double sales = row["total_sales"].isNull() ? 0.0 : row["total_sales"].as<double>();

        if (views > bestViews) {
          bestViews = views;
          bestTitle = row["newspaper_title"].as<std::string>();
          bestSales = sales;
        }
      }

      bestPerforming["title"] = bestTitle;
      bestPerforming["views"] = (Json::UInt64)bestViews;
      bestPerforming["sales"] = bestSales;
      summary["bestPerformingNewspaper"] = bestPerforming;
    }

    reportData["items"] = items;
    reportData["summary"] = summary;

    // Add insights
    Json::Value insights;
    if (totalSales > 0 && totalViews > 0) {
      insights["revenuePerView"] = std::round((totalSales / totalViews) * 100) / 100.0;
    } else {
      insights["revenuePerView"] = 0.0;
    }

    if (totalCopies > 0 && totalSales > 0) {
      insights["averageRevenuePerCopy"] = std::round((totalSales / totalCopies) * 100) / 100.0;
    } else {
      insights["averageRevenuePerCopy"] = 0.0;
    }

    reportData["insights"] = insights;

    response.result = reportData;
    co_return response;

  } catch (const DrogonDbException &e) {
    gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to retrieve newspaper engagement report";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}



drogon::Task<gnp::dto::BaseApiResponse> NewspaperService::update(const dto::NewsPaperDto &dto, const std::string &id) {
  auto dbClient = drogon::app().getDbClient();
  drogon::orm::CoroMapper<drogon_model::Gnp::Newspapers> mp(dbClient);

  try {
    auto newspaper = co_await mp.findByPrimaryKey(id);

    if (!dto.getTitle().empty()) newspaper.setTitle(dto.getTitle());
    if (!dto.getSlug().empty()) newspaper.setSlug(dto.getSlug());
    if (dto.getPrice() >= 0) newspaper.setPrice(std::to_string(dto.getPrice()));
    newspaper.setIsFree(dto.isFree());
    newspaper.setIsPublished(dto.isPublished());
    if (!dto.getPublicationId().empty()) newspaper.setPublicationId(dto.getPublicationId());
    if (!dto.getPublicationName().empty()) newspaper.setPublicationName(dto.getPublicationName());
    if (!dto.getEditionNumber().empty()) newspaper.setEditionNumber(dto.getEditionNumber());
    newspaper.setIsPopular(dto.getIsPopular());
    if (!dto.getFullDescription().empty()) newspaper.setFullDescription(dto.getFullDescription());
    if (!dto.getThumbnailId().empty()) newspaper.setThumbnailId(dto.getThumbnailId());
    newspaper.setFileType(dto.getFileType());
    if (!dto.getStorageService().empty()) newspaper.setStorageService(dto.getStorageService());
    if (!dto.getDocumentId().empty()) newspaper.setDocumentId(dto.getDocumentId());
    if (!dto.getFeaturedStories().empty()) newspaper.setFeaturedStories(dto.getFeaturedStories());
    
    if (dto.getPublicationDate().microSecondsSinceEpoch() > 0) {
      newspaper.setPublicationDate(dto.getPublicationDate());
    }

    co_await mp.update(newspaper);

    dto::BaseApiResponse successResponse;
    successResponse.success = true;
    successResponse.message = "Newspaper updated successfully";
    successResponse.result["id"] = newspaper.getValueOfId();

    co_return successResponse;
  } catch (const drogon::orm::DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Database error while updating Newspaper";
    if (e.base().what() == std::string("Record Not Found")) {
      errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
      errorResponse.error["message"] = "Newspaper not found.";
    } else {
      errorResponse.error["code"] = constants::ERR_DB_QUERY;
    }
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}


drogon::Task<gnp::dto::BaseApiResponse> NewspaperService::publishAsync(const std::string &id) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Newspapers> mp(dbClient);

  try {
    auto newspaper = co_await mp.findByPrimaryKey(id);

    if (newspaper.getValueOfIsPublished()) {
      dto::BaseApiResponse response;
      response.success = true;
      response.message = "Newspaper is already published.";
      co_return response;
    }

    newspaper.setIsPublished(true);
    newspaper.setPublishedDate(trantor::Date::now());

    co_await mp.update(newspaper);

    dto::BaseApiResponse response;
    response.success = true;
    response.message = "Newspaper published successfully.";

    // send daily news update reminder based in system settings
    // better still it can be managed by plugins to prevent tight coupling. 

    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse response;
    response.success = false;
    if (e.base().what() == std::string("Record Not Found")) {
      response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
      response.error["message"] = "Newspaper not found.";
    } else {
      response.error["code"] = constants::ERR_DB_QUERY;
      response.error["message"] = "Error publishing newspaper.";
    }
    response.error["detail"] = e.base().what();
    co_return response;
  }
}


drogon::Task<gnp::dto::BaseApiResponse> NewspaperService::unPublishAsync(const std::string &id) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<Newspapers> mp(dbClient);

  try {

    auto newspaper = co_await mp.findByPrimaryKey(id);

    if (!newspaper.getValueOfIsPublished()) {
      dto::BaseApiResponse response;
      response.success = true;
      response.message = "Newspaper is already unpublished.";
      co_return response;
    }

    newspaper.setIsPublished(false);
    co_await mp.update(newspaper);

    dto::BaseApiResponse response;
    response.success = true;
    response.message = "Newspaper unpublished successfully.";
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse response;
    response.success = false;

    if (e.base().what() == std::string("Record Not Found")) {
      response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
      response.error["message"] = "Newspaper not found.";
    } else {
      response.error["code"] = constants::ERR_DB_QUERY;
      response.error["message"] = "Error unpublishing newspaper.";
    }
    response.error["detail"] = e.base().what();
    co_return response;
  }
}

drogon::Task<gnp::dto::BaseApiResponse> NewspaperService::deleteNewspaperAsync(const std::string &id) {
  auto dbClient = drogon::app().getDbClient();
  auto mp = drogon::orm::CoroMapper<drogon_model::Gnp::Newspapers>(dbClient);

  try {
    // Create criteria to find the newspaper with specified ID
    Criteria criteria = Criteria(drogon_model::Gnp::Newspapers::Cols::_id,
                                 CompareOperator::EQ, id);

    // First verify the newspaper exists
    co_await mp.findOne(criteria);

    // Newspaper found, proceed with deletion
    size_t count = co_await mp.deleteBy(criteria);

    if (count > 0) {
      // Successfully deleted
      dto::BaseApiResponse response;
      response.success = true;
      response.message = "Newspaper deleted successfully";
      co_return response;
    } else {
      // No rows were deleted
      dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "Failed to delete newspaper";
      errorResponse.error["code"] = constants::ERR_DB_QUERY;
      co_return errorResponse;
    }
  } catch (const DrogonDbException &e) {
    // Newspaper not found or database error
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Newspaper not found or database error";
    errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}


drogon::Task<gnp::dto::BaseApiResponse> NewspaperService::regenerateNewspaperEntitlement(const std::string &startDate) {
  auto dbClient = drogon::app().getDbClient();
  drogon::orm::CoroMapper<drogon_model::Gnp::Newspapers> newsMapper(dbClient);
  drogon::orm::CoroMapper<drogon_model::Gnp::UserSubscriptions> subMapper(dbClient);

  try {
    // 1. Get all newspapers from startDate onwards
    Criteria newsCriteria(Newspapers::Cols::_publication_date, CompareOperator::GE, startDate);
    auto newspapers = co_await newsMapper.findBy(newsCriteria);

    if (newspapers.empty()) {
      dto::BaseApiResponse response;
      response.success = true;
      response.message = "No newspapers found from the given start date.";
      co_return response;
    }

    // 2. Get user subscriptions whose start date matches the given start date
    drogon::orm::Criteria subCriteria(drogon_model::Gnp::UserSubscriptions::Cols::_start_date, drogon::orm::CompareOperator::EQ, startDate);
    auto subscriptions = co_await subMapper.findBy(subCriteria);

    int updatedCount = 0;

    // 3. For each user, update their entitlements
    for (auto &userSub : subscriptions) {
      std::string currentEntitlementsStr = userSub.getValueOfNewspaperEntitlements();
      Json::Value entitlements;

      if (!currentEntitlementsStr.empty()) {
        Json::CharReaderBuilder readerBuilder;
        std::string errs;
        std::istringstream s(currentEntitlementsStr);
        if (!Json::parseFromStream(readerBuilder, s, &entitlements, &errs)) {
          entitlements = Json::arrayValue;
        }
      } else {
        entitlements = Json::arrayValue;
      }

      bool changed = false;

      // Check against all fetched newspapers
      for (const auto &newspaper : newspapers) {
        std::string newPaperId = newspaper.getValueOfId();
        bool alreadyExists = false;

        for (const auto &ent : entitlements) {
          if (ent.isObject() && ent.isMember("id") && ent["id"].isString() &&
              ent["id"].asString() == newPaperId) {
            alreadyExists = true;
            break;
          } else if (ent.isString() && ent.asString() == newPaperId) {
            alreadyExists = true;
            break;
          }
        }

        if (!alreadyExists) {
          Json::Value newEnt;
          newEnt["id"] = newPaperId;
          newEnt["uniqueId"] = gnp::utils::IdGeneratorUtils::generateAlphanumericId();
          entitlements.append(newEnt);
          changed = true;
        }
      }

      // If changes were made, update the subscription record
      if (changed) {
        Json::StreamWriterBuilder writerBuilder;
        writerBuilder["indentation"] = "";
        userSub.setNewspaperEntitlements(Json::writeString(writerBuilder, entitlements));
        co_await subMapper.update(userSub);
        updatedCount++;
      }
    }

    dto::BaseApiResponse response;
    response.success = true;
    response.message = "Entitlements regenerated successfully.";
    response.result["updatedUsersCount"] = updatedCount;
    response.result["newspapersProcessedCount"] = (Json::UInt64)newspapers.size();
    
    co_return response;

  } catch (const drogon::orm::DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Database error during entitlement regeneration.";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  } catch (const std::exception &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "An error occurred: " + std::string(e.what());
    co_return errorResponse;
  }
}


void NewspaperService::incrementViewCount(
    const std::string &id,
    const std::function<void(const dto::BaseApiResponse &)> &callback) {

  auto dbClient = drogon::app().getDbClient();

  dbClient->execSqlAsync(
      "UPDATE newspapers SET views = views + 1 WHERE id = $1",
      [callback](const drogon::orm::Result &result) {
        dto::BaseApiResponse response;
        if (result.affectedRows() > 0) {
          response.success = true;
          response.message = "View count incremented successfully";
        } else {
          response.success = false;
          response.message = "Newspaper not found";
          response.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
        }
        callback(response);
      },
      [callback](const drogon::orm::DrogonDbException &e) {
        dto::BaseApiResponse response;
        response.success = false;
        response.message = "Database error while incrementing view count";
        response.error["code"] = constants::ERR_DB_QUERY;
        response.error["detail"] = e.base().what();
        callback(response);
      },
      id);
}



 drogon::Task<gnp::dto::BaseApiResponse> NewspaperService::handleOcrIngestion(const dto::OcrIngestionDto &dto) {

  LOG_INFO << "OCR Ingestion request received:"
           << " id=" << dto.getId()
           << ", publicationDate=" << dto.getPublicationDate().toDbStringLocal()
           << ", pageNumber=" << dto.getPageNumber()
           << ", editionNumber=" << dto.getEditionNumber()
           << ", fileType=" << dto.getFileType()
           << ", thumbnailId=" << dto.getThumbnailId()
           << ", contentType=" << dto.getContentType()
           << ", pageSummary=" << dto.getPageSummary()
           << ", pageTextLength=" << dto.getPageText().size();

  auto dbClient = drogon::app().getDbClient();
  auto mp_newspaper = drogon::orm::CoroMapper<Newspapers>(dbClient);
  auto mp_detail = drogon::orm::CoroMapper<drogon_model::Gnp::NewspaperDetails>(dbClient);

  try {
    // check if a newspaper exists for that publication date
    Criteria criteria = Criteria(Newspapers::Cols::_publication_date, CompareOperator::EQ, dto.getPublicationDate());
    auto newspapers = co_await mp_newspaper.limit(1).findBy(criteria);

    Newspapers newspaper;

    //if newspaper is null, create a new news paper record and create a 1st detail record linked to the newspaper record
    if (newspapers.empty()) {

      Newspapers new_newspaper;

      std::string dayStr = dto.getPublicationDate().toCustomFormattedString("%d");
      if (!dayStr.empty() && dayStr[0] == '0') {
          dayStr = dayStr.substr(1);
      }
      std::string titleStr = "DG " + dto.getPublicationDate().toCustomFormattedString("%A, %B ") + dayStr + dto.getPublicationDate().toCustomFormattedString(", %Y");
      new_newspaper.setTitle(titleStr);
      
      std::string slugStr = "dg-" + dto.getPublicationDate().toCustomFormattedString("%A-%B-%d-%Y");
      for (auto& c : slugStr) {
          if (c >= 'A' && c <= 'Z') c = c + ('a' - 'A');
      }
      new_newspaper.setId(dto.getId());
      new_newspaper.setSlug(slugStr);
      new_newspaper.setPrice("0.1");
      new_newspaper.setIsArchived(true);
      new_newspaper.setIsFree(false);
      new_newspaper.setIsPublished(true);
      new_newspaper.setStorageService("google-drive");
      new_newspaper.setCopyrightOwner("Graphic Communications Group Limited");
      new_newspaper.setEditionNumber(dto.getEditionNumber());
      new_newspaper.setFullDescription(dto.getPageSummary());
      //new_newspaper.setPublishedDate(trantor::Date::fromDbStringLocal("2023-02-02 00:00:00")); // set date to  2nd february,2023
      new_newspaper.setPublishedDate(dto.getPublicationDate());
      new_newspaper.setPublicationDate(dto.getPublicationDate());
      new_newspaper.setIsPublished(true);
      new_newspaper.setPublicationId("485ac7f9-022d-4a30-818e-41732d90da18");
      new_newspaper.setPublicationName("Daily Graphic");
      new_newspaper.setCreatorName("System");
      new_newspaper.setThumbnailId(dto.getThumbnailId());
      new_newspaper.setDocumentId("");

      new_newspaper.setFileType(dto.getFileType());
      new_newspaper.setCreatedAt(trantor::Date::now());
      new_newspaper.setIsPublished(false);
      new_newspaper.setIsFree(false);
      new_newspaper.setIsPopular(false);
      
      newspaper = co_await mp_newspaper.insert(new_newspaper);
    } else {
      //else add up to the details record linked to the parent newspaper record
      newspaper = newspapers[0];
    }

    drogon_model::Gnp::NewspaperDetails detail;
    detail.setNewspaperId(newspaper.getValueOfId());
    detail.setId(dto.getId());
    detail.setPageText(dto.getPageText());
    detail.setPageNumber(dto.getPageNumber());
    detail.setFileType(dto.getFileType());
    detail.setPublicationId("485ac7f9-022d-4a30-818e-41732d90da18");
    detail.setPublicationName("Daily Graphic");
    detail.setCreatorName("System");
    detail.setThumbnailId(dto.getThumbnailId());
    detail.setDocumentId("");

    detail.setPublishedDate(trantor::Date::fromDbStringLocal("2023-02-02 00:00:00"));

    if (!dto.getContentType().empty()) detail.setContentType(dto.getContentType());


    Json::Value tagsJson(Json::arrayValue);
    for (const auto& tag : dto.getTags()) tagsJson.append(tag);
    detail.setTags(tagsJson.empty() ? "[]" : Json::FastWriter().write(tagsJson));

    Json::Value catJson(Json::arrayValue);
    for (const auto& cat : dto.getCategories()) catJson.append(cat);
    detail.setCategories(catJson.empty() ? "[]" : Json::FastWriter().write(catJson));

    if (!dto.getPageSummary().empty()) detail.setSupportingText(dto.getPageSummary());
    
    detail.setPublicationDate(dto.getPublicationDate());
    detail.setPublishedDate(dto.getDatePublished());

    detail.setCreatedAt(trantor::Date::now());

    co_await mp_detail.insert(detail);

    dto::BaseApiResponse successResponse;
    successResponse.success = true;
    successResponse.message = "OCR ingestion handled successfully";
    co_return successResponse;

  } catch (const drogon::orm::DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Database error during OCR ingestion";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}


NewspaperService::DeliveryChannel  NewspaperService::parseDeliveryChannel(const std::string &channel) {
    std::string c;
    c.reserve(channel.size());
    for (char ch : channel)
        c.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));

    if (c.empty() || c == "both" || c == "all") return DeliveryChannel::Both;
    if (c == "sms")   return DeliveryChannel::Sms;
    if (c == "email") return DeliveryChannel::Email;

    LOG_WARN << "Unknown delivery channel '" << channel << "', defaulting to both.";
    return DeliveryChannel::Both;
}

bool NewspaperService::isSystemGeneratedEmail(const std::string &email) {
    static const std::string domain = "@graphicnewsplus.com.gh";
    if (email.length() <= domain.length()) return false;

    const std::string emailDomain = email.substr(email.length() - domain.length());
    if (emailDomain != domain) return false;

    const std::string phonePart = email.substr(0, email.length() - domain.length());
    if (phonePart.empty() || phonePart[0] != '0') return false;

    for (char c : phonePart) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}


drogon::Task<std::string> NewspaperService::buildNewspaperCards(drogon::orm::CoroMapper<drogon_model::Gnp::Newspapers> &newsMapper,
    const std::string &todayIso) {

    using namespace drogon::orm;
    using Newspapers = drogon_model::Gnp::Newspapers;

    Criteria newsCriteria =
        Criteria(Newspapers::Cols::_publication_date, CompareOperator::EQ, todayIso) &&
        Criteria(Newspapers::Cols::_is_published,     CompareOperator::EQ, true) &&
        Criteria(Newspapers::Cols::_is_archived,      CompareOperator::EQ, false);

    auto todaysNewspapers = co_await newsMapper
        .orderBy(Newspapers::Cols::_publication_date, SortOrder::DESC)
        .findBy(newsCriteria);

    std::string cards;
    for (const auto &newspaper : todaysNewspapers) {
        const std::string title     = newspaper.getValueOfTitle();
        const std::string id        = newspaper.getValueOfId();
        const std::string shortDesc = newspaper.getValueOfFullDescription();
        const std::string thumbnailUrl = newspaper.getValueOfThumbnailId();

        const std::string imgSrc = isUsableThumbnailUrl(thumbnailUrl) ? thumbnailUrl : std::string(kDefaultNewsThumbnail);

        cards += R"(
          <table border="0" cellpadding="0" cellspacing="0" width="100%" style="margin-bottom: 20px; background-color: #ffffff; border: 1px solid #fee2e2; border-left: 4px solid #dc2626; border-radius: 8px; border-collapse: separate; overflow: hidden; box-shadow: 0 2px 5px rgba(0,0,0,0.02);">
            <tr>
              <td style="padding: 20px;">
                <table border="0" cellpadding="0" cellspacing="0" width="100%">
                  <tr>
                    <td width="90" valign="top" style="padding-right: 18px;">
                      <img src=")" + imgSrc + R"(" alt=")" + title + R"(" width="90" height="115" style="display: block; width: 90px; height: 115px; object-fit: cover; border-radius: 6px; border: 1px solid #fecdd3;" />
                    </td>
                    <td valign="top" style="text-align: left;">
                      <h3 style="margin: 0 0 8px 0; font-size: 17px; line-height: 1.3; font-weight: 700; color: #111827; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;">
                        )" + title + R"(
                      </h3>
                      <p style="margin: 0 0 16px 0; font-size: 13px; line-height: 1.5; color: #4b5563; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;">
                        )" + shortDesc + R"(
                      </p>
                      <div>
                        <a href="https://new.graphicnewsplus.com/newspapers/)" + id + R"(" target="_blank" style="display: inline-block; padding: 9px 20px; background-color: #b91c1c; color: #ffffff; text-decoration: none; border-radius: 6px; font-size: 13px; font-weight: 600; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;">
                          Read Edition &rarr;
                        </a>
                      </div>
                    </td>
                  </tr>
                </table>
              </td>
            </tr>
          </table>
        )";
    }

    if (cards.empty()) {
        cards = R"(
          <table border="0" cellpadding="0" cellspacing="0" width="100%" style="background-color: #fff1f2; border: 1px dashed #fca5a5; border-radius: 8px; margin-bottom: 24px;">
            <tr>
              <td style="padding: 32px; text-align: center;">
                <p style="margin: 0; font-size: 14px; color: #991b1b; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;">
                  No new editions have been published today. Please check back later or explore previous archives!
                </p>
              </td>
            </tr>
          </table>
        )";
    }

    co_return cards;
}



std::string  NewspaperService::buildDailyNewsEmailBody(const std::string &firstName,
                                          const std::string &todayFormatted,
                                          const std::string &newspaperCards,
                                          const std::string &userId) {
    return R"(<!DOCTYPE html>
    <html lang="en">
    <head>
      <meta charset="UTF-8" />
      <meta name="viewport" content="width=device-width, initial-scale=1.0" />
      <meta http-equiv="X-UA-Compatible" content="IE=edge" />
      <title>Graphic NewsPlus - Daily Briefing</title>
    </head>
    <body style="margin: 0; padding: 0; background-color: #fcf8f8; -webkit-font-smoothing: antialiased;">
      <table border="0" cellpadding="0" cellspacing="0" width="100%" style="background-color: #fcf8f8; padding: 40px 10px;">
        <tr>
          <td align="center">
            <table border="0" cellpadding="0" cellspacing="0" width="100%" style="max-width: 600px; background-color: #ffffff; border-radius: 16px; overflow: hidden; box-shadow: 0 10px 30px rgba(185, 28, 28, 0.08); border: 1px solid #fee2e2;">

              <tr>
                <td style="background-color: #991b1b; background: linear-gradient(135deg, #7f1d1d 0%, #b91c1c 100%); padding: 36px 32px; text-align: left;">
                  <table border="0" cellpadding="0" cellspacing="0" width="100%">
                    <tr>
                      <td>
                        <span style="display: inline-block; font-size: 11px; font-weight: 700; letter-spacing: 1.5px; text-transform: uppercase; color: #fecdd3; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;">
                          Graphic NewsPlus
                        </span>
                        <h1 style="margin: 6px 0 0 0; font-size: 24px; font-weight: 700; color: #ffffff; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; letter-spacing: -0.3px;">
                          Your Daily Digest
                        </h1>
                      </td>
                      <td align="right" valign="bottom">
                        <span style="font-size: 13px; font-weight: 600; color: #fecdd3; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;">
                          )" + todayFormatted + R"(
                        </span>
                      </td>
                    </tr>
                  </table>
                </td>
              </tr>

              <tr>
                <td style="background-color: #dc2626; height: 4px; line-height: 4px; font-size: 4px;">&nbsp;</td>
              </tr>

              <tr>
                <td style="padding: 32px;">
                  <p style="margin: 0 0 24px 0; font-size: 15px; line-height: 1.6; color: #374151; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;">
                    Hello <strong style="color: #991b1b;">)" + firstName + R"(</strong>,<br/>
                    Here are today's featured editions curated for you. Click any edition to start reading instantly.
                  </p>

                  )" + newspaperCards + R"(

                </td>
              </tr>

              <tr>
                <td style="background-color: #fff1f2; padding: 24px 32px; text-align: center; border-top: 1px solid #ffe4e6;">
                  <p style="margin: 0 0 8px 0; font-size: 12px; line-height: 1.5; color: #9f1239; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;">
                    You are receiving this daily update as an active subscriber to <strong>Graphic NewsPlus</strong>.
                  </p>
                  <p style="margin: 0 0 12px 0; font-size: 12px; color: #f43f5e; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;">
                    &copy; Graphic NewsPlus. All rights reserved.
                  </p>
                  <p style="margin: 0; font-size: 11px; color: #6b7280; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;">
                    Don&apos;t want these emails?
                    <a href="https://new.graphicnewsplus.com/notifications/unsubscribe?userId=)" + userId + R"(&type=0" target="_blank" style="color: #dc2626; text-decoration: underline; font-weight: 600;">Unsubscribe from Daily Digest</a>
                  </p>
                </td>
              </tr>

            </table>
          </td>
        </tr>
      </table>
    </body>
    </html>)";
}

std::string NewspaperService::buildDailyNewsSmsBody() {
    return "Good morning! Your daily news update from Graphic NewsPlus is ready. "
           "Visit https://new.graphicnewsplus.com to read today's editions.";
}

drogon::Task<bool> NewspaperService::hasOptedOutOfDailyNews(
    drogon::orm::CoroMapper<drogon_model::Gnp::UserNotificationSubscriptions> &notifMapper,
    const std::string &userId) {

    using namespace drogon::orm;
    using UNS = drogon_model::Gnp::UserNotificationSubscriptions;

    Criteria optOutCriteria =
        Criteria(UNS::Cols::_user_id,             CompareOperator::EQ, userId) &&
        Criteria(UNS::Cols::_notification_type,   CompareOperator::EQ,
                 static_cast<int32_t>(gnp::constants::NotificationTypes::DAILY_NEWS_UPDATE)) &&
        Criteria(UNS::Cols::_subscription_status, CompareOperator::EQ,
                 static_cast<int32_t>(gnp::constants::SubscriptionStatus::UNSUBSCRIBED));

    auto optOutRecords = co_await notifMapper.findBy(optOutCriteria);
    co_return !optOutRecords.empty();
}

drogon::Task<void> NewspaperService::sendDailyNewsEmail(const drogon_model::Gnp::Users &user,
                                     const std::string &newspaperCards,
                                     const std::string &todayFormatted) {
    const std::string userEmail = user.getValueOfEmail();
    if (userEmail.empty() || isSystemGeneratedEmail(userEmail)) co_return;

    const std::string firstName =
        user.getValueOfFirstName().empty() ? "GNP User" : user.getValueOfFirstName();

    auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
    auto &emailService = plugin->getEmailService();

    gnp::dto::SendEmailDto emailDto;
    emailDto.setTo(userEmail);
    emailDto.setSubject("Your Daily News Update — " + todayFormatted);
    emailDto.setBody(buildDailyNewsEmailBody(firstName, todayFormatted,
                                             newspaperCards, user.getValueOfId()));

    try {
        co_await emailService.sendEmailAsync(emailDto);
        LOG_INFO << "Daily news email sent to: " << userEmail;
    } catch (const std::exception &e) {
        LOG_ERROR << "Failed to send daily news email to " << userEmail << ": " << e.what();
    }
}

drogon::Task<void> NewspaperService::sendDailyNewsSms(const drogon_model::Gnp::Users &user) {
    const std::string userPhone = user.getValueOfPhoneNumber();
    if (userPhone.empty()) co_return;

    auto plugin = drogon::app().getPlugin<gnp::plugins::GnpServicePlugin>();
    auto &hubtelSmsApi = plugin->getHubtelSmsApi();

    try {
        co_await hubtelSmsApi.sendSms(userPhone, buildDailyNewsSmsBody());
        LOG_INFO << "Daily news SMS sent to: " << userPhone;
    } catch (const std::exception &e) {
        LOG_ERROR << "Failed to send daily news SMS to " << userPhone << ": " << e.what();
    }
}

drogon::Task<void> NewspaperService::dispatchDailyNewsUpdate(const std::string &deliveryChannel) {

    const auto channel   = parseDeliveryChannel(deliveryChannel);
    const bool sendEmail = (channel == DeliveryChannel::Email || channel == DeliveryChannel::Both);
    const bool sendSms   = (channel == DeliveryChannel::Sms   || channel == DeliveryChannel::Both);

    auto dbClient = drogon::app().getDbClient();

    try {
        using namespace drogon::orm;
        using Users = drogon_model::Gnp::Users;

        CoroMapper<Users> userMapper(dbClient);
        auto users = co_await userMapper.findBy(
            Criteria(Users::Cols::_is_active, CompareOperator::EQ, true));

        // Build the shared date strings up-front.
        auto rawDate        = ::trantor::Date::now();
        auto todayFormatted = rawDate.toCustomFormattedStringLocal("%d %b, %Y");
        auto todayIso       = rawDate.toCustomFormattedStringLocal("%Y-%m-%d");

        // Newspaper cards are only needed for email dispatches.
        std::string newspaperCards;
        if (sendEmail) {
            CoroMapper<drogon_model::Gnp::Newspapers> newsMapper(dbClient);
            newspaperCards = co_await buildNewspaperCards(newsMapper, todayIso);
        }

        CoroMapper<drogon_model::Gnp::UserNotificationSubscriptions> notifMapper(dbClient);

        for (const auto &user : users) {
            if (user.getValueOfEmail().empty() && user.getValueOfPhoneNumber().empty()) continue;

            // Skip users who opted out of DAILY_NEWS_UPDATE.
            if (co_await hasOptedOutOfDailyNews(notifMapper, user.getValueOfId())) {
                LOG_INFO << "Skipping daily news update for user " << user.getValueOfId()
                         << " (opted out)";
                continue;
            }

            const bool hasRealEmail =
                !user.getValueOfEmail().empty() && !isSystemGeneratedEmail(user.getValueOfEmail());

            if (sendEmail && hasRealEmail) {
                co_await sendDailyNewsEmail(user, newspaperCards, todayFormatted);
            }

            if (sendSms) {
                co_await sendDailyNewsSms(user);
            }
        }
    } catch (const std::exception &e) {
        LOG_ERROR << "Failed to dispatch daily news update: " << e.what();
    }
}


  drogon::Task<gnp::dto::BaseApiResponse> NewspaperService::trackUserEngagement(
    const dto::UserEngagementDto &dto,
    const std::string &userId) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<drogon_model::Gnp::NewspaperEngagement> engagementMapper(dbClient);
  CoroMapper<drogon_model::Gnp::Newspapers> newspaperMapper(dbClient);

  try {
    // Validate required fields
    if (dto.getNewspaperId().empty() || userId.empty()) {
      dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "Newspaper ID and User ID are required";
      errorResponse.error["code"] = constants::ERR_VALIDATION;
      co_return errorResponse;
    }

    // Get user to retrieve partner_id
    CoroMapper<drogon_model::Gnp::Users> userMapper(dbClient);
    auto user = co_await userMapper.findByPrimaryKey(userId);

    // Check if an engagement record already exists for this user and newspaper
    auto existingEngagements = co_await engagementMapper
        .orderBy(drogon_model::Gnp::NewspaperEngagement::Cols::_viewed_at,
                 SortOrder::DESC)
        .limit(1)
        .findBy(
            Criteria(drogon_model::Gnp::NewspaperEngagement::Cols::_newspaper_id,
                     CompareOperator::EQ, dto.getNewspaperId()) &&
            Criteria(drogon_model::Gnp::NewspaperEngagement::Cols::_user_id,
                     CompareOperator::EQ, userId));

    trantor::Date currentTime = trantor::Date::now();
    bool isFirstView = existingEngagements.empty();

    if (!isFirstView) {
      // ── UPDATE EXISTING RECORD ──────────────────────────────────────────
      auto engagement = existingEngagements[0];

      // Calculate time spent since last view
      trantor::Date lastViewed = engagement.getValueOfViewedAt();
      int64_t diffMicroSeconds = currentTime.microSecondsSinceEpoch() -
                                 lastViewed.microSecondsSinceEpoch();
      int32_t timeSpentDelta = static_cast<int32_t>(diffMicroSeconds / 1000000);

      // Only update if delta is positive and reasonable (not more than 1 hour)
      if (timeSpentDelta > 0 && timeSpentDelta < 3600) {
        int32_t currentTotal = engagement.getValueOfTimeSpentSeconds();
        engagement.setTimeSpentSeconds(currentTotal + timeSpentDelta);
      }

      // Update timestamps
      engagement.setLastViewed(currentTime);
      engagement.setViewedAt(currentTime);  // Update viewed_at to current time

      // Update device type if provided and different
      if (!dto.getDeviceType().empty()) {
        engagement.setDeviceType(dto.getDeviceType());
      }

      // Update user agent if provided
      if (!dto.getUserAgent().empty()) {
        engagement.setUserAgent(dto.getUserAgent());
      }

      co_await engagementMapper.update(engagement);

      dto::BaseApiResponse response;
      response.success = true;
      response.message = "Engagement updated successfully";
      response.result["newspaperId"] = dto.getNewspaperId();
      response.result["userId"] = userId;
      response.result["totalTimeSpentSeconds"] = engagement.getValueOfTimeSpentSeconds();
      response.result["action"] = "updated";

      co_return response;

    } else {
      // ── CREATE NEW RECORD ──────────────────────────────────────────────
      drogon_model::Gnp::NewspaperEngagement engagement;
      engagement.setNewspaperId(dto.getNewspaperId());
      engagement.setUserId(userId);
      engagement.setPartnerId(user.getValueOfPartnerId());
      engagement.setDeviceType(dto.getDeviceType().empty() ? "web" : dto.getDeviceType());
      engagement.setViewedAt(currentTime);
      engagement.setLastViewed(currentTime);
      engagement.setTimeSpentSeconds(0);

      if (!dto.getUserAgent().empty()) {
        engagement.setUserAgent(dto.getUserAgent());
      }

      co_await engagementMapper.insert(engagement);

      // ── INCREMENT VIEWS IN NEWSPAPER TABLE ─────────────────────────────
      // Only increment views for the newspaper when it's the first view
      try {
        auto newspaper = co_await newspaperMapper.findByPrimaryKey(dto.getNewspaperId());
        int32_t currentViews = newspaper.getValueOfViews();
        newspaper.setViews(currentViews + 1);
        co_await newspaperMapper.update(newspaper);

        LOG_INFO << "[trackUserEngagement] Incremented views for newspaper "
                 << dto.getNewspaperId() << " from " << currentViews
                 << " to " << (currentViews + 1);
      } catch (const std::exception &e) {
        LOG_ERROR << "[trackUserEngagement] Failed to increment views for newspaper "
                  << dto.getNewspaperId() << ": " << e.what();
        // Don't fail the whole operation if view increment fails
      }

      dto::BaseApiResponse response;
      response.success = true;
      response.message = "Engagement tracked successfully";
      response.result["newspaperId"] = dto.getNewspaperId();
      response.result["userId"] = userId;
      response.result["action"] = "created";

      co_return response;
    }

  } catch (const drogon::orm::UnexpectedRows &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "User not found";
    errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    errorResponse.error["detail"] = e.what();
    co_return errorResponse;
  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Database error while tracking engagement";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}



 drogon::Task<gnp::dto::BaseApiResponse> NewspaperService::updateUserEngagement(
    const dto::UserEngagementDto &dto,
    const std::string &userId) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<drogon_model::Gnp::NewspaperEngagement> engagementMapper(dbClient);

  try {
    // Validate required fields
    if (dto.getNewspaperId().empty() || userId.empty()) {
      dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "Newspaper ID and User ID are required";
      errorResponse.error["code"] = constants::ERR_VALIDATION;
      co_return errorResponse;
    }

    // Find the most recent engagement record for this user and newspaper
    auto engagements = co_await engagementMapper
        .orderBy(drogon_model::Gnp::NewspaperEngagement::Cols::_viewed_at,
                 SortOrder::DESC)
        .limit(1)
        .findBy(
            Criteria(drogon_model::Gnp::NewspaperEngagement::Cols::_newspaper_id,
                     CompareOperator::EQ, dto.getNewspaperId()) &&
            Criteria(drogon_model::Gnp::NewspaperEngagement::Cols::_user_id,
                     CompareOperator::EQ, userId));

    if (engagements.empty()) {
      dto::BaseApiResponse errorResponse;
      errorResponse.success = false;
      errorResponse.message = "No engagement record found for this user and newspaper";
      errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
      co_return errorResponse;
    }

    auto engagement = engagements[0];

    // Get the current time
    trantor::Date currentTime = trantor::Date::now();
    trantor::Date lastViewed = engagement.getValueOfViewedAt();

    // Calculate time spent in seconds since last view
    int64_t diffMicroSeconds = currentTime.microSecondsSinceEpoch() -
                               lastViewed.microSecondsSinceEpoch();
    int32_t timeSpentDelta = static_cast<int32_t>(diffMicroSeconds / 1000000);

    // Only update if the delta is positive and reasonable (not more than 1 hour)
    if (timeSpentDelta > 0 && timeSpentDelta < 3600) {
      int32_t currentTotal = engagement.getValueOfTimeSpentSeconds();
      engagement.setTimeSpentSeconds(currentTotal + timeSpentDelta);
    }

    // Update last viewed timestamp
    engagement.setLastViewed(currentTime);
    engagement.setUserAgent(dto.getUserAgent());

    co_await engagementMapper.update(engagement);

    dto::BaseApiResponse response;
    response.success = true;
    response.message = "Engagement updated successfully";
    response.result["newspaperId"] = dto.getNewspaperId();
    response.result["userId"] = userId;
    response.result["totalTimeSpentSeconds"] = engagement.getValueOfTimeSpentSeconds();

    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Database error while updating engagement";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

} // namespace gnp::services
