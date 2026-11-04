#include <drogon/drogon_test.h>

#include "dto/ReportDto.h"
#include "models/AchievementsDefinitions.h"

DROGON_TEST(GeneratedModelUtf8LengthValidation) {
    using drogon_model::Gnp::AchievementsDefinitions;
    Json::Value json;
    std::string error;
    json["name"] = std::string(100, 'a');
    CHECK(AchievementsDefinitions::validateJsonForUpdate(json, error));
    json["name"] = std::string(101, 'a');
    CHECK(!AchievementsDefinitions::validateJsonForUpdate(json, error));

    std::string multibyte;
    for (int i = 0; i < 100; ++i) multibyte += "\xc3\xa9";
    json["name"] = multibyte;
    CHECK(AchievementsDefinitions::validateJsonForUpdate(json, error));
    json["name"] = multibyte + "\xc3\xa9";
    CHECK(!AchievementsDefinitions::validateJsonForUpdate(json, error));
}

DROGON_TEST(ReportCivilDateFormatting) {
    using gnp::dto::ReportFilterDto;
    CHECK(ReportFilterDto::civilFromDays(0) == "1970-01-01");
    CHECK(ReportFilterDto::civilFromDays(-1) == "1969-12-31");
    CHECK(ReportFilterDto::civilFromDays(ReportFilterDto::daysFromCivil(2000, 2, 29)) == "2000-02-29");
    CHECK(ReportFilterDto::civilFromDays(ReportFilterDto::daysFromCivil(2999, 12, 31)) == "2999-12-31");
    int year, month, day;
    CHECK(ReportFilterDto::parseYmd(ReportFilterDto::todayYmd(), year, month, day));
}
