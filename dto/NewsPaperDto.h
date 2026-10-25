//
// Created by Emmanuel Addo-Odame on 12/07/2026.
//

#ifndef GNPAPI_NEWSPAPERDTO_H
#define GNPAPI_NEWSPAPERDTO_H
#include <algorithm>
#include <json/json.h>
#include <string>
#include <trantor/utils/Date.h>

namespace gnp::dto {

    class NewsPaperDto {

    public:
        NewsPaperDto() = default;
        explicit NewsPaperDto(const Json::Value& json);

        void fromJson(const Json::Value& json);

        // Getters
        [[nodiscard]] const std::string &getTitle() const { return title_; }
        [[nodiscard]] const std::string &getSlug() const { return slug_; }
        [[nodiscard]] double getPrice() const { return price_; }
        [[nodiscard]] bool isFree() const { return is_free_; }
        [[nodiscard]] const std::string &getPublicationId() const { return publication_id_; }
        [[nodiscard]] const std::string &getPublicationName() const { return publication_name_; }
        [[nodiscard]] const std::string &getEditionNumber() const { return edition_number_; }
        [[nodiscard]] bool getIsPopular() const { return is_popular_; }
        [[nodiscard]] const std::string &getFullDescription() const {  return full_description_; }
        [[nodiscard]] const std::string &getThumbnailId() const { return thumbnail_id_; }
        [[nodiscard]] int getFileType() const { return file_type_; }
        [[nodiscard]] const std::string &getStorageService() const { return storage_service_; }
        [[nodiscard]] const std::string &getDocumentId() const { return document_id_; }
        [[nodiscard]] bool isPublished() const { return is_published_; }
        [[nodiscard]] const ::trantor::Date &getPublicationDate() const { return publication_date_; }
        [[nodiscard]] const std::string &getFeaturedStories() const { return featured_stories_; }
        [[nodiscard]] const std::vector<std::string> &getTags() const { return tags_; }
        [[nodiscard]] const std::vector<std::string> &getCategories() const { return categories_; }

        // Setters
        void setTitle(const std::string &v) { title_ = v; }
        void setSlug(const std::string &v) { slug_ = v; }
        void setPrice(double v) { price_ = v; }
        void setIsFree(bool v) { is_free_ = v; }
        void setPublicationId(const std::string &v) { publication_id_ = v; }
        void setPublicationName(const std::string &v) { publication_name_ = v; }
        void setEditionNumber(const std::string &v) { edition_number_ = v; }
        void setIsPopular(bool v) { is_popular_ = v; }
        void setFullDescription(const std::string &v) { full_description_ = v; }
        void setThumbnailId(const std::string &v) { thumbnail_id_ = v; }
        void setFileType(const int &v) { file_type_ = v; }
        void setStorageService(const std::string &v) { storage_service_ = v; }
        void setDocumentId(const std::string &v) { document_id_ = v; }
        void setIsPublished(bool v) { is_published_ = v; }
        void setPublicationDate(const ::trantor::Date &v) { publication_date_ = v; }
        void setFeaturedStories(const std::string &v) { featured_stories_ = v; }
        void setTags(const std::vector<std::string> &value) { tags_ = value; }
        void setCategories(const std::vector<std::string> &value) { categories_ = value; }

    private:
        std::string title_;
        std::string slug_;
        double price_ {0};
        bool is_free_ {false};
        std::string publication_id_;
        std::string publication_name_;
        std::string edition_number_;
        bool is_popular_ {false};
        std::string full_description_;
        std::string thumbnail_id_;
        int file_type_ {0};
        std::string featured_stories_;
        std::string storage_service_;
        std::string document_id_;
        bool is_published_ {false};
        trantor::Date publication_date_;
        std::vector<std::string> tags_;
        std::vector<std::string> categories_;
    };

