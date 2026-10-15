//
// Created by Emmanuel Addo-Odame on 18/09/2026.
//

#include "JoomlaApi.h"

#include <drogon/utils/coroutine.h>
#include "constants/ErrorCodes.h"
#include "dto/BaseApiResponse.h"
#include "dto/JoomlaApiArticleResponse.h"

namespace gnp::services {

    drogon::Task<dto::BaseApiResponse> JoomlaApi::getArticles(int limit, int offset) {

        dto::BaseApiResponse response;

        auto customConfig = drogon::app().getCustomConfig();
        std::string joomlaApiKey = customConfig["JoomlaApiKey"].asString();

        if (joomlaApiKey.empty()) {
            response.success = false;
            response.error["code"] = constants::ERR_INTERNAL;
            response.error["message"] = "Joomla API key is not configured.";
            co_return response;
        }

        auto client =  drogon::HttpClient::newHttpClient("https://www.graphic.com.gh");
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setMethod(drogon::Get);
        req->setPath("/api/index.php/v1/content/articles");
        req->setParameter("page[limit]", std::to_string(limit));
        req->setParameter("page[offset]", std::to_string(offset));
        req->addHeader("Authorization", "Bearer " + joomlaApiKey);
        //  req->addHeader("Accept", "application/json");
        req->addHeader("Accept", "application/vnd.api+json");
        auto resp = co_await client->sendRequestCoro(req);

        if (!resp) {
            response.message = "Network error. Failed to reach the server.";
            co_return response;
        }

        if (resp->getStatusCode() != drogon::k200OK) {
            response.message = "Request failed with status code " + std::to_string(resp->getStatusCode());
            co_return response;
        }

        const std::string responseStr = std::string(resp->getBody());

        Json::Value jsonResponse;
        Json::CharReaderBuilder readerBuilder;
        std::string errs;
        std::istringstream respIss(responseStr);

        if (Json::parseFromStream(readerBuilder, respIss, &jsonResponse, &errs)) {

            auto result = dto::JoomlaApiArticleResponse::fromJson(jsonResponse);

            //LOG_INFO << "[GetJoomlaArticles] response => " << responseStr;

            response.result["data"] = result.toJson()["data"];
            response.success = true;
            response.message = "Articles retrieved successfully.";
            co_return response;
        }

        response.success = false;
        response.message = "Failed to retrieve articles. Please try again.";
        co_return response;

    }


}
