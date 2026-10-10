/**
 *
 *  GnpServicePlugin.h
 *
 */

#pragma once

#include <drogon/drogon.h>
#include <drogon/plugins/Plugin.h>

#include "services/affiliates/AffiliateService.h"
#include "services/audit_logs/AuditLogService.h"
#include "services/campaigns/CampaignService.h"
#include "services/categories/CategoryService.h"
#include "services/content/ContentService.h"
#include "services/coupons/CouponService.h"
#include "services/email/EmailService.h"
#include "services/g3_storage/G3StorageService.h"
#include "services/hubtel_sms/HubtelSmsApi.h"
#include "services/ingestion_jobs/IngestionJobService.h"
#include "services/joomla_api/JoomlaApi.h"
#include "services/media/MediaService.h"
#include "services/newspapers/NewspaperService.h"
#include "services/partners/CommercialPartnerService.h"
#include "services/partner_api_request_logs/PartnerApiLogService.h"
#include "services/partner_invoice/PartnerInvoiceService.h"
#include "services/partner_user_logs/PartnerUserLogService.h"
#include "services/payments/PaymentService.h"
#include "services/paystack/PaystackApi.h"
#include "services/permissions/PermissionService.h"
#include "services/publications/PublicationService.h"
#include "services/quartz/QuartzApi.h"
#include "services/redis/RedisCacheManager.h"
#include "services/reports/ReportingService.h"
#include "services/roles/RoleService.h"
#include "services/settings/SettingService.h"
#include "services/subscription_plans/SubscriptionPlanService.h"
#include "services/subscriptions/SubscriptionService.h"
#include "services/tags/TagService.h"
#include "services/telegram/TelegramService.h"
#include "services/games/GameManager.h"
#include "services/users/UserService.h"
#include "services/user_invitations/AdminUserInvitationService.h"
#include "utils/PasswordUtils.h"

namespace gnp::plugins {

class GnpServicePlugin : public drogon::Plugin<GnpServicePlugin> {
public:
  GnpServicePlugin() = default;
  ~GnpServicePlugin() override = default;

  void initAndStart(const Json::Value &config) override;
  void shutdown() override;

  services::RedisCacheManager &getRedisCacheManager() { return redisCacheService_; }
  ::gnp::services::UserService &getUserService() { return userService_; }
  ::gnp::services::EmailService &getEmailService() { return emailService_; }
  ::gnp::services::NewspaperService &getNewsPaperService() { return newspaperService_; }
  ::gnp::services::CommercialPartnerService &getCommercialPartnerService() { return commercialPartnerService_; }
  ::gnp::services::PaymentService &getPaymentService() { return paymentService_; }
  ::gnp::services::IngestionJobService &getIngestionJobService() { return ingestionJobService_; }
  ::gnp::services::CampaignService &getCampaignService() { return campaignService_; }
  ::gnp::services::SubscriptionPlanService &getSubscriptionPlanService() { return subscriptionPlanService_; }
  ::gnp::services::SubscriptionService &getSubscriptionService() { return subscriptionService_; }
  ::gnp::services::PublicationService &getPublicationService() { return publicationService_; }
  ::gnp::services::AffiliateService &getAffiliateService() { return affiliate_service_; }
  ::gnp::services::PaystackApi &getPaystackApi() { return paystackApi_; }
  ::gnp::services::HubtelSmsApi &getHubtelSmsApi() { return hubtelSmsApi_; }
  ::gnp::services::QuartzApi &getQuartzApi() { return quartzApi_; }
  ::gnp::services::MediaService &getMediaService() { return mediaService_; }
  ::gnp::services::CouponService &getCouponService() { return couponService_; }
  ::gnp::services::RoleService &getRoleService() { return roleService_; }
  ::gnp::services::PartnerInvoiceService &getPartnerInvoiceService() { return partnerInvoiceService_; }
  ::gnp::services::AdminUserInvitationService &getAdminUserInvitationService() { return adminUserInvitationService_; }
  ::gnp::services::AuditLogService &getAuditLogService() { return auditLogService_; }
  ::gnp::services::PartnerUserLogService &getPartnerUserActivityLogService() { return partnerUserActivityLogService_; }
  ::gnp::services::PartnerApiLogService &getPartnerApiLogService() { return partnerApiLogService_; }
  ::gnp::services::TagService &getTagService() { return tagService_; }
  ::gnp::services::CategoryService &getCategoryService() { return categoryService_; }
  ::gnp::services::ReportingService &getReportingService() { return reportingService_; }
  ::gnp::services::G3StorageService &getG3StorageService() { return g3StorageService_; }
  ::gnp::services::SettingService &getSettingService() { return settingService_; }
  ::gnp::services::ContentService &getContentService() { return contentService_; }
  ::gnp::services::TelegramService &getTelegramService() { return telegramService_; }
  ::gnp::services::PermissionService &getPermissionService() { return permissionService_; }
  ::gnp::services::JoomlaApi &getJoomlaApi() { return joomlaApi_; }
  ::gnp::services::GameManager &getGameManager() { return gameManager_; }

private:
  services::RedisCacheManager redisCacheService_;
  ::gnp::services::AffiliateService affiliate_service_;
  ::gnp::services::PermissionService permissionService_;
  ::gnp::services::UserService userService_;
  ::gnp::services::EmailService emailService_;
  ::gnp::services::NewspaperService newspaperService_;
  ::gnp::services::CommercialPartnerService commercialPartnerService_;
  ::gnp::services::IngestionJobService ingestionJobService_;
  ::gnp::services::PaymentService paymentService_;
  ::gnp::services::CampaignService campaignService_;
  ::gnp::services::SubscriptionPlanService subscriptionPlanService_;
  ::gnp::services::SubscriptionService subscriptionService_;
  ::gnp::services::PublicationService publicationService_;
  ::gnp::services::PaystackApi paystackApi_;
  ::gnp::services::HubtelSmsApi hubtelSmsApi_;
  ::gnp::services::QuartzApi quartzApi_;
  ::gnp::services::MediaService mediaService_;
  ::gnp::services::CouponService couponService_;
  ::gnp::services::RoleService roleService_;
  ::gnp::services::PartnerInvoiceService partnerInvoiceService_;
  ::gnp::services::AdminUserInvitationService adminUserInvitationService_;
  ::gnp::services::AuditLogService auditLogService_;
  ::gnp::services::TagService tagService_;
  ::gnp::services::ContentService contentService_;
  ::gnp::services::CategoryService categoryService_;
  ::gnp::services::PartnerUserLogService partnerUserActivityLogService_;
  ::gnp::services::PartnerApiLogService partnerApiLogService_;
  ::gnp::services::ReportingService reportingService_;
  ::gnp::services::G3StorageService g3StorageService_;
  ::gnp::services::SettingService settingService_;
  ::gnp::services::TelegramService telegramService_;
  ::gnp::services::JoomlaApi joomlaApi_;
  ::gnp::services::GameManager gameManager_;
};

} // namespace gnp::plugins
