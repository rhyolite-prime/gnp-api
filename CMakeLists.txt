cmake_minimum_required(VERSION 3.11)
cmake_policy(VERSION 3.5)
project(GnpApi CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options("-include" "${CMAKE_CURRENT_SOURCE_DIR}/utils/codecvt_helper.h" "-Wno-deprecated-declarations")

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-fcoroutines)
endif()

add_executable(${PROJECT_NAME} main.cc
        services/users/UserService.h
        services/users/UserService.cc
        dto/BaseApiResponse.h
        services/subscriptions/SubscriptionService.h
        services/subscriptions/SubscriptionService.cc
        services/publications/PublicationService.h
        services/publications/PublicationService.cc
        services/newspapers/NewspaperService.cc
        services/newspapers/NewspaperService.h
        dto/PublicationDto.h
        constants/ErrorCodes.h
        constants/PublicationTypes.h
        dto/UserDto.h
        services/paystack/PaystackApi.h
        services/hubtel_sms/HubtelSmsApi.h
        dto/SigninDto.h
        services/subscription_plans/SubscriptionPlanService.h
        dto/SubscriptionPlanDto.h
        services/subscription_plans/SubscriptionPlanService.cc
        dto/IngestNewsPaperDto.h
        services/email/EmailService.h
        services/email/EmailService.cc
        dto/SendEmailDto.h
        dto/GuestSubscriptionDto.h
        dto/InitializePaymentRequest.h
        dto/VerifyPayResponse.h
        dto/InitializePaymentResponse.h
        services/paystack/PaystackApi.cc
        dto/GuestOnetimeBuyDto.h
        services/campaigns/CampaignService.h
        services/payments/PaymentService.h
        dto/CreateCampaignDto.h
        dto/CreatePaymentDto.h
        services/campaigns/CampaignService.cc
        services/payments/PaymentService.cc
        services/ingestion_jobs/IngestionJobService.h
        dto/IngestJobDto.h
        services/ingestion_jobs/IngestionJobService.cc
        services/partners/CommercialPartnerService.h
        services/partners/CommercialPartnerService.cc
        dto/CreatePartnerDto.h
        dto/PartnerInvoiceDto.h
        dto/UpdatePartnerDto.h
        dto/RegisterUserPasskeysDto.h
        dto/CreatePartnerSubscriberDto.h
        utils/PasswordUtils.h
        utils/PasswordUtils.cc
        utils/IdGeneratorUtils.h
        utils/IdGeneratorUtils.cc
        utils/StringUtils.h
        utils/StringUtils.cc
        dto/AssignPartnerSubscriberPlanDto.h
        dto/GrantNewsPaperAccessDto.h
        filters/JwtAuthFilter.h
        controllers/PartnerApiController.h
        constants/PartnerApiScopes.h
        services/hubtel_sms/HubtelSmsApi.cc
        controllers/CampaignsController.h
        services/quartz/QuartzApi.h
        services/quartz/QuartzApi.cc
        services/affiliates/AffiliateService.h
        services/affiliates/AffiliateService.cc
        dto/AffiliateDto.h
        controllers/AffiliateController.cc
        services/coupons/CouponService.h
        services/coupons/CouponService.cc
        dto/CreateCouponDto.h
        dto/UpdateCouponDto.h
        services/mtn_api/MtnMomoApi.h
        dto/AccessTokenDto.h
        services/media/MediaService.h
        services/media/MediaService.cc
        dto/MediaMetadataDto.h
        controllers/PartnerController.h
        filters/PartnerJwtAuthFilter.h
        dto/UpdatePartnerSubscriberDto.h
        services/roles/RoleService.h
        services/roles/RoleService.h
        services/roles/RoleService.h
        dto/RoleDto.h
        services/roles/RoleService.cc
        dto/VerifyPartnerUserOtpDto.h
        services/partner_invoice/PartnerInvoiceService.h
        services/partner_invoice/PartnerInvoiceService.cc
        dto/AdminUserDto.h
        services/user_invitations/AdminUserInvitationService.h
        dto/AdminUserInvitationDto.h
        services/user_invitations/AdminUserInvitationService.cc
        services/content/ContentService.h
        services/content/ContentService.cc
        services/categories/CategoryService.h
        services/categories/CategoryService.cc
        services/tags/TagService.h
        services/tags/TagService.cc
        services/dashboard/DashboardService.h
        services/dashboard/DashboardService.cc
        services/bookmarks/BookmarkService.h
        services/bookmarks/BookmarkService.cc
        services/saved_searches/SavedSearchService.h
        services/saved_searches/SavedSearchService.cc
        services/support_tickets/SupportTicketService.h
        services/support_tickets/SupportTicketService.cc
        services/plugins/PluginService.h
        services/plugins/PluginService.cc
        dto/SavedSearchDto.h
        dto/TagDto.h
        dto/CategoryDto.h
        services/audit_logs/AuditLogService.h
        dto/AuditLogDto.h
        dto/UserIdentityDto.h
        services/audit_logs/AuditLogService.cc
        dto/OcrIngestionDto.h
        utils/codecvt_helper.h
        dto/MtnBroadBandCallbackResponseDto.h
        services/partner_user_logs/PartnerUserLogService.h
        services/partner_api_request_logs/PartnerApiLogService.h
        services/partner_api_request_logs/PartnerApiLogService.cc
        services/partner_user_logs/PartnerUserLogService.cc
        services/redis/RedisCacheManager.h
        services/redis/RedisCacheManager.cc
        services/reports/ReportingService.h
        services/g3_storage/G3StorageService.h
        services/g3_storage/G3StorageService.cc
        constants/StatusTypes.h
        dto/NewsPaperDto.h
        utils/CronUtils.h
        utils/CronUtils.cc
        services/telegram/TelegramService.h
        dto/PartnerQuotaDto.h
        dto/ChangePasswordDto.h
        services/settings/SettingService.h
        dto/SettingsDto.h
        services/settings/SettingService.cc
        constants/NotificationTypes.h
        dto/ArticleDto.h
        dto/ReportDto.h
        dto/AffiliateSignupDto.h
        utils/TimeUtils.h
        utils/TimeUtils.cc
        dto/AffiliateSettingsDto.h
        services/games/GameManager.h
        services/games/GameManager.cc
        services/rag/RagManager.h
        services/rag/RagManager.cc
        services/telegram/TelegramService.cc
        dto/ActivateDeactivateSubscriber.h
        dto/UserEngagementDto.h
        services/joomla_api/JoomlaApi.h
        services/joomla_api/JoomlaApi.cc
        dto/JoomlaApiArticleResponse.h
        services/permissions/PermissionService.h
        services/permissions/PermissionService.cc
)

