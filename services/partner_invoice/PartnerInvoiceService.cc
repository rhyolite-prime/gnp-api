#include "PartnerInvoiceService.h"
#include "PartnerInvoicePayments.h"
#include "PartnerInvoices.h"
#include "constants/ErrorCodes.h"
#include "utils/IdGeneratorUtils.h"
#include <algorithm>
#include <drogon/orm/Mapper.h>
#include <trantor/utils/Date.h>

#include "plugins/GnpServicePlugin.h"
#include "utils/CronUtils.h"
#include "utils/PartnerInvoiceCalculation.h"
#include "dto/ReportDto.h"

using namespace drogon::orm;
using drogon_model::Gnp::PartnerInvoices;
#include "models/CommercialPartners.h"
#include "models/UserSubscriptions.h"
#include <cmath>
#include <map>

namespace gnp::services {

drogon::Task<dto::BaseApiResponse>
PartnerInvoiceService::getAll(int pageNo, int pageSize,
                              const std::string &query) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<PartnerInvoices> mp(dbClient);

  // 1. Build the search criteria
  Criteria searchCriteria;
  if (!query.empty()) {
    std::string likeQuery = "%" + query + "%";

    searchCriteria = Criteria(PartnerInvoices::Cols::_invoice_number,
                              CompareOperator::Like, likeQuery) ||
                     Criteria(PartnerInvoices::Cols::_description,
                              CompareOperator::Like, likeQuery);
  }

