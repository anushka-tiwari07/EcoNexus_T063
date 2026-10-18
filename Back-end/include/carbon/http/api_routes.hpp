// All /api/v1 business routes (auth, organisations, memberships, facilities, audit).
#pragma once

#include "carbon/audit/audit_service.hpp"
#include "carbon/auth/auth_service.hpp"
#include "carbon/auth/rate_limiter.hpp"
#include "carbon/auth/session_service.hpp"
#include "carbon/config.hpp"
#include "carbon/domain/facility_service.hpp"
#include "carbon/domain/organization_service.hpp"
#include "carbon/http/app.hpp"

namespace carbon::http {

struct ApiDeps {
  const Config& config;
  Logger& logger;
  auth::SessionService& sessions;
  auth::AuthService& auth;
  domain::OrganizationService& orgs;
  domain::FacilityService& facilities;
  audit::AuditService& audit;
  auth::RateLimiter& loginLimiter;     // per client IP
  auth::RateLimiter& registerLimiter;  // per client IP
  auth::RateLimiter& resetLimiter;     // per client IP (request + confirm)
};

void registerApiRoutes(CarbonApp& app, ApiDeps& deps);

// Session cookie name: the __Host- prefix (Secure, Path=/, no Domain) is used whenever cookies are Secure.
std::string sessionCookieName(const Config& config);

}  // namespace carbon::http