# ##############################################################################
# If you include the drogon source code locally in your project, use this method
# to add drogon 
# add_subdirectory(drogon) 
# target_link_libraries(${PROJECT_NAME} PRIVATE drogon)
#
# and comment out the following lines
find_package(Drogon CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

find_path(MUPDF_INCLUDE_DIR mupdf/fitz.h PATHS /opt/homebrew/include /usr/local/include /usr/include)
find_library(MUPDF_LIBRARY NAMES mupdf PATHS /opt/homebrew/lib /usr/local/lib /usr/lib /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu)
find_library(MUPDF_THIRD_LIBRARY NAMES mupdf-third PATHS /opt/homebrew/lib /usr/local/lib /usr/lib /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu)

if(MUPDF_INCLUDE_DIR AND MUPDF_LIBRARY)
    set(MUPDF_LIBRARIES ${MUPDF_LIBRARY})
    if(MUPDF_THIRD_LIBRARY)
        list(APPEND MUPDF_LIBRARIES ${MUPDF_THIRD_LIBRARY})
    endif()
    
    if (UNIX AND NOT APPLE)
        # On Linux (like Ubuntu), libmupdf.a relies on system libraries instead of bundled mupdf-third
        list(APPEND MUPDF_LIBRARIES freetype harfbuzz openjp2 jpeg jbig2dec gumbo mujs)
    endif()

    set(MUPDF_INCLUDE_DIRS ${MUPDF_INCLUDE_DIR})
    message(STATUS "Found MuPDF: ${MUPDF_LIBRARIES}")
else()
    message(FATAL_ERROR "MuPDF not found!")
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
        Drogon::Drogon
        bcrypt
        jwt-cpp
        OpenSSL::SSL
        OpenSSL::Crypto
        ${MUPDF_LIBRARIES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
        ${MUPDF_INCLUDE_DIRS}
)


# ##############################################################################

if (CMAKE_CXX_STANDARD LESS 17)
    message(FATAL_ERROR "c++17 or higher is required")
elseif (CMAKE_CXX_STANDARD LESS 20)
    message(STATUS "use c++17")
else ()
    message(STATUS "use c++20")
endif ()

aux_source_directory(controllers CTL_SRC)
aux_source_directory(filters FILTER_SRC)
aux_source_directory(plugins PLUGIN_SRC)
aux_source_directory(constants CONSTANTS_SRC)
aux_source_directory(models MODEL_SRC)
aux_source_directory(utils UTILS_SRC)

drogon_create_views(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/views
                    ${CMAKE_CURRENT_BINARY_DIR})
# use the following line to create views with namespaces.
# drogon_create_views(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/views
#                     ${CMAKE_CURRENT_BINARY_DIR} TRUE)
# use the following line to create views with namespace CHANGE_ME prefixed
# and path namespaces.
# drogon_create_views(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/views
#                     ${CMAKE_CURRENT_BINARY_DIR} TRUE CHANGE_ME)


# Update the include directory for jwt-cpp
target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/external_deps/jwt-cpp/include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/models
)


target_sources(${PROJECT_NAME}
               PRIVATE
               ${SRC_DIR}
               ${CTL_SRC}
               ${FILTER_SRC}
               ${PLUGIN_SRC}
               ${CONSTANTS_SRC}
               ${SERVICE_SRC}
               ${MODEL_SRC}
               ${UTILS_SRC})
# ##############################################################################
# uncomment the following line for dynamically loading views 
# set_property(TARGET ${PROJECT_NAME} PROPERTY ENABLE_EXPORTS ON)

# ##############################################################################

#target_compile_options(${PROJECT_NAME} PRIVATE "-include" "${CMAKE_CURRENT_SOURCE_DIR}/utils/codecvt_helper.h" "-Wno-deprecated-declarations")



add_subdirectory(test)
add_subdirectory(external_deps/Bcrypt)

# Disable jwt-cpp examples and tests to avoid a duplicate target error:
# jwt-cpp's example/traits/CMakeLists.txt calls find_package(jsoncpp),
# which re-creates the JsonCpp::JsonCpp imported target that Drogon already
# defined, causing CMake to abort with "another target with the same name
# already exists".
set(JWT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JWT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(external_deps/jwt-cpp)