  try {
    size_t totalCount = co_await mp.count(searchCriteria);
    if (totalCount == 0) {
      gnp::dto::BaseApiResponse response;
      response.success = true;
      response.result["data"] = Json::arrayValue;
      response.result["totalCount"] = 0;
      co_return response;
    }

    // 3. Find the paginated data
    int offset = (pageNo - 1) * pageSize;
    auto partnerInvoices =
        co_await mp.limit(pageSize)
            .offset(offset)
            .orderBy(PartnerInvoices::Cols::_created_at, SortOrder::DESC)
            .findBy(searchCriteria);

    // 4. Build the final response
    gnp::dto::BaseApiResponse response;
    auto totalPages = (totalCount + pageSize - 1) / pageSize;

    response.success = true;
    response.result["totalCount"] = (Json::UInt64)totalCount;
    response.result["pageNo"] = pageNo;
    response.result["pageSize"] = pageSize;
    response.result["lowerBound"] = pageSize * (pageNo - 1) + 1;
    response.result["upperBound"] = Json::Value(
        (int)totalPages == pageNo ? (Json::UInt64)totalCount
                                  : (Json::UInt64)(pageNo * pageSize));
    response.result["totalPages"] = (int)totalPages;

    Json::Value data = Json::arrayValue;

    for (const auto &partnerInvoice : partnerInvoices) {
      Json::Value partnerInvoiceJson = partnerInvoice.toJson();

      // Convert snake_case to camelCase
      Json::Value camelCasePartnerInvoice;

      camelCasePartnerInvoice["id"] = partnerInvoiceJson["id"];
      camelCasePartnerInvoice["partnerId"] = partnerInvoiceJson["partner_id"];
      camelCasePartnerInvoice["partnerName"] =
          partnerInvoiceJson["partner_name"];
      camelCasePartnerInvoice["partnerEmail"] =
          partnerInvoiceJson["partner_email"];
      camelCasePartnerInvoice["invoiceNumber"] =
          partnerInvoiceJson["invoice_number"];
      camelCasePartnerInvoice["description"] =
          partnerInvoiceJson["description"];
      camelCasePartnerInvoice["invoiceAmount"] =
          partnerInvoiceJson["invoice_amount"];
      camelCasePartnerInvoice["balance"] = partnerInvoiceJson["balance"];
      camelCasePartnerInvoice["billingCycle"] =
          partnerInvoiceJson["billing_cycle"];
      camelCasePartnerInvoice["currency"] = partnerInvoiceJson["currency"];
      camelCasePartnerInvoice["status"] = partnerInvoiceJson["status"];
      camelCasePartnerInvoice["createdAt"] = partnerInvoiceJson["created_at"];
      camelCasePartnerInvoice["dueDate"] = partnerInvoiceJson["due_date"];
      camelCasePartnerInvoice["paidAt"] = partnerInvoiceJson["paid_at"];

      data.append(camelCasePartnerInvoice);
    }

    response.result["data"] = data;
    co_return response;

  } catch (const DrogonDbException &e) {
    gnp::dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["message"] =
        "Database error while fetching partner invoices.";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<dto::BaseApiResponse> PartnerInvoiceService::createInvoice(const dto::PartnerInvoiceDto &dto) {

  auto dbClient = drogon::app().getDbClient();
  CoroMapper<PartnerInvoices> mp(dbClient);

  try {
    PartnerInvoices invoice;
    invoice.setId(gnp::utils::IdGeneratorUtils::generateGuid());
    invoice.setPartnerId(dto.getPartnerId());
    invoice.setPartnerName(dto.getPartnerName());
    invoice.setPartnerEmail(dto.getPartnerEmail());
    invoice.setBillingCycle(dto.getBillingCycle());
    invoice.setInvoiceNumber(dto.getInvoiceNumber());
    invoice.setDescription(dto.getDescription());

    if (dto.getInvoiceDate().microSecondsSinceEpoch() != 0) {
      invoice.setCreatedAt(dto.getInvoiceDate());
    }

    char unitPriceBuf[64];
    snprintf(unitPriceBuf, sizeof(unitPriceBuf), "%.2f", dto.getUnitPrice());
    invoice.setUnitPrice(unitPriceBuf);

    // Convert double to string for numeric fields
    char buf[64];
    snprintf(buf, sizeof(buf), "%.2f", dto.getInvoiceAmount());
    invoice.setInvoiceAmount(buf);

    snprintf(buf, sizeof(buf), "%.2f", dto.getBalance());
    invoice.setBalance(buf);

    invoice.setCurrency(dto.getCurrency());
    invoice.setStatus(dto.getStatus().empty() ? "Pending" : dto.getStatus());

    invoice.setDueDate(dto.getDueDate());
    invoice.setCreatedAt(dto.getInvoiceDate()); //

    auto result = co_await mp.insert(invoice);

    dto::BaseApiResponse response;
    response.success = true;
    response.message = "Invoice created successfully";
    response.result["id"] = result.getValueOfId();
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.message = "Failed to create invoice";
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<dto::BaseApiResponse> PartnerInvoiceService::getInvoiceStats() {

  auto dbClient = drogon::app().getDbClient();

  std::string sql = R"sql(
        SELECT 
            COALESCE(SUM(CAST(invoice_amount AS NUMERIC)), 0) as total_invoiced,
            COALESCE(SUM(CAST(invoice_amount AS NUMERIC) - CAST(balance AS NUMERIC)), 0) as total_paid,
            COALESCE(SUM(CAST(balance AS NUMERIC)), 0) as pending_invoices,
            COALESCE(SUM(CASE WHEN due_date < NOW() AND status != 'Paid' THEN CAST(balance AS NUMERIC) ELSE 0 END), 0) as overdue_amount
        FROM partner_invoices
    )sql";

  try {
    auto result = co_await dbClient->execSqlCoro(sql);

    dto::BaseApiResponse response;
    response.success = true;

    Json::Value stats;
    if (!result.empty()) {
      stats["totalInvoiced"] = result[0]["total_invoiced"].as<double>();
      stats["totalPaid"] = result[0]["total_paid"].as<double>();
      stats["pendingInvoices"] = result[0]["pending_invoices"].as<double>();
      stats["overdueAmount"] = result[0]["overdue_amount"].as<double>();
    } else {
      stats["totalInvoiced"] = 0.0;
      stats["totalPaid"] = 0.0;
      stats["pendingInvoices"] = 0.0;
      stats["overdueAmount"] = 0.0;
    }

    response.result = stats;
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to fetch invoice stats";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<dto::BaseApiResponse>
PartnerInvoiceService::markAsPaid(const std::string &id) {
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<PartnerInvoices> mp(dbClient);

  try {
    auto invoice = co_await mp.findByPrimaryKey(id);
    invoice.setStatus("Paid");
    invoice.setBalance("0.00");
    invoice.setPaidAt(trantor::Date::now());
    invoice.setUpdatedAt(trantor::Date::now());

    co_await mp.update(invoice);

    dto::BaseApiResponse response;
    response.success = true;
    response.message = "Invoice marked as paid";
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to mark invoice as paid";
    errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<dto::BaseApiResponse> PartnerInvoiceService::makePartPayment(
    const dto::PartnerInvoicePaymentDto &dto) {
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<drogon_model::Gnp::PartnerInvoicePayments> mp(dbClient);

  try {
    drogon_model::Gnp::PartnerInvoicePayments payment;
    payment.setId(gnp::utils::IdGeneratorUtils::generateGuid());
    payment.setInvoiceId(dto.getInvoiceId());
    payment.setPartnerId(dto.getPartnerId());
    payment.setPaymentDate(trantor::Date::now());

    char buf[64];
    snprintf(buf, sizeof(buf), "%.2f", dto.getAmountPaid());
    payment.setAmountPaid(buf);

    payment.setPaymentMethod(dto.getPaymentMethod());
    payment.setPaymentReference(dto.getPaymentReference());
    payment.setTransactionId(dto.getTransactionId());
    payment.setCurrency(dto.getCurrency().empty() ? "GHS" : dto.getCurrency());
    payment.setPaymentStatus("Completed");
    payment.setNotes(dto.getNotes());
    payment.setCreatedAt(trantor::Date::now());

    co_await mp.insert(payment);

    dto::BaseApiResponse response;
    response.success = true;
    response.message =
        "Payment recorded successfully. Invoice balance updated.";
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to record payment";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<dto::BaseApiResponse> PartnerInvoiceService::makeFullPayment(
    const dto::PartnerInvoicePaymentDto &dto) {
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<drogon_model::Gnp::PartnerInvoices> invMp(dbClient);

  try {
    // Fetch the current balance of the invoice
    auto invoice = co_await invMp.findByPrimaryKey(dto.getInvoiceId());
    double balance = std::stod(invoice.getValueOfBalance());

    if (balance <= 0) {
      dto::BaseApiResponse response;
      response.success = true;
      response.message = "Invoice is already fully paid";
      co_return response;
    }

    // Create a full payment
    dto::PartnerInvoicePaymentDto fullPaymentDto = dto;
    fullPaymentDto.setAmountPaid(balance);

    co_return co_await makePartPayment(fullPaymentDto);

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to fetch invoice for full payment";
    errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<dto::BaseApiResponse>
PartnerInvoiceService::getById(const std::string &id) {
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<PartnerInvoices> mp(dbClient);

  try {
    auto partnerInvoice = co_await mp.findByPrimaryKey(id);

    dto::BaseApiResponse response;
    response.success = true;

    Json::Value partnerInvoiceJson = partnerInvoice.toJson();
    Json::Value camelCasePartnerInvoice;

    camelCasePartnerInvoice["id"] = partnerInvoiceJson["id"];
    camelCasePartnerInvoice["partnerId"] = partnerInvoiceJson["partner_id"];
    camelCasePartnerInvoice["invoiceNumber"] =
        partnerInvoiceJson["invoice_number"];
    camelCasePartnerInvoice["description"] = partnerInvoiceJson["description"];
    camelCasePartnerInvoice["invoiceAmount"] =
        partnerInvoiceJson["invoice_amount"];
    camelCasePartnerInvoice["balance"] = partnerInvoiceJson["balance"];
    camelCasePartnerInvoice["billingCycle"] =
        partnerInvoiceJson["billing_cycle"];
    camelCasePartnerInvoice["currency"] = partnerInvoiceJson["currency"];
    camelCasePartnerInvoice["status"] = partnerInvoiceJson["status"];
    camelCasePartnerInvoice["createdAt"] = partnerInvoiceJson["created_at"];
    camelCasePartnerInvoice["dueDate"] = partnerInvoiceJson["due_date"];
    camelCasePartnerInvoice["paidAt"] = partnerInvoiceJson["paid_at"];

    response.result = camelCasePartnerInvoice;
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Invoice not found";
    errorResponse.error["code"] = constants::ERR_RESOURCE_NOT_FOUND;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<dto::BaseApiResponse>
PartnerInvoiceService::deleteInvoice(const std::string &id) {
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<PartnerInvoices> mp(dbClient);

  try {
    co_await mp.deleteByPrimaryKey(id);

    dto::BaseApiResponse response;
    response.success = true;
    response.message = "Invoice deleted successfully";
    co_return response;

  } catch (const DrogonDbException &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to delete invoice";
    errorResponse.error["code"] = constants::ERR_DB_QUERY;
    errorResponse.error["detail"] = e.base().what();
    co_return errorResponse;
  }
}

drogon::Task<dto::BaseApiResponse>
PartnerInvoiceService::generatePartnerInvoices(const std::string &invoiceDate) {
  auto dbClient = drogon::app().getDbClient();
  CoroMapper<drogon_model::Gnp::CommercialPartners> partnerMp(dbClient);

  try {
    // Quartz supplies a timestamp, while the public endpoint may supply a date.
    int year, month, day;
    if (!dto::ReportFilterDto::parseYmd(invoiceDate, year, month, day) ||
        dto::ReportFilterDto::civilFromDays(
            dto::ReportFilterDto::daysFromCivil(year, month, day)) != invoiceDate.substr(0, 10)) {
      dto::BaseApiResponse response;
      response.success = false;
      response.message = "A valid invoice date (YYYY-MM-DD) is required";
      response.error["code"] = constants::ERR_VALIDATION;
      co_return response;
    }
    const std::string date = invoiceDate.substr(0, 10);

    // A history row with N/A is created for a first subscription. The latest
    // matching history with a real previous cycle identifies a renewal. Use
    // both user and identifier (identifiers alone are not globally unique).
    // Missing history is ambiguous; it must not be silently billed as new.
    const auto subscriptions = co_await dbClient->execSqlCoro(R"sql(
      SELECT s.partner_id,
             (EXTRACT(EPOCH FROM (s.end_date - s.start_date)) * 1000000)::bigint AS duration_micros,
             COALESCE(LOWER(TRIM(h.past_billing_cycle)) NOT IN ('n/a', ''), FALSE) AS renewed,
             h.past_billing_cycle IS NULL AS classification_unknown
      FROM user_subscriptions s
      LEFT JOIN LATERAL (
        SELECT r.past_billing_cycle
        FROM subscription_renewal_history r
        WHERE r.user_id = s.user_id
          AND r.subscription_identifier = s.subscription_identifier
          AND (r.partner_id IS NULL OR r.partner_id = s.partner_id)
          AND LOWER(r.transaction_status) = 'successful'
        ORDER BY r.created_at DESC, r.id DESC
        LIMIT 1
      ) h ON TRUE
      WHERE s.partner_id IS NOT NULL
        AND s.start_date >= $1::date AND s.start_date < $1::date + INTERVAL '1 day'
    )sql", date);

    struct Amounts { long long newMonths = 0; long long renewedMonths = 0; bool unknown = false; };
    std::map<std::string, Amounts> partnerSubs;
    for (const auto &row : subscriptions) {
      auto &amounts = partnerSubs[row["partner_id"].as<std::string>()];
      amounts.unknown |= row["classification_unknown"].as<bool>();
      auto &months = row["renewed"].as<bool>() ? amounts.renewedMonths : amounts.newMonths;
      months += utils::partner_invoice::durationMultiplier(row["duration_micros"].as<long long>());
    }

    int invoicesGenerated = 0;
    int legacyPartnersSkipped = 0;
    int unclassifiedPartnersSkipped = 0;
    for (const auto &[partnerId, amounts] : partnerSubs) {
      if (amounts.unknown) {
        ++unclassifiedPartnersSkipped;
        continue;
      }
      auto partner = co_await partnerMp.findByPrimaryKey(partnerId);
      const double costPerHead = partner.getValueOfCostPerHead().empty()
                                     ? 3.0 : std::stod(partner.getValueOfCostPerHead());
      if (!std::isfinite(costPerHead) || costPerHead < 0) {
        throw std::runtime_error("Invalid cost per head for partner " + partnerId);
      }
      const auto unitPesewas = utils::partner_invoice::toPesewas(costPerHead);
      const auto invoiceDateObj = trantor::Date::fromDbStringLocal(date + " 00:00:00");
      const std::string email = partner.getValueOfBillingEmail().empty()
                                    ? partner.getValueOfContactEmail() : partner.getValueOfBillingEmail();

      // A partner/day is atomic and retry-safe, including concurrent requests.
      auto tx = co_await dbClient->newTransactionCoro();
      try {
        co_await tx->execSqlCoro("SELECT pg_advisory_xact_lock(hashtextextended($1::text, 0))",
                                 "partner-invoice:" + partnerId + ":" + date);
        auto existing = co_await tx->execSqlCoro(
            "SELECT description FROM partner_invoices WHERE partner_id = $1 "
            "AND (description = $2 OR description = $3 OR description LIKE $4)", partnerId,
            utils::partner_invoice::description(false, date),
            utils::partner_invoice::description(true, date),
            std::string(utils::partner_invoice::legacyPrefix) + date + "%");
        bool hasNew = false, hasRenewed = false, hasLegacy = false;
        for (const auto &row : existing) {
          const auto desc = row["description"].as<std::string>();
          hasNew |= desc == utils::partner_invoice::description(false, date);
          hasRenewed |= desc == utils::partner_invoice::description(true, date);
          hasLegacy |= desc.rfind(std::string(utils::partner_invoice::legacyPrefix) + date, 0) == 0;
        }
        if (hasLegacy) {
          // A combined historical invoice cannot be split reliably. Do not
          // silently double-bill it; reconcile it manually before regeneration.
          ++legacyPartnersSkipped;
        } else {
          CoroMapper<PartnerInvoices> invoiceMapper(tx);
          for (bool renewed : {false, true}) {
            if (renewed ? hasRenewed : hasNew) continue;
            const auto months = renewed ? amounts.renewedMonths : amounts.newMonths;
            const auto amount = utils::partner_invoice::toCurrency(unitPesewas * months);
            PartnerInvoices invoice;
            invoice.setId(utils::IdGeneratorUtils::generateGuid());
            invoice.setPartnerId(partnerId);
            invoice.setPartnerName(partner.getValueOfName());
            invoice.setPartnerEmail(email);
            invoice.setBillingCycle("Daily");
            invoice.setInvoiceNumber("GNP-INV-" + utils::IdGeneratorUtils::generateAlphanumericId());
            invoice.setDescription(utils::partner_invoice::description(renewed, date));
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.2f", utils::partner_invoice::toCurrency(unitPesewas));
            invoice.setUnitPrice(buf);
            std::snprintf(buf, sizeof(buf), "%.2f", amount);
            invoice.setInvoiceAmount(buf);
            invoice.setBalance(buf);
            invoice.setCurrency("GHS");
            invoice.setStatus("Pending");
            invoice.setCreatedAt(invoiceDateObj);
            invoice.setDueDate(invoiceDateObj.after(30.0 * 24.0 * 3600.0));
            co_await invoiceMapper.insert(invoice);
            ++invoicesGenerated;
          }
        }
      } catch (...) {
        tx->rollback();
        throw;
      }
      tx.reset(); // Drogon commits the transaction on destruction.
    }

    dto::BaseApiResponse response;
    response.success = true;
    response.message = "Successfully generated " + std::to_string(invoicesGenerated) +
                       " invoices for partner subscribers.";
    response.result["invoicesGenerated"] = invoicesGenerated;
    response.result["legacyPartnersSkipped"] = legacyPartnersSkipped;
    response.result["unclassifiedPartnersSkipped"] = unclassifiedPartnersSkipped;
    if (legacyPartnersSkipped || unclassifiedPartnersSkipped)
      response.message += " Some partners were skipped and require reconciliation.";

    //schedule periodic invoice generation for the next day at 23:59:59

    auto plugin = drogon::app().getPlugin<plugins::GnpServicePlugin>();
    auto &quartzApi = plugin->getQuartzApi();

    auto &app = drogon::app();
    auto customConfig = app.getCustomConfig();

    std::string partnerInvoiceGenerationCallBackUrl = customConfig["QuartzSchedulerApi"]["PartnerInvoiceGenerationCallBackUrl"].asString();

    std::string tomorrowStr = trantor::Date::now().after(24 * 3600).toDbStringLocal();
    std::string nextDayEndTimeStr = tomorrowStr.substr(0, 10) + " 23:59:59";
    auto startTime = trantor::Date::fromDbStringLocal(nextDayEndTimeStr);

    dto::QuartzJobDto raffleClosureQuartzJobDto;
    raffleClosureQuartzJobDto.name = "partner-invoice-" + startTime.toDbStringLocal();
    raffleClosureQuartzJobDto.description = "Generates partner invoice at scheduled start time";
    raffleClosureQuartzJobDto.customData.uniqueId = startTime.toDbStringLocal();
    raffleClosureQuartzJobDto.customData.callbackUrl = partnerInvoiceGenerationCallBackUrl + startTime.toDbStringLocal();

    auto endTime = startTime.after(5 * 60);

    raffleClosureQuartzJobDto.schedule = utils::CronUtils::buildCronExpression(startTime);
    
    std::string startDateStr = startTime.after(-5 * 60).toDbStringLocal();
    std::replace(startDateStr.begin(), startDateStr.end(), ' ', 'T');
    raffleClosureQuartzJobDto.startDate = startDateStr;

    std::string endDateStr = endTime.toDbStringLocal();
    std::replace(endDateStr.begin(), endDateStr.end(), ' ', 'T');
    raffleClosureQuartzJobDto.endDate = endDateStr;

    co_await quartzApi.scheduleJob(raffleClosureQuartzJobDto);

    co_return response;

  } catch (const std::exception &e) {
    dto::BaseApiResponse errorResponse;
    errorResponse.success = false;
    errorResponse.message = "Failed to generate partner invoices";
    errorResponse.error["code"] = constants::ERR_INTERNAL;
    errorResponse.error["detail"] = e.what();
    co_return errorResponse;
  }
}
} // namespace gnp::services
