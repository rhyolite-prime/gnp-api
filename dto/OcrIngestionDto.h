//
// Created by Emmanuel Addo-Odame on 06/06/2026.
//

#ifndef GNPAPI_OCRINGESTIONDTO_H
#define GNPAPI_OCRINGESTIONDTO_H
#include <json/json.h>
#include <string>

namespace gnp::dto {

    class OcrIngestionDto {
    public:
        OcrIngestionDto() = default;
        explicit OcrIngestionDto(const Json::Value& json);

        void fromJson(const Json::Value& json);

        // Getters
        [[nodiscard]] const std::string& getId() const { return id_; }
        [[nodiscard]] const std::string& getTitle() const { return title_; }
        [[nodiscard]] const std::string& getEditionNumber() const { return edition_number_; }
        [[nodiscard]] const trantor::Date& getPublicationDate() const { return publication_date_; }
        [[nodiscard]] int getFileType() const { return file_type_; }
        [[nodiscard]] const std::vector<std::string>& getCategories() const { return categories_; }
        [[nodiscard]] const std::vector<std::string>& getTags() const { return tags_; }
        [[nodiscard]] const std::string& getContentType() const { return content_type_; }
        [[nodiscard]] const std::string& getThumbnailId() const { return thumbnail_id_; }
        [[nodiscard]] const std::string& getDocumentId() const { return document_id_; }
        [[nodiscard]] const std::string& getPageSummary() const { return page_summary_; }
        [[nodiscard]] const trantor::Date& getDatePublished() const { return date_published_; }
        [[nodiscard]] const std::string& getPageText() const { return page_text_; }
        [[nodiscard]] int getPageNumber() const { return page_number_; }

        // Setters
        void setId(const std::string& id) { id_ = id; }
        void setTitle(const std::string& title) { title_ = title; }
        void setEditionNumber(const std::string& value) { edition_number_ = value; }
        void setPublicationDate(const trantor::Date& value) { publication_date_ = value; }
        void setFileType(const int file_type) { file_type_ = file_type; }
        void setCategories(const std::vector<std::string> &value) { categories_ = value; }
        void setTags(const std::vector<std::string> &value) { tags_ = value; }
        void setContentType(const std::string& value) { content_type_ = value; }
        void setThumbnailId(const std::string& value) { thumbnail_id_ = value; }
        void setDocumentId(const std::string& value) { document_id_ = value; }
        void setPageSummary(const std::string& value) { page_summary_ = value; }
        void setDatePublished(const trantor::Date& value) { date_published_ = value; }
        void setPageText(const std::string& value) { page_text_ = value; }
        void setPageNumber(const int value) { page_number_ = value; }


    private:

        std::string id_;
        std::string title_;
        std::string edition_number_;
        trantor::Date publication_date_;
        int file_type_ {0};
        std::vector<std::string> categories_;
        std::vector<std::string> tags_;
        std::string content_type_;
        std::string thumbnail_id_;
        std::string document_id_;
        std::string page_summary_;
        trantor::Date date_published_;
        std::string page_text_;
        int page_number_ = 0;

    };

    inline void OcrIngestionDto::fromJson(const Json::Value& json) {

        if (json.isMember("title") && !json["title"].isNull()) {
            title_ = json["title"].asString();
        }

        if (json.isMember("id") && !json["id"].isNull()) {
            id_ = json["id"].asString();
        }

        if (json.isMember("editionNumber") && !json["editionNumber"].isNull()) {
            edition_number_ = json["editionNumber"].asString();
        }

        if (json.isMember("publicationDate") && !json["publicationDate"].isNull()) {

            std::string dateStr = json["publicationDate"].asString();
            if (dateStr.find('T') != std::string::npos) {
                std::replace(dateStr.begin(), dateStr.end(), 'T', ' ');
                if (dateStr.length() == 16) dateStr += ":00";
            } else if (dateStr.length() == 10) {
                dateStr += " 00:00:00";
            }
            publication_date_ = trantor::Date::fromDbString(dateStr);
        }

        if (json.isMember("fileType") && !json["fileType"].isNull()) {
            file_type_ = json["fileType"].asInt();
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

        if (json.isMember("contentType") && !json["contentType"].isNull()) {
            content_type_ = json["contentType"].asString();
        }

        if (json.isMember("thumbnailUrl") && !json["thumbnailUrl"].isNull()) {
            setThumbnailId(json["thumbnailUrl"].asString());
        }

        if (json.isMember("documentId") && !json["documentId"].isNull()) {
            document_id_ = json["documentId"].asString();
        }

        if (json.isMember("pageSummary") && !json["pageSummary"].isNull()) {
            page_summary_ = json["pageSummary"].asString();
        }


        if (json.isMember("datePublished") && !json["datePublished"].isNull()) {
            std::string dateStr = json["datePublished"].asString();
            if (dateStr.find('T') != std::string::npos) {
                std::replace(dateStr.begin(), dateStr.end(), 'T', ' ');
                if (dateStr.length() == 16) dateStr += ":00";
            } else if (dateStr.length() == 10) {
                dateStr += " 00:00:00";
            }
            date_published_ = trantor::Date::fromDbString(dateStr);
        }

        if (json.isMember("pageText") && !json["pageText"].isNull()) {
            page_text_ = json["pageText"].asString();
        }

        if (json.isMember("pageNumber") && !json["pageNumber"].isNull()) {
            page_number_ = json["pageNumber"].asInt();
        }

    }

}
#endif //GNPAPI_OCRINGESTIONDTO_H