#pragma once

#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

using namespace drogon;

class AdminController : public drogon::HttpController<AdminController> {
public:
  static constexpr const char *PREFIX = "/api/v1/admin/";
  METHOD_LIST_BEGIN

  ADD_METHOD_TO(AdminController::getDashboardData, std::string(PREFIX) + "get-dashboard-data", Get, Options, "JwtAuthFilter");

  // publications
  ADD_METHOD_TO(AdminController::getAllPublications, std::string(PREFIX) + "get-all-publications", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::createPublication, std::string(PREFIX) + "create-publication", Post, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::updatePublication, std::string(PREFIX) + "update-publication", Put, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::deletePublication, std::string(PREFIX) + "delete-publication", Delete, Options, "JwtAuthFilter");

  // newspaper
  ADD_METHOD_TO(AdminController::getAllNewsPapers,std::string(PREFIX) + "get-all-newspapers", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::getAllArchivedNewsPapers,std::string(PREFIX) + "get-all-archived-newspapers", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::getNewsPaperDetails, std::string(PREFIX) + "get-newspaper-details/{1}", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::publishNewsPaper, std::string(PREFIX) + "publish-newspaper", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::unPublishNewsPaper, std::string(PREFIX) + "unpublish-newspaper", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::IngestNewsPaper, std::string(PREFIX) + "ingest-newspaper", Post, Options, "JwtAuthFilter");

  ADD_METHOD_TO(AdminController::updateNewsPaper, std::string(PREFIX) + "update-newspaper/{1}", Put, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::deleteNewsPaper, std::string(PREFIX) + "delete-newspaper", Delete, Options, "JwtAuthFilter");
  // manage admin users
  ADD_METHOD_TO(AdminController::getAllAdminUsers, std::string(PREFIX) + "get-all-admin-users", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::getUserDetails, std::string(PREFIX) + "get-user-details", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::lockUserAccount, std::string(PREFIX) + "lock-admin-user-account", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::unLockUserAccount, std::string(PREFIX) + "unlock-admin-user-account", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::activateAdminUser, std::string(PREFIX) + "activate-admin-user/{1}", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::deactivateAdminUser, std::string(PREFIX) + "deactivate-admin-user/{1}", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::createAdminUser, std::string(PREFIX) + "create-admin-user", Post, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::updateAdminUser, std::string(PREFIX) + "update-admin-user/{1}", Post, Options, "JwtAuthFilter");
  //ADD_METHOD_TO(AdminController::updateUserProfileImage, std::string(PREFIX) + "update-profile-image", Post, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::deleteAdminUser, std::string(PREFIX) + "delete-admin-user/{1}", Delete, Options, "JwtAuthFilter");

  // subscription plans
  ADD_METHOD_TO(AdminController::getAllSubscriptionPlans, std::string(PREFIX) + "get-all-subscription-plans", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::createSubscriptionPlan, std::string(PREFIX) + "create-subscription-plan", Post, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::updateSubscriptionPlan, std::string(PREFIX) + "update-subscription-plan/{1}", Post, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::deleteSubscriptionPlan, std::string(PREFIX) + "delete-subscription-plan/{1}", Delete, Options, "JwtAuthFilter");

  // user subscription
  ADD_METHOD_TO(AdminController::getAllUserSubscriptions, std::string(PREFIX) + "get-all-subscriptions", Get, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::getUserSubscriptionDetails, std::string(PREFIX) + "get-user-subscription-details", Get, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::renewUserSubscription, std::string(PREFIX) + "renew-user-subscription", Post, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::getPartnerSubscriberInfo, std::string(PREFIX) + "get-partner-subscriber-info/{1}/{2}", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::getSubscriberInfo, std::string(PREFIX) + "get-subscriber-info/{1}", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::assignSubscriptionPlanToSubscriber, std::string(PREFIX) + "assign-subscriber-plan", Post, Options, "JwtAuthFilter");
  
