//
// Created by Emmanuel Addo-Odame on 11/10/2026.
//

#ifndef GNPAPI_SMSGATEWAYAPI_H
#define GNPAPI_SMSGATEWAYAPI_H

#include <drogon/drogon.h>
#include <string>
#include <vector>

namespace gnp::services {

    class SmsGatewayApi {
    public:
        drogon::Task<bool> sendSms(const std::vector<std::string> &recipientList, const std::string &messageContent);
    };

}
#endif //GNPAPI_SMSGATEWAYAPI_H
