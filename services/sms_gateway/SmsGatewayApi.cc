//
// Created by Emmanuel Addo-Odame on 11/10/2026.
//
#include "SmsGatewayApi.h"
#include <cstdlib>

namespace gnp::services {


    drogon::Task<bool> SmsGatewayApi::sendSms(
        const std::vector<std::string> &recipientList,
        const std::string &messageContent) {

        // Guard: nothing to send if the list is empty.
        if (recipientList.empty()) {
            LOG_WARN << "sendSms called with an empty recipient list; skipping.";
            co_return false;
        }

        // Build the request JSON payload.
        Json::Value payload;
        Json::Value toArray(Json::arrayValue);
        for (const auto &recipient : recipientList) {
            toArray.append(recipient);
        }
        payload["to"] = toArray;
        payload["message"] = messageContent;
        payload["device_id"] = "4ba63614-69e9-48d3-8626-603408e0ba8f";
        payload["sim_slot"] = 1;  // 0 = SIM 1, 1 = SIM 2
        payload["ttl"] = 3600;

        auto client = drogon::HttpClient::newHttpClient(
            "https://dev-api.graphicnewsplus.com");

        auto req = drogon::HttpRequest::newHttpJsonRequest(payload);
        req->setMethod(drogon::Post);
        req->setPath("/api/v1/messages");
        // Load the API key from the environment — never hardcode secrets.
        const char* apiKey = std::getenv("SMS_GATEWAY_API_KEY");
        if (!apiKey || std::string(apiKey).empty()) {
            LOG_ERROR << "SMS_GATEWAY_API_KEY environment variable is not set.";
            co_return false;
        }
        req->addHeader("Authorization", "Bearer " + std::string(apiKey));
        req->addHeader("Content-Type", "application/json");
        req->addHeader("Accept", "application/json");

        // sendRequestCoro yields a plain HttpResponsePtr in the coroutine API.
        // Network / timeout failures are reported via exceptions, not a result code.
        drogon::HttpResponsePtr response;
        try {
            response = co_await client->sendRequestCoro(req, 10.0);
        } catch (const drogon::HttpException &e) {
            LOG_ERROR << "SMS gateway request failed ("
                      << drogon::to_string(e.code()) << "): " << e.what();
            co_return false;
        } catch (const std::exception &e) {
            LOG_ERROR << "SMS gateway request failed: " << e.what();
            co_return false;
        }

        if (response == nullptr) {
            LOG_ERROR << "SMS gateway returned a null response.";
            co_return false;
        }

        auto status = response->getStatusCode();
        if (status != drogon::k200OK &&
            status != drogon::k201Created &&
            status != drogon::k202Accepted) {
            LOG_ERROR << "SMS gateway returned HTTP "
                      << static_cast<int>(status)
                      << " body: " << response->getBody();
            co_return false;
        }

        // Optionally validate the response JSON.
        auto json = response->getJsonObject();
        if (json && (*json).isMember("messages")) {
            LOG_INFO << "SMS queued for "
                     << (*json)["messages"].size() << " recipient(s)";
        }

        co_return true;
    }


}