  // campaigns
  ADD_METHOD_TO(AdminController::getAllCampaigns, std::string(PREFIX) + "get-all-campaigns", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::createCampaign, std::string(PREFIX) + "create-campaign", Post, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::publishCampaign, std::string(PREFIX) + "publish-campaign", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::deleteCampaign, std::string(PREFIX) + "delete-campaign", Get, Options, "JwtAuthFilter");

  // commercial partners
  ADD_METHOD_TO(AdminController::getAllPartners,  std::string(PREFIX) + "get-all-partners", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::getPartnerSubscribers, std::string(PREFIX) + "get-partner-subscribers", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::getPartnerSubscriptionSummary, std::string(PREFIX) + "get-partner-subscription-summary", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::getPartnerStats, std::string(PREFIX) + "get-partner-stats", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::getPartnerDetails, std::string(PREFIX) + "get-partner-details", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::createPartner, std::string(PREFIX) + "create-partner", Post, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::createPartnerSubscriber, std::string(PREFIX) + "create-partner-subscriber", Post, Options, "JwtAuthFilter");

  ADD_METHOD_TO(AdminController::uploadPartnerSubscribers, std::string(PREFIX) + "upload-partner-subscribers/{1}", Post, Options, "JwtAuthFilter");

  ADD_METHOD_TO(AdminController::updatePartnerQuota, std::string(PREFIX) + "update-partner-quota/{1}", Post, Options, "JwtAuthFilter");

 ADD_METHOD_TO(AdminController::resetPartnerSubscriberPasswords, std::string(PREFIX) + "reset-partner-subscriber-passwords/{1}", Post, Options, "JwtAuthFilter");

  ADD_METHOD_TO(AdminController::resetPartnerSubscriberPasswordByUserId, std::string(PREFIX) + "reset-partner-subscriber-password/{1}/{2}", Get, Options, "JwtAuthFilter");

  ADD_METHOD_TO(AdminController::activateDeactivatePartnerSubscriber, std::string(PREFIX) + "update-partner-subscriber-status", Post, Options, "JwtAuthFilter");

  ADD_METHOD_TO(AdminController::assignPartnerSubscribersPlan, std::string(PREFIX) + "assign-partner-subscribers-plan", Post, Options, "JwtAuthFilter");

  ADD_METHOD_TO(AdminController::updatePartner, std::string(PREFIX) + "update-partner", Post, Options, "JwtAuthFilter");

  ADD_METHOD_TO(AdminController::updatePartnerStatus, std::string(PREFIX) + "update-partner-status", Get, Options, "JwtAuthFilter");

  ADD_METHOD_TO(AdminController::deletePartner, std::string(PREFIX) + "delete-partner", Delete, Options, "JwtAuthFilter");

  ADD_METHOD_TO(AdminController::deletePartnerSubscriber, std::string(PREFIX) + "delete-partner-subscriber", Delete, Options, "JwtAuthFilter");

  ADD_METHOD_TO(AdminController::enablePartnerSubaccount, std::string(PREFIX) + "enable-partner-subaccount", Get, Options, "JwtAuthFilter");

  ADD_METHOD_TO(AdminController::disablePartnerSubaccount, std::string(PREFIX) + "disable-partner-subaccount", Get, Options, "JwtAuthFilter");

  ADD_METHOD_TO(AdminController::getPartnerApiKeys, std::string(PREFIX) + "get-partner-api-keys", Get, Options, "JwtAuthFilter");

  ADD_METHOD_TO(AdminController::generatePartnerApiKey, std::string(PREFIX) + "generate-partner-api-key", Post, Options, "JwtAuthFilter");

  ADD_METHOD_TO(AdminController::updatePartnerApiKey, std::string(PREFIX) + "update-partner-api-key", Post, Options, "JwtAuthFilter");

  ADD_METHOD_TO(AdminController::revokePartnerApiKey, std::string(PREFIX) + "revoke-partner-api-key", Delete, Options, "JwtAuthFilter");

  // partner api request logs
  ADD_METHOD_TO(AdminController::getPartnerApiRequestLogs, std::string(PREFIX) + "get-partner-api-request-logs", Get, Options, "JwtAuthFilter");