    inline void NewsPaperDto::fromJson(const Json::Value& json) {

        if (json.isMember("title") && !json["title"].isNull()) {
            title_ = json["title"].asString();
        }

        if (json.isMember("slug") && !json["slug"].isNull()) {
            slug_ = json["slug"].asString();
        }

        if (json.isMember("price") && !json["price"].isNull()) {
            price_ = json["price"].asDouble();
        }

        if (json.isMember("isFree") && !json["isFree"].isNull()) {
            is_free_ = json["isFree"].asBool();
        }

        if (json.isMember("slug") && !json["slug"].isNull()) {
            slug_ = json["slug"].asString();
        }

        if (json.isMember("publicationId") && !json["publicationId"].isNull()) {
            publication_id_ = json["publicationId"].asString();
        }

        if (json.isMember("publicationName") && !json["publicationName"].isNull()) {
            publication_name_ = json["publicationName"].asString();
        }

        if (json.isMember("editionNumber") && !json["editionNumber"].isNull()) {
            edition_number_ = json["editionNumber"].asString();
        }

        if (json.isMember("isPopular") && !json["isPopular"].isNull()) {
            is_popular_ = json["isPopular"].asBool();
        }

        if (json.isMember("fullDescription") && !json["fullDescription"].isNull()) {
            full_description_ = json["fullDescription"].asString();
        }

        if (json.isMember("thumbnailId") && !json["thumbnailId"].isNull()) {
            thumbnail_id_ = json["thumbnailId"].asString();
        }

        if (json.isMember("fileType") && !json["fileType"].isNull()) {
            file_type_ = json["fileType"].asInt();
        }

        if (json.isMember("storageService") && !json["storageService"].isNull()) {
            storage_service_ = json["storageService"].asString();
        }

        if (json.isMember("documentId") && !json["documentId"].isNull()) {
            document_id_ = json["documentId"].asString();
        }

        if (json.isMember("isPublished") && !json["isPublished"].isNull()) {
            is_published_ = json["isPublished"].asBool();
        }

        if (json.isMember("publicationDate") && !json["publicationDate"].isNull()) {
            std::string publicationDateStr = json["publicationDate"].asString();

            if (publicationDateStr.size() == 10) {
                // If it's just a date (YYYY-MM-DD), append midnight time
                publicationDateStr += " 00:00:00";
            } else {
                // Replace 'T' with space and add seconds
                std::replace(publicationDateStr.begin(), publicationDateStr.end(), 'T',
                             ' ');
                publicationDateStr += ":00"; // -> 2025-12-31 06:10:00
            }

            publication_date_ = trantor::Date::fromDbString(publicationDateStr);
        }

        if (json.isMember("featuredStories") && json["featuredStories"].isArray()) {
            Json::StreamWriterBuilder builder;
            builder["commentStyle"] = "None";
            builder["indentation"] = ""; // Compact JSON
            featured_stories_ = Json::writeString(builder, json["featuredStories"]);
        } else {
            featured_stories_ = "[]"; // Default empty array
        }


        if (json.isMember("tags") && json["tags"].isArray()) {
            for (const auto &ip : json["tags"]) {
                tags_.push_back(ip.asString());
            }
        }

        if (json.isMember("categories") && json["categories"].isArray()) {
            for (const auto &ip : json["categories"]) {
                categories_.push_back(ip.asString());
            }
        }
    }

     class VideoDto {

    public:

        VideoDto() = default;

        void fromJson(const Json::Value& json);

        // Getters
        [[nodiscard]] const std::string& getFirstName() const { return first_name_; }
        [[nodiscard]] const std::string& getLastName() const { return last_name_; }
        [[nodiscard]] const std::string& getEmail() const { return email_; }
        [[nodiscard]] const std::string& getUsername() const { return username_; }

        // Setters
        void setFirstName(const std::string& value) { first_name_ = value; }
        void setLastName(const std::string& value) { last_name_ = value; }
        void setEmail(const std::string& value) { email_ = value; }
        void setUsername(const std::string& value) { username_ = value; }


    private:

        std::string first_name_;
        std::string last_name_;
        std::string username_;
        std::string email_;
    };

    inline void VideoDto::fromJson(const Json::Value& json) {

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

    }

}
#endif //GNPAPI_NEWSPAPERDTO_H