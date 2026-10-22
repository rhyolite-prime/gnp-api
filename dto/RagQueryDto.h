// Retrieval-augmented question DTO for the Graphic Archives.
#pragma once

#include <json/json.h>
#include <string>

namespace gnp::dto {

class RagQueryDto {
public:
  RagQueryDto() = default;
  explicit RagQueryDto(const Json::Value &json) { fromJson(json); }

  void fromJson(const Json::Value &json) {
    question_.clear();
    topK_ = 5;
    dateFrom_.clear();
    dateTo_.clear();
    publicationDate_.clear();
    publicationYear_ = 0;
    publicationId_.clear();
    publicationName_.clear();
    invalidInput_ = false;

    if (!json.isObject()) {
      invalidInput_ = true;
      return;
    }

    if (json.isMember("question") && !json["question"].isNull()) {
      readString(json["question"], question_);
    } else if (json.isMember("query") && !json["query"].isNull()) {
      readString(json["query"], question_);
    } else if (json.isMember("q") && !json["q"].isNull()) {
      readString(json["q"], question_);
    }

    if (json.isMember("topK") && !json["topK"].isNull()) {
      readInteger(json["topK"], topK_);
    } else if (json.isMember("top_k") && !json["top_k"].isNull()) {
      readInteger(json["top_k"], topK_);
    } else if (json.isMember("limit") && !json["limit"].isNull()) {
      readInteger(json["limit"], topK_);
    }

    readOptionalString(json, "dateFrom", dateFrom_);
    readOptionalString(json, "dateTo", dateTo_);
    readOptionalString(json, "publicationDate", publicationDate_);
    readOptionalString(json, "publicationId", publicationId_);
    readOptionalString(json, "publicationName", publicationName_);

    if (json.isMember("publicationYear") && !json["publicationYear"].isNull()) {
      readInteger(json["publicationYear"], publicationYear_);
    }
  }

  [[nodiscard]] const std::string &getQuestion() const { return question_; }
  [[nodiscard]] const std::string &getQuery() const { return question_; }
  [[nodiscard]] int getTopK() const { return topK_; }
  [[nodiscard]] const std::string &getDateFrom() const { return dateFrom_; }
  [[nodiscard]] const std::string &getDateTo() const { return dateTo_; }
  [[nodiscard]] const std::string &getPublicationDate() const { return publicationDate_; }
  [[nodiscard]] int getPublicationYear() const { return publicationYear_; }
  [[nodiscard]] const std::string &getPublicationId() const { return publicationId_; }
  [[nodiscard]] const std::string &getPublicationName() const { return publicationName_; }
  [[nodiscard]] bool hasInvalidInput() const { return invalidInput_; }

  void setQuestion(const std::string &value) { question_ = value; }
  void setQuery(const std::string &value) { question_ = value; }
  void setTopK(int value) { topK_ = value; }
  void setDateFrom(const std::string &value) { dateFrom_ = value; }
  void setDateTo(const std::string &value) { dateTo_ = value; }
  void setPublicationDate(const std::string &value) { publicationDate_ = value; }
  void setPublicationYear(int value) { publicationYear_ = value; }
  void setPublicationId(const std::string &value) { publicationId_ = value; }
  void setPublicationName(const std::string &value) { publicationName_ = value; }

private:
  void readString(const Json::Value &value, std::string &target) {
    if (!value.isString()) {
      invalidInput_ = true;
      return;
    }
    target = value.asString();
  }

  void readInteger(const Json::Value &value, int &target) {
    if (!value.isInt()) {
      invalidInput_ = true;
      return;
    }
    target = value.asInt();
  }

  void readOptionalString(const Json::Value &json,
                          const char *key,
                          std::string &target) {
    if (json.isMember(key) && !json[key].isNull()) {
      readString(json[key], target);
    }
  }

  std::string question_;
  int topK_ = 5;
  std::string dateFrom_;
  std::string dateTo_;
  std::string publicationDate_;
  int publicationYear_ = 0;
  std::string publicationId_;
  std::string publicationName_;
  bool invalidInput_ = false;
};

} // namespace gnp::dto