  // payments
  ADD_METHOD_TO(AdminController::getAllPayments, std::string(PREFIX) + "get-all-payments", Get, Options, "JwtAuthFilter");

  // ingestion jobs
  ADD_METHOD_TO(AdminController::getAllIngestionJobs, std::string(PREFIX) + "get-all-ingestion-jobs", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::createIngestionJob, std::string(PREFIX) + "create-ingestion-job", Post, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::deleteIngestionJob, std::string(PREFIX) + "delete-ingestion-job", Get, Options, "JwtAuthFilter");

   // subscribers
   ADD_METHOD_TO(AdminController::getAllSubscribers, std::string(PREFIX) + "get-subscribers", Get, Options, "JwtAuthFilter");
   ADD_METHOD_TO(AdminController::createSubscriber, std::string(PREFIX) + "create-subscriber", Post, Options, "JwtAuthFilter");
   ADD_METHOD_TO(AdminController::updateSubscriber, std::string(PREFIX) + "update-subscriber/{1}", Put, Options, "JwtAuthFilter");
   ADD_METHOD_TO(AdminController::updateSubscriberStatus, std::string(PREFIX) + "update-subscriber-status", Put, Options, "JwtAuthFilter");
   ADD_METHOD_TO(AdminController::deleteSubscriber, std::string(PREFIX) + "delete-subscriber/{1}", Delete, Options, "JwtAuthFilter");
   ADD_METHOD_TO(AdminController::resetSubscriberPassword, std::string(PREFIX) + "reset-subscriber-password/{1}", Get, Options, "JwtAuthFilter");

