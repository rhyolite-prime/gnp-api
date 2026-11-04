//
// Created by Emmanuel Addo-Odame on 23/10/2026.
//

#ifndef GNPAPI_VIDEOSERVICE_H
#define GNPAPI_VIDEOSERVICE_H
#include <drogon/utils/coroutine.h>

#include "dto/BaseApiResponse.h"
#include "dto/NewsPaperDto.h"

namespace gnp::services {

    class VideoService {
    public:
        drogon::Task<dto::BaseApiResponse> getAll(int pageNo, int pageSize, const std::string &query);
        drogon::Task<dto::BaseApiResponse> getVideoDetails(const std::string &id);
        drogon::Task<dto::BaseApiResponse> create(const dto::VideoDto &dto);
        drogon::Task<dto::BaseApiResponse> update(const dto::VideoDto &dto, const std::string &id);
        drogon::Task<dto::BaseApiResponse> deleteVideo(const std::string &id);



    };

}
#endif //GNPAPI_VIDEOSERVICE_H
