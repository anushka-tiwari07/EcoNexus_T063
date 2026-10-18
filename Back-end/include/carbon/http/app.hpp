// Crow application assembly: middleware wiring, log bridge, and route registration.
#pragma once

#include "carbon/config.hpp"
#include "carbon/health/health_service.hpp"
#include "carbon/http/middleware.hpp"
#include "carbon/logging.hpp"

#include <crow.h>

#include <array>
#include <filesystem>
#include <string_view>

namespace carbon::http {

// Single source of truth for implemented routes. Path parameters use OpenAPI style {name}.
// Tests check this table against the live server AND against docs/openapi.yaml, so the specification
// cannot drift from the implementation.
struct RouteSpec {
  std::string_view method;
  std::string_view path;
};
inline constexpr std::array<RouteSpec, 21> kApiRoutes{{
    {"GET", "/api/v1/health/live"},
    {"GET", "/api/v1/health/ready"},
    {"POST", "/api/v1/auth/register"},
    {"POST", "/api/v1/auth/login"},
    {"POST", "/api/v1/auth/logout"},
    {"GET", "/api/v1/auth/me"},
    {"POST", "/api/v1/auth/password-reset/request"},
    {"POST", "/api/v1/auth/password-reset/confirm"},
    {"GET", "/api/v1/organizations"},
    {"POST", "/api/v1/organizations"},
    {"GET", "/api/v1/organizations/{orgId}"},
    {"PATCH", "/api/v1/organizations/{orgId}"},
    {"GET", "/api/v1/organizations/{orgId}/members"},
    {"POST", "/api/v1/organizations/{orgId}/members"},
    {"PATCH", "/api/v1/organizations/{orgId}/members/{membershipId}"},
    {"DELETE", "/api/v1/organizations/{orgId}/members/{membershipId}"},
    {"GET", "/api/v1/organizations/{orgId}/facilities"},
    {"POST", "/api/v1/organizations/{orgId}/facilities"},
    {"GET", "/api/v1/organizations/{orgId}/facilities/{facilityId}"},
    {"PATCH", "/api/v1/organizations/{orgId}/facilities/{facilityId}"},
    {"GET", "/api/v1/organizations/{orgId}/audit-events"},
}};

using CarbonApp = crow::App<ApiMiddleware>;

// Routes Crow's own log lines into our structured logger.
class CrowLogBridge final : public crow::ILogHandler {
 public:
  explicit CrowLogBridge(Logger& logger) : logger_(logger) {}
  void log(const std::string& message, crow::LogLevel level) override;

 private:
  Logger& logger_;
};

void configureApp(CarbonApp& app, const Config& config, Logger& logger);
void registerHealthRoutes(CarbonApp& app, health::HealthService& health, Logger& logger);
// Serves the static frontend (same origin as the API, so no CORS). Never serves outside `root` or dotfiles.
void registerStaticRoutes(CarbonApp& app, const std::filesystem::path& root);

void registerFallbackRoutes(CarbonApp& app);  // JSON 404 for unknown routes

}  // namespace carbon::http