    // coupon
    ADD_METHOD_TO(AdminController::getAllCoupons, std::string(PREFIX) + "get-all-coupons", Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(AdminController::createCoupon, std::string(PREFIX) + "create-coupon", Post, Options, "JwtAuthFilter");
    ADD_METHOD_TO(AdminController::updateCoupon, std::string(PREFIX) + "update-coupon", Post, Options, "JwtAuthFilter");
    ADD_METHOD_TO(AdminController::deleteCoupon, std::string(PREFIX) + "delete-coupon", Delete, Options, "JwtAuthFilter");

    //admin roles
    ADD_METHOD_TO(AdminController::getAllRoles, std::string(PREFIX) + "get-all-roles", Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(AdminController::getAllPermissions, std::string(PREFIX) + "get-all-permissions", Get, Options, "JwtAuthFilter");
    ADD_METHOD_TO(AdminController::createRole, std::string(PREFIX) + "create-role", Post, Options, "JwtAuthFilter");
    ADD_METHOD_TO(AdminController::updateRole, std::string(PREFIX) + "update-role/{1}", Post, Options, "JwtAuthFilter");
    ADD_METHOD_TO(AdminController::deleteRole, std::string(PREFIX) + "delete-role/{1}", Delete, Options, "JwtAuthFilter");

   // user groups
   ADD_METHOD_TO(AdminController::getAllUserGroups, std::string(PREFIX) + "get-all-user-groups", Get, Options, "JwtAuthFilter");
   ADD_METHOD_TO(AdminController::createUserGroup, std::string(PREFIX) + "create-user-group", Post, Options, "JwtAuthFilter");
   ADD_METHOD_TO(AdminController::updateUserGroup, std::string(PREFIX) + "update-user-group/{1}", Post, Options, "JwtAuthFilter");
   ADD_METHOD_TO(AdminController::deleteUserGroup, std::string(PREFIX) + "delete-user-group/{1}", Delete, Options, "JwtAuthFilter");

   // partner invoices
   ADD_METHOD_TO(AdminController::getAllPartnerInvoices, std::string(PREFIX) + "get-all-partner-invoices", Get, Options, "JwtAuthFilter");
   ADD_METHOD_TO(AdminController::getPartnerInvoiceStats, std::string(PREFIX) + "get-partner-invoice-stats", Get, Options, "JwtAuthFilter");
   ADD_METHOD_TO(AdminController::createPartnerInvoice, std::string(PREFIX) + "create-partner-invoice", Post, Options, "JwtAuthFilter");
   ADD_METHOD_TO(AdminController::markPartnerInvoicePaid, std::string(PREFIX) + "mark-partner-invoice-paid", Get, Options, "JwtAuthFilter");
   ADD_METHOD_TO(AdminController::deletePartnerInvoice, std::string(PREFIX) + "delete-partner-invoice", Delete, Options, "JwtAuthFilter");

  //affiliates
  ADD_METHOD_TO(AdminController::getAllAffiliates, std::string(PREFIX) + "get-all-affiliates", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::getAffiliateApplicants, std::string(PREFIX) + "get-affiliate-applicants", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::getAffiliateCommissions, std::string(PREFIX) + "get-affiliate-commissions", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::getAffiliatePayouts, std::string(PREFIX) + "get-affiliate-payouts", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::getAffiliateProgramSettings, std::string(PREFIX) + "get-affiliate-program-settings", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::createAffiliateProgramSettings, std::string(PREFIX) + "create-affiliate-program-settings", Post, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::getOverallAffiliateStats, std::string(PREFIX) + "get-affiliate-stats", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::getAffiliateAccountStats, std::string(PREFIX) + "get-affiliate-stats/{1}", Get, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::createAffiliate, std::string(PREFIX) + "create-affiliate", Post, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::updateAffiliate, std::string(PREFIX) + "update-affiliate/{1}", Post, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::updateAffiliateProfileImage, std::string(PREFIX) + "update-affiliate-profile-image/{1}", Post, Options, "JwtAuthFilter");
  ADD_METHOD_TO(AdminController::deleteAffiliate, std::string(PREFIX) + "delete-affiliate", Delete, Options, "JwtAuthFilter");

   // archive RAG indexing maintenance
   ADD_METHOD_TO(AdminController::indexArchiveRag, std::string(PREFIX) + "rag/index-pending", Post, Options, "JwtAuthFilter");

   // settings
   ADD_METHOD_TO(AdminController::manageSettings, std::string(PREFIX) + "manage-settings", Post, Options, "JwtAuthFilter");

   // scheduled actions
   ADD_METHOD_TO(AdminController::regenerateNewspaperEntitlements, std::string(PREFIX) + "regenerate-newspaper-entitlements", Post, Options);
   ADD_METHOD_TO(AdminController::generatePartnerInvoices, std::string(PREFIX) + "generate-partner-invoices/{1}", Post, Options);
   ADD_METHOD_TO(AdminController::dispatchDailyNewsUpdate, std::string(PREFIX) + "dispatch-daily-news-update/{1}", Post, Options);
   ADD_METHOD_TO(AdminController::dispatchSubscriptionRenewalReminder, std::string(PREFIX) + "dispatch-subscription-renewal-reminder", Post, Options);

  METHOD_LIST_END

 //dashboard
 drogon::Task<HttpResponsePtr> getDashboardData(HttpRequestPtr req);

  // subscription plans ...
 drogon::Task<HttpResponsePtr> getAllSubscriptionPlans(HttpRequestPtr req);
 drogon::Task<HttpResponsePtr> createSubscriptionPlan(HttpRequestPtr req);
 drogon::Task<HttpResponsePtr> updateSubscriptionPlan(HttpRequestPtr req, const std::string &id);
 drogon::Task<HttpResponsePtr> deleteSubscriptionPlan(HttpRequestPtr req, const std::string &id);


  // Newspapers
  drogon::Task<HttpResponsePtr> getAllNewsPapers(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> getAllArchivedNewsPapers(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> getNewsPaperDetails(HttpRequestPtr req, const std::string &newspaperId);
  drogon::Task<HttpResponsePtr> publishNewsPaper(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> unPublishNewsPaper(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> IngestNewsPaper(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> updateNewsPaper(HttpRequestPtr req, const std::string &newspaperId);
  drogon::Task<HttpResponsePtr> deleteNewsPaper(HttpRequestPtr req);

  // manage administrative users
  drogon::Task<HttpResponsePtr> getAllAdminUsers(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> createAdminUser(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> updateAdminUser(HttpRequestPtr req, const std::string &userId);
  drogon::Task<HttpResponsePtr> activateAdminUser(HttpRequestPtr req, const std::string &userId);
  drogon::Task<HttpResponsePtr> deactivateAdminUser(HttpRequestPtr req, const std::string &userId);
  drogon::Task<HttpResponsePtr> getUserDetails(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> deleteAdminUser(HttpRequestPtr req, const std::string &userId);
  drogon::Task<HttpResponsePtr> lockUserAccount(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> unLockUserAccount(HttpRequestPtr req);



  // publications ...

  Task<HttpResponsePtr> getAllPublications(HttpRequestPtr req);
  Task<HttpResponsePtr> createPublication(HttpRequestPtr req);
  Task<HttpResponsePtr> updatePublication(HttpRequestPtr req, const std::string &publicationId);
  Task<HttpResponsePtr> activate(HttpRequestPtr req, const std::string &publicationId);
  Task<HttpResponsePtr> deactivate(HttpRequestPtr req, const std::string &publicationId);
  Task<HttpResponsePtr> deletePublication(HttpRequestPtr req, const std::string &publicationId);

  // user subscription...
  void getAllUserSubscriptions(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);
  void getUserSubscriptionDetails(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);
  void renewUserSubscription(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);

  // campaigns ...
  drogon::Task<HttpResponsePtr> getAllCampaigns(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> createCampaign(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> publishCampaign(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> deleteCampaign(HttpRequestPtr req);

  // commercial partners ...
  drogon::Task<HttpResponsePtr> getAllPartners(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> getPartnerSubscribers(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> getPartnerSubscriptionSummary(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> getPartnerStats(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> createPartner(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> createPartnerSubscriber(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> uploadPartnerSubscribers(HttpRequestPtr req, const std::string &partnerId);
  drogon::Task<HttpResponsePtr> updatePartnerQuota(HttpRequestPtr req, const std::string &partnerId);
  drogon::Task<HttpResponsePtr> resetPartnerSubscriberPasswords(HttpRequestPtr req, const std::string &partnerId);
  drogon::Task<HttpResponsePtr> resetPartnerSubscriberPasswordByUserId(HttpRequestPtr req, const std::string &partnerId, const std::string &userId);
  drogon::Task<HttpResponsePtr> activateDeactivatePartnerSubscriber(HttpRequestPtr req);


  //affiliates
 drogon::Task<HttpResponsePtr> getAllAffiliates(HttpRequestPtr req);
 drogon::Task<HttpResponsePtr> getAffiliateApplicants(HttpRequestPtr req);
 drogon::Task<HttpResponsePtr> getAffiliateCommissions(HttpRequestPtr req);
 drogon::Task<HttpResponsePtr> getAffiliatePayouts(HttpRequestPtr req);
 drogon::Task<HttpResponsePtr> getAffiliateProgramSettings(HttpRequestPtr req);
 drogon::Task<HttpResponsePtr> createAffiliateProgramSettings(HttpRequestPtr req);
 drogon::Task<HttpResponsePtr> getOverallAffiliateStats(HttpRequestPtr req);
 drogon::Task<HttpResponsePtr> getAffiliateAccountStats(HttpRequestPtr req, const std::string &affiliateId);
 drogon::Task<HttpResponsePtr> createAffiliate(HttpRequestPtr req);
 drogon::Task<HttpResponsePtr> updateAffiliate(HttpRequestPtr req, const std::string &affiliateId);
 drogon::Task<HttpResponsePtr> updateAffiliateProfileImage(HttpRequestPtr req, const std::string &affiliateId);
 drogon::Task<HttpResponsePtr> deleteAffiliate(HttpRequestPtr req);

  drogon::Task<HttpResponsePtr> assignPartnerSubscribersPlan(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> updatePartner(HttpRequestPtr req);
  void getPartnerDetails(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);
  void updatePartnerStatus(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);
  drogon::Task<HttpResponsePtr> deletePartner(HttpRequestPtr req);

  drogon::Task<HttpResponsePtr> deletePartnerSubscriber(HttpRequestPtr req);

  void enablePartnerSubaccount(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);
  void disablePartnerSubaccount(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);

  Task<HttpResponsePtr> getPartnerApiKeys(HttpRequestPtr req);
  Task<HttpResponsePtr> generatePartnerApiKey(HttpRequestPtr req);
  Task<HttpResponsePtr> revokePartnerApiKey(HttpRequestPtr req);
  Task<HttpResponsePtr> updatePartnerApiKey(HttpRequestPtr req);
  Task<HttpResponsePtr> getPartnerApiRequestLogs(HttpRequestPtr req);

  //payments
  void getAllPayments(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);

  //ingestion Jobs
  void getAllIngestionJobs(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);
  void createIngestionJob(const HttpRequestPtr &req,  std::function<void(const HttpResponsePtr &)> &&callback);
  void deleteIngestionJob(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback);

  // coupons
  drogon::Task<HttpResponsePtr> getAllCoupons(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> createCoupon(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> updateCoupon(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> deleteCoupon(HttpRequestPtr req);

  // subscribers
  drogon::Task<HttpResponsePtr> getAllSubscribers(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> createSubscriber(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> updateSubscriber(HttpRequestPtr req, std::string subscriberId);
  drogon::Task<HttpResponsePtr> updateSubscriberStatus(HttpRequestPtr req);
  drogon::Task<HttpResponsePtr> deleteSubscriber(HttpRequestPtr req, std::string subscriberId);
  drogon::Task<HttpResponsePtr> resetSubscriberPassword(HttpRequestPtr req, std::string subscriberId);
 Task<HttpResponsePtr> assignSubscriptionPlanToSubscriber(HttpRequestPtr req);

   // partner invoices
   drogon::Task<HttpResponsePtr> getAllPartnerInvoices(HttpRequestPtr req);
   drogon::Task<HttpResponsePtr> getPartnerInvoiceStats(HttpRequestPtr req);
   drogon::Task<HttpResponsePtr> createPartnerInvoice(HttpRequestPtr req);
   drogon::Task<HttpResponsePtr> markPartnerInvoicePaid(HttpRequestPtr req);
   drogon::Task<HttpResponsePtr> deletePartnerInvoice(HttpRequestPtr req);

    // admin roles
    Task<HttpResponsePtr> getAllPermissions(HttpRequestPtr req);
    Task<HttpResponsePtr> getAllRoles(HttpRequestPtr req);
    Task<HttpResponsePtr> createRole(HttpRequestPtr req);
    Task<HttpResponsePtr> updateRole(HttpRequestPtr req, const std::string &roleId);
    Task<HttpResponsePtr> deleteRole(HttpRequestPtr req, const std::string &roleId);

   // user groups
   Task<HttpResponsePtr> getAllUserGroups(HttpRequestPtr req);
   Task<HttpResponsePtr> createUserGroup(HttpRequestPtr req);
   Task<HttpResponsePtr> updateUserGroup(HttpRequestPtr req, const std::string &roleId);
   Task<HttpResponsePtr> deleteUserGroup(HttpRequestPtr req, const std::string &roleId);

    Task<HttpResponsePtr> getPartnerSubscriberInfo(HttpRequestPtr req, const std::string &partnerId, const std::string &userId);
    Task<HttpResponsePtr> getSubscriberInfo(HttpRequestPtr req, const std::string &userId);

    // scheduled actions
    Task<HttpResponsePtr> regenerateNewspaperEntitlements(HttpRequestPtr req);
    Task<HttpResponsePtr> generatePartnerInvoices(HttpRequestPtr req, const std::string &date);
    Task<HttpResponsePtr> dispatchDailyNewsUpdate(HttpRequestPtr req, const std::string &deliveryChannel);
    Task<HttpResponsePtr> dispatchSubscriptionRenewalReminder(HttpRequestPtr req);

  // Archive RAG maintenance
  Task<HttpResponsePtr> indexArchiveRag(HttpRequestPtr req);

  // settings
  Task<HttpResponsePtr> manageSettings(HttpRequestPtr req);



};
