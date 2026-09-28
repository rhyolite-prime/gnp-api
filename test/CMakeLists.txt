cmake_minimum_required(VERSION 3.10)
project(GnpApi_test CXX)

file(GLOB CTL_SRC "../controllers/*.cc")
file(GLOB FILTER_SRC "../filters/*.cc")
file(GLOB PLUGIN_SRC "../plugins/*.cc")
file(GLOB CONSTANTS_SRC "../constants/*.cc")
file(GLOB MODEL_SRC "../models/*.cc")
file(GLOB USER_SVC "../services/users/*.cc")
file(GLOB NEWSPAPER_SVC "../services/newspapers/*.cc")
file(GLOB SUB_SVC "../services/subscriptions/*.cc")
file(GLOB PUB_SVC "../services/publications/*.cc")
file(GLOB EMAIL_SVC "../services/email/*.cc")
file(GLOB PAYSTACK_SVC "../services/paystack/*.cc")
file(GLOB HUBTEL_SVC "../services/hubtel_sms/*.cc")
file(GLOB PAYMENT_SVC "../services/payments/*.cc")
file(GLOB CAMPAIGN_SVC "../services/campaigns/*.cc")
file(GLOB QUARTZ_SVC "../services/quartz/*.cc")
file(GLOB AFFILIATE_SVC "../services/affiliates/*.cc")
file(GLOB INGESTION_JOB_SVC "../services/ingestion_jobs/*.cc")
file(GLOB SUB_PLAN_SVC "../services/subscription_plans/*.cc")
file(GLOB PARTNER_SVC "../services/partners/*.cc")
file(GLOB PARTNER_INVOICE_SVC "../services/partner_invoice/*.cc")
file(GLOB PARTNER_USER_LOGS_SVC "../services/partner_user_logs/*.cc")
file(GLOB COUPON_SVC "../services/coupons/*.cc")
file(GLOB PARTNER_API_REQUEST_LOGS_SVC "../services/partner_api_request_logs/*.cc")
file(GLOB ADMIN_USER_INVITATION_SVC "../services/user_invitations/*.cc")
file(GLOB ROLE_SVC "../services/roles/*.cc")
file(GLOB G3_STORAGE_SVC "../services/g3_storage/*.cc")
file(GLOB MEDIA_SVC "../services/media/*.cc")
file(GLOB PERMISSION_SVC "../services/permissions/*.cc")
file(GLOB UTILS_SVC "../utils/*.cc")
file(GLOB JOOMLA_SVC "../services/joomla_api/*.cc")
file(GLOB SETTING_SVC "../services/settings/*.cc")


add_executable(${PROJECT_NAME} test_main.cc
        ${CTL_SRC}
    ${FILTER_SRC}
    ${PLUGIN_SRC}
    ${CONSTANTS_SRC}
    ${MODEL_SRC}
    ${USER_SVC}
    ${NEWSPAPER_SVC}
    ${SUB_SVC}
    ${PUB_SVC}
    ${EMAIL_SVC}
    ${PAYSTACK_SVC}
    ${HUBTEL_SVC}
    ${SUB_PLAN_SVC}
    ${CAMPAIGN_SVC}
    ${QUARTZ_SVC}
    ${AFFILIATE_SVC}
    ${INGESTION_JOB_SVC}
    ${PAYMENT_SVC}
    ${MEDIA_SVC}
    ${COUPON_SVC}
    ${ROLE_SVC}
    ${PARTNER_SVC}
    ${PARTNER_INVOICE_SVC}
    ${G3_STORAGE_SVC}
    ${PARTNER_USER_LOGS_SVC}
    ${PARTNER_API_REQUEST_LOGS_SVC}
    ${ADMIN_USER_INVITATION_SVC}
    ${UTILS_SVC}
    ${JOOMLA_SVC}
    ${PERMISSION_SVC}
    ${SETTING_SVC}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../
    ${CMAKE_CURRENT_SOURCE_DIR}/../external_deps/jwt-cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../models
    ${MUPDF_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Drogon::Drogon
    bcrypt
    jwt-cpp
    OpenSSL::SSL
    OpenSSL::Crypto
    ${MUPDF_LIBRARIES}
)

# ##############################################################################
# If you include the drogon source code locally in your project, use this method
# to add drogon 
# target_link_libraries(${PROJECT_NAME} PRIVATE drogon)
#
# and comment out the following lines


ParseAndAddDrogonTests(${PROJECT_NAME})
