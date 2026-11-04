#include <drogon/drogon_test.h>
#include "dto/VideoDto.h"

DROGON_TEST(VideoDtoParsingAndValidation) {
    Json::Value json;
    json["title"] = "News bulletin";
    json["fileName"] = "bulletin.mp4";
    json["filePath"] = "/videos/bulletin.mp4";
    json["fileSize"] = 1024;
    json["durationInMins"] = 5;
    json["tags"] = Json::arrayValue;
    json["tags"].append("news");
    json["tags"].append("quote\" and slash\\");
    json["metadata"]["source"] = "studio";
    gnp::dto::VideoDto value(json);
    CHECK(value.validate().empty());
    CHECK(value.getTitle() == "News bulletin");
    CHECK(value.getFileSize() == 1024);
    CHECK(value.getDurationInMins() == 5);
    CHECK(value.getTags().size() == 2);
    CHECK(value.getMetadata()["source"].asString() == "studio");

    // Parsing replaces previous state rather than accumulating tags.
    value.fromJson(json);
    CHECK(value.getTags().size() == 2);
    value.setFileSize(-1);
    CHECK(!value.validate().empty());
    value.setFileSize(0);
    value.setTitle("  \t");
    CHECK(!value.validate().empty());
    value.setTitle(std::string(501, 'a'));
    CHECK(!value.validate().empty());
    CHECK(!gnp::dto::VideoDto().validate().empty());
}

DROGON_TEST(VideoDtoRejectsInvalidTypes) {
    auto rejects = [](const Json::Value &json) {
        try {
            gnp::dto::VideoDto value(json);
            return false;
        } catch (const std::invalid_argument &) {
            return true;
        }
    };
    CHECK(rejects(Json::Value(Json::arrayValue)));
    Json::Value json;
    json["fileSize"] = "large";
    CHECK(rejects(json));
    json.clear();
    json["tags"].append(123);
    CHECK(rejects(json));
    json.clear();
    json["metadata"] = "not an object";
    CHECK(rejects(json));
}
