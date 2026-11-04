#include "VideoService.h"

#include <algorithm>
#include <drogon/drogon.h>
#include <drogon/orm/CoroMapper.h>
#include "constants/ErrorCodes.h"
#include "models/Videos.h"

using namespace drogon::orm;
using drogon_model::Gnp::Videos;

namespace gnp::services {
namespace {
dto::BaseApiResponse failure(int code, const std::string &message) {
    dto::BaseApiResponse response;
    response.message = message;
    response.error["code"] = code;
    response.error["message"] = message;
    return response;
}

// PostgreSQL text[] is not JSON: quote every element and escape array syntax.
std::string tagsArray(const std::vector<std::string> &tags) {
    std::string result = "{";
    for (const auto &tag : tags) {
        if (result.size() > 1) result += ',';
        result += '"';
        for (char c : tag) {
            if (c == '"' || c == '\\') result += '\\';
            result += c;
        }
        result += '"';
    }
    return result + '}';
}

void apply(Videos &video, const dto::VideoDto &value) {
    video.setTitle(value.getTitle());
    video.setDescription(value.getDescription());
    video.setFileName(value.getFileName());
    video.setFilePath(value.getFilePath());
    video.setFileSize(value.getFileSize());
    video.setDurationInMins(value.getDurationInMins());
    video.setFormat(value.getFormat());
    video.setThumbnailUrl(value.getThumbnailUrl());
    video.setStatus(value.getStatus());
    video.setChecksum(value.getChecksum());
    video.setTags(tagsArray(value.getTags()));
    video.setMetadata(Json::writeString(Json::StreamWriterBuilder{}, value.getMetadata()));
    if (!value.getModifiedBy().empty()) video.setModifiedBy(value.getModifiedBy());
    video.setModifiedAt(trantor::Date::now());
}

dto::BaseApiResponse databaseFailure(const DrogonDbException &e) {
    LOG_ERROR << "Video database operation failed: " << e.base().what();
    return failure(constants::ERR_DB_QUERY, "Database error while processing video.");
}
} // namespace

drogon::Task<dto::BaseApiResponse> VideoService::getAll(int pageNo, int pageSize, const std::string &query) {
    if (pageNo < 1 || pageSize < 1 || pageSize > 100)
        co_return failure(constants::ERR_VALIDATION, "pageNo must be positive and pageSize must be between 1 and 100.");
    try {
        CoroMapper<Videos> mapper(drogon::app().getDbClient());
        Criteria criteria;
        if (!query.empty()) {
            const auto pattern = "%" + query + "%";
            criteria = Criteria(Videos::Cols::_title, CompareOperator::Like, pattern) ||
                       Criteria(Videos::Cols::_description, CompareOperator::Like, pattern);
        }
        const auto total = co_await mapper.count(criteria);
        const auto offset = static_cast<size_t>(pageNo - 1) * static_cast<size_t>(pageSize);
        Json::Value data(Json::arrayValue);
        if (offset < total) {
            auto videos = co_await mapper.orderBy(Videos::Cols::_created_at, SortOrder::DESC)
                .orderBy(Videos::Cols::_id).limit(pageSize).offset(offset).findBy(criteria);
            for (const auto &video : videos) data.append(video.toJson());
        }
        dto::BaseApiResponse response;
        response.success = true;
        response.result["data"] = data;
        response.result["totalCount"] = Json::UInt64(total);
        response.result["totalPages"] = Json::UInt64((total + pageSize - 1) / pageSize);
        response.result["pageNo"] = pageNo;
        response.result["pageSize"] = pageSize;
        response.result["lowerBound"] = Json::UInt64(data.empty() ? 0 : offset + 1);
        response.result["upperBound"] = Json::UInt64(data.empty() ? 0 : offset + data.size());
        co_return response;
    } catch (const DrogonDbException &e) {
        co_return databaseFailure(e);
    }
}

drogon::Task<dto::BaseApiResponse> VideoService::getVideoDetails(const std::string &id) {
    if (id.empty()) co_return failure(constants::ERR_VALIDATION, "Video ID is required.");
    try {
        CoroMapper<Videos> mapper(drogon::app().getDbClient());
        auto video = co_await mapper.findByPrimaryKey(id);
        dto::BaseApiResponse response;
        response.success = true;
        response.result = video.toJson();
        co_return response;
    } catch (const UnexpectedRows &) {
        co_return failure(constants::ERR_RESOURCE_NOT_FOUND, "Video not found.");
    } catch (const DrogonDbException &e) {
        co_return databaseFailure(e);
    }
}

drogon::Task<dto::BaseApiResponse> VideoService::create(const dto::VideoDto &value) {
    if (const auto error = value.validate(); !error.empty())
        co_return failure(constants::ERR_VALIDATION, error);
    try {
        CoroMapper<Videos> mapper(drogon::app().getDbClient());
        Videos video;
        apply(video, value);
        video.setId(drogon::utils::getUuid());
        video.setCreatedAt(trantor::Date::now());
        if (!value.getCreatedBy().empty()) video.setCreatedBy(value.getCreatedBy());
        video.setViews(0);
        video.setLikes(0);
        video.setDownloads(0);
        auto inserted = co_await mapper.insert(video);
        dto::BaseApiResponse response;
        response.success = true;
        response.message = "Video created successfully.";
        response.result = inserted.toJson();
        co_return response;
    } catch (const DrogonDbException &e) {
        co_return databaseFailure(e);
    }
}

drogon::Task<dto::BaseApiResponse> VideoService::update(const dto::VideoDto &value, const std::string &id) {
    if (id.empty()) co_return failure(constants::ERR_VALIDATION, "Video ID is required.");
    if (const auto error = value.validate(); !error.empty())
        co_return failure(constants::ERR_VALIDATION, error);
    try {
        CoroMapper<Videos> mapper(drogon::app().getDbClient());
        auto video = co_await mapper.findByPrimaryKey(id);
        apply(video, value);
        // Preserve identity, creation audit fields and engagement counters.
        if (co_await mapper.update(video) == 0)
            co_return failure(constants::ERR_RESOURCE_NOT_FOUND, "Video not found.");
        dto::BaseApiResponse response;
        response.success = true;
        response.message = "Video updated successfully.";
        response.result = video.toJson();
        co_return response;
    } catch (const UnexpectedRows &) {
        co_return failure(constants::ERR_RESOURCE_NOT_FOUND, "Video not found.");
    } catch (const DrogonDbException &e) {
        co_return databaseFailure(e);
    }
}

drogon::Task<dto::BaseApiResponse> VideoService::deleteVideo(const std::string &id) {
    if (id.empty()) co_return failure(constants::ERR_VALIDATION, "Video ID is required.");
    try {
        CoroMapper<Videos> mapper(drogon::app().getDbClient());
        if (co_await mapper.deleteByPrimaryKey(id) == 0)
            co_return failure(constants::ERR_RESOURCE_NOT_FOUND, "Video not found.");
        dto::BaseApiResponse response;
        response.success = true;
        response.message = "Video deleted successfully.";
        co_return response;
    } catch (const DrogonDbException &e) {
        co_return databaseFailure(e);
    }
}
} // namespace gnp::services
