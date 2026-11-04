#pragma once

#include <json/json.h>
#include <string>
#include <vector>
#include <stdexcept>

namespace gnp::dto {

// Full replacement DTO. Counters, IDs and timestamps are server-managed.
class VideoDto {
public:
    VideoDto() = default;
    explicit VideoDto(const Json::Value &json) { fromJson(json); }

    [[nodiscard]] const std::string & getTitle() const { return title_; }
    void setTitle(const std::string & value) { title_ = value; }
    [[nodiscard]] const std::string & getDescription() const { return description_; }
    void setDescription(const std::string & value) { description_ = value; }
    [[nodiscard]] const std::string & getFileName() const { return fileName_; }
    void setFileName(const std::string & value) { fileName_ = value; }
    [[nodiscard]] const std::string & getFilePath() const { return filePath_; }
    void setFilePath(const std::string & value) { filePath_ = value; }
    [[nodiscard]] int getFileSize() const { return fileSize_; }
    void setFileSize(int value) { fileSize_ = value; }
    [[nodiscard]] int getDurationInMins() const { return durationInMins_; }
    void setDurationInMins(int value) { durationInMins_ = value; }
    [[nodiscard]] const std::string & getFormat() const { return format_; }
    void setFormat(const std::string & value) { format_ = value; }
    [[nodiscard]] const std::string & getThumbnailUrl() const { return thumbnailUrl_; }
    void setThumbnailUrl(const std::string & value) { thumbnailUrl_ = value; }
    [[nodiscard]] int getStatus() const { return status_; }
    void setStatus(int value) { status_ = value; }
    [[nodiscard]] const std::string & getChecksum() const { return checksum_; }
    void setChecksum(const std::string & value) { checksum_ = value; }
    [[nodiscard]] const std::string & getCreatedBy() const { return createdBy_; }
    void setCreatedBy(const std::string & value) { createdBy_ = value; }
    [[nodiscard]] const std::string & getModifiedBy() const { return modifiedBy_; }
    void setModifiedBy(const std::string & value) { modifiedBy_ = value; }
    [[nodiscard]] const std::vector<std::string> & getTags() const { return tags_; }
    void setTags(const std::vector<std::string> & value) { tags_ = value; }
    [[nodiscard]] const Json::Value & getMetadata() const { return metadata_; }
    void setMetadata(const Json::Value & value) { metadata_ = value; }

    void fromJson(const Json::Value &json) {
        if (!json.isObject()) throw std::invalid_argument("Video must be an object");
        VideoDto value;
        if (json.isMember("title") && !json["title"].isNull()) {
            if (!json["title"].isString()) throw std::invalid_argument("Invalid title");
            value.title_ = json["title"].asString();
        }
        if (json.isMember("description") && !json["description"].isNull()) {
            if (!json["description"].isString()) throw std::invalid_argument("Invalid description");
            value.description_ = json["description"].asString();
        }
        if (json.isMember("fileName") && !json["fileName"].isNull()) {
            if (!json["fileName"].isString()) throw std::invalid_argument("Invalid fileName");
            value.fileName_ = json["fileName"].asString();
        }
        if (json.isMember("filePath") && !json["filePath"].isNull()) {
            if (!json["filePath"].isString()) throw std::invalid_argument("Invalid filePath");
            value.filePath_ = json["filePath"].asString();
        }
        if (json.isMember("fileSize") && !json["fileSize"].isNull()) {
            if (!json["fileSize"].isInt()) throw std::invalid_argument("Invalid fileSize");
            value.fileSize_ = json["fileSize"].asInt();
        }
        if (json.isMember("durationInMins") && !json["durationInMins"].isNull()) {
            if (!json["durationInMins"].isInt()) throw std::invalid_argument("Invalid durationInMins");
            value.durationInMins_ = json["durationInMins"].asInt();
        }
        if (json.isMember("format") && !json["format"].isNull()) {
            if (!json["format"].isString()) throw std::invalid_argument("Invalid format");
            value.format_ = json["format"].asString();
        }
        if (json.isMember("thumbnailUrl") && !json["thumbnailUrl"].isNull()) {
            if (!json["thumbnailUrl"].isString()) throw std::invalid_argument("Invalid thumbnailUrl");
            value.thumbnailUrl_ = json["thumbnailUrl"].asString();
        }
        if (json.isMember("status") && !json["status"].isNull()) {
            if (!json["status"].isInt()) throw std::invalid_argument("Invalid status");
            value.status_ = json["status"].asInt();
        }
        if (json.isMember("checksum") && !json["checksum"].isNull()) {
            if (!json["checksum"].isString()) throw std::invalid_argument("Invalid checksum");
            value.checksum_ = json["checksum"].asString();
        }
        if (json.isMember("createdBy") && !json["createdBy"].isNull()) {
            if (!json["createdBy"].isString()) throw std::invalid_argument("Invalid createdBy");
            value.createdBy_ = json["createdBy"].asString();
        }
        if (json.isMember("modifiedBy") && !json["modifiedBy"].isNull()) {
            if (!json["modifiedBy"].isString()) throw std::invalid_argument("Invalid modifiedBy");
            value.modifiedBy_ = json["modifiedBy"].asString();
        }
        if (json.isMember("tags") && !json["tags"].isNull()) {
            if (!json["tags"].isArray()) throw std::invalid_argument("Invalid tags");
            for (const auto &tag : json["tags"]) {
                if (!tag.isString()) throw std::invalid_argument("Tags must be strings");
                value.tags_.push_back(tag.asString());
            }
        }
        if (json.isMember("metadata") && !json["metadata"].isNull()) {
            if (!json["metadata"].isObject()) throw std::invalid_argument("Invalid metadata");
            value.metadata_ = json["metadata"];
        }
        *this = value;
    }

    [[nodiscard]] std::string validate() const {
        if (title_.find_first_not_of(" \t\r\n") == std::string::npos || fileName_.empty() || filePath_.empty())
            return "Title, fileName and filePath are required.";
        if (title_.size() > 500 || fileName_.size() > 255 || filePath_.size() > 1000 ||
            format_.size() > 50 || thumbnailUrl_.size() > 150 || checksum_.size() > 64)
            return "A video field exceeds its maximum length.";
        if (fileSize_ < 0 || durationInMins_ < 0 || status_ < 0)
            return "File size, duration and status cannot be negative.";
        if (!metadata_.isObject()) return "Metadata must be an object.";
        return {};
    }

private:
    std::string title_;
    std::string description_;
    std::string fileName_;
    std::string filePath_;
    int fileSize_{0};
    int durationInMins_{0};
    std::string format_;
    std::string thumbnailUrl_;
    int status_{0};
    std::string checksum_;
    std::string createdBy_;
    std::string modifiedBy_;
    std::vector<std::string> tags_;
    Json::Value metadata_{Json::objectValue};
};

} // namespace gnp::dto
