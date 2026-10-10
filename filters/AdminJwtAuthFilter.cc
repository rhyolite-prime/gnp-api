/**
 *
 *  AdminJwtAuthFilter.cc
 *
 */

#include "AdminJwtAuthFilter.h"
#include "dto/BaseApiResponse.h"
#include <drogon/HttpAppFramework.h>
#include <jwt-cpp/jwt.h>

using namespace drogon;

void AdminJwtAuthFilter::doFilter(const HttpRequestPtr &req,
                         FilterCallback &&fcb,
                         FilterChainCallback &&fccb)
{
    LOG_DEBUG << "AdminJwtAuthFilter::doFilter called";
  LOG_DEBUG << "Method: " << req->getMethodString();

  if (req->getMethod() == Options) {
    LOG_DEBUG << "OPTIONS request, bypassing auth";
    fccb();
    return;
  }


  // Extract the Authorization header
  auto authHeader = req->getHeader("Authorization");
  LOG_DEBUG << "Authorization header (capitalized): '" << authHeader << "'";

  if (authHeader.empty()) {
    authHeader = req->getHeader("authorization");
    LOG_DEBUG << "Authorization header (lowercase): '" << authHeader << "'";
  }

  if (authHeader.empty() || authHeader.substr(0, 7) != "Bearer ") {
    LOG_WARN << "Authorization header is missing or invalid. Header value: '" << authHeader << "'";
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = "Authorization header is missing or invalid";
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k401Unauthorized);
    fcb(resp);
    return;
  }

  // Extract the token
  std::string token = authHeader.substr(7);
  //LOG_DEBUG << "Extracted token: " << token.substr(0, 20) << "...";

  try {
    // Get JWT config
    auto &app = drogon::app();
    auto customConfig = app.getCustomConfig();
    std::string jwtSecurityKey =
        customConfig["JwtBearer"]["JwtSecurityKey"].asString();
    std::string jwtIssuer = customConfig["JwtBearer"]["JwtIssuer"].asString();

    LOG_DEBUG << "JWT Issuer from config: " << jwtIssuer;

    // Verify token
    auto verifier = jwt::verify()
                        .allow_algorithm(jwt::algorithm::hs256{jwtSecurityKey})
                        .with_issuer(jwtIssuer);

    auto decoded = jwt::decode(token);
    verifier.verify(decoded);

    LOG_DEBUG << "Token verified successfully";

    // Extract claims and store in request attributes
    if (decoded.has_payload_claim("adminUserId")) {
      auto adminUserId = decoded.get_payload_claim("adminUserId").as_string();
      req->attributes()->insert("adminUserId", adminUserId);
      LOG_DEBUG << "Extracted adminUserId: " << adminUserId;
    }

    if (decoded.has_payload_claim("adminUserEmail")) {
      auto adminUserEmail = decoded.get_payload_claim("adminUserEmail").as_string();
      req->attributes()->insert("adminUserEmail", adminUserEmail);
      LOG_DEBUG << "Extracted adminUserEmail: " << adminUserEmail;
    }

    if (decoded.has_payload_claim("adminUserName")) {
      auto adminUserName = decoded.get_payload_claim("adminUserName").as_string();
      req->attributes()->insert("adminUserName", adminUserName);
    }

    if (decoded.has_payload_claim("permissions")) {
      auto permissionsStr = decoded.get_payload_claim("permissions").as_string();
      std::unordered_set<std::string> permissionsSet;
      Json::Value permissionsJson;
      Json::Reader reader;
      if (!permissionsStr.empty() && reader.parse(permissionsStr, permissionsJson) && permissionsJson.isArray()) {
        for (const auto& perm : permissionsJson) {
          if (perm.isString()) {
            permissionsSet.insert(perm.asString());
          }
        }
      }
      req->attributes()->insert("permissions", permissionsSet);
      LOG_DEBUG << "Extracted " << permissionsSet.size() << " permissions";
    }

    // Token is valid, continue to the next filter or handler
    LOG_DEBUG << "Calling next filter/handler";
    fccb();

  } catch (const std::exception &e) {
    LOG_ERROR << "Token validation failed: " << e.what();
    gnp::dto::BaseApiResponse response;
    response.success = false;
    response.error["message"] = std::string("Token validation failed: ") + e.what();
    auto resp = HttpResponse::newHttpJsonResponse(response.toJson());
    resp->setStatusCode(k401Unauthorized);

    fcb(resp);
  }
}
