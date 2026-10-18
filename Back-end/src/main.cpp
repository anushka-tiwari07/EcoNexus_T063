// carbon_server: HTTP API entry point. Fails fast with clear messages on bad config / DB / migrations.
#include "carbon/config.hpp"
#include "carbon/db/migrator.hpp"
#include "carbon/db/pool.hpp"
#include "carbon/health/health_service.hpp"
#include "carbon/audit/audit_service.hpp"
#include "carbon/auth/auth_service.hpp"
#include "carbon/auth/password.hpp"
#include "carbon/auth/rate_limiter.hpp"
#include "carbon/auth/reset_notifier.hpp"
#include "carbon/auth/session_service.hpp"
#include "carbon/domain/facility_service.hpp"
#include "carbon/domain/organization_service.hpp"
#include "carbon/http/api_routes.hpp"
#include "carbon/http/app.hpp"

#include <filesystem>
#include <iostream>

#ifndef CARBON_VERSION
#define CARBON_VERSION "0.0.0"
#endif

int main() {
  using namespace carbon;

  Config config;
  try {
    config = Config::fromProcessEnvironment();
  } catch (const ConfigError& e) {
    std::cerr << "carbon_server: " << e.what() << "\nSee .env.example for all settings.\n";
    return 2;
  }

  Logger logger(std::cout, config.logLevel);
  logger.info("starting carbon_server", {{"version", CARBON_VERSION}, {"config", config.redactedSummary()}});

  try {
    db::ConnectionPool pool(config.databaseUrl, config.dbPoolSize);
    try {
      auto lease = pool.acquire();  // fail fast if the database is unreachable
    } catch (const std::exception& e) {
      logger.error("cannot connect to PostgreSQL; check CARBON_DATABASE_URL and that the database is running", {{"error", e.what()}});
      return 3;
    }

    const auto migrations = db::loadMigrations(config.migrationsDir);
    {
      auto lease = pool.acquire();
      db::MigrationStatus status = db::inspectMigrations(*lease, migrations);
      if (!status.pending.empty() && config.autoMigrate && status.unknown.empty() && status.modified.empty()) {
        logger.info("applying pending migrations (CARBON_AUTO_MIGRATE=true)", {{"pending", status.pending.size()}});
        db::applyMigrations(*lease, migrations, &logger);
        status = db::inspectMigrations(*lease, migrations);
      }
      if (!status.upToDate()) {
        logger.error("database schema does not match this build; run `carbon_migrate up` first",
                     {{"pending", status.pending}, {"unknown", status.unknown}, {"modified", status.modified}});
        return 4;
      }
    }

    health::HealthService healthService(CARBON_VERSION, logger);
    healthService.addProbe(std::make_unique<health::DatabaseProbe>(pool));
    healthService.addProbe(std::make_unique<health::MigrationProbe>(pool, migrations));

    http::CrowLogBridge crowLog(logger);
    crow::logger::setHandler(&crowLog);
    crow::logger::setLogLevel(crow::LogLevel::Warning);

    // ---- services ----
    const auth::ArgonPasswordHasher hasher = config.argon2Profile == Config::Argon2Profile::Moderate ? auth::ArgonPasswordHasher::moderate()
                                                                                                      : auth::ArgonPasswordHasher::interactive();
    audit::AuditService auditService(pool);
    auth::SessionService sessions(pool, {std::chrono::hours(config.sessionTtlHours), std::chrono::minutes(config.sessionIdleMinutes)});
    auth::LogResetNotifier notifier(logger, !config.isProduction());
    auth::AuthService authService(pool, hasher, sessions, auditService, notifier, {config.allowSelfRegistration, 5, std::chrono::minutes(15), std::chrono::hours(1)});
    domain::OrganizationService orgs(pool, auditService);
    domain::FacilityService facilities(pool, auditService);
    auth::RateLimiter loginLimiter(30, std::chrono::seconds(60));
    auth::RateLimiter registerLimiter(10, std::chrono::hours(1));
    auth::RateLimiter resetLimiter(10, std::chrono::hours(1));
    sessions.purgeExpired();

    http::CarbonApp app;
    http::configureApp(app, config, logger);
    http::registerHealthRoutes(app, healthService, logger);
    http::ApiDeps deps{config, logger, sessions, authService, orgs, facilities, auditService, loginLimiter, registerLimiter, resetLimiter};
    http::registerApiRoutes(app, deps);
    if (config.frontendDir) {
      if (std::filesystem::is_directory(*config.frontendDir)) {
        http::registerStaticRoutes(app, *config.frontendDir);
        logger.info("serving frontend", {{"dir", config.frontendDir->string()}});
      } else {
        logger.warn("frontend directory not found; serving the API only (set CARBON_FRONTEND_DIR or 'none')",
                    {{"dir", config.frontendDir->string()}});
      }
    }
    http::registerFallbackRoutes(app);

    logger.info("listening", {{"bind", config.httpBind}, {"port", config.httpPort}});
    app.run();  // blocks; Crow handles SIGINT/SIGTERM for a clean shutdown
    logger.info("stopped");
  } catch (const std::exception& e) {
    logger.error("fatal startup error", {{"error", e.what()}});
    return 1;
  }
  return 0;
}
