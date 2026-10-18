// Application configuration, read only from environment variables (12-factor).
// Validation collects ALL problems so a misconfigured deployment gets one clear startup error.
#pragma once

#include "carbon/logging.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace carbon {

class ConfigError : public std::runtime_error {
 public:
  explicit ConfigError(std::vector<std::string> problems);
  const std::vector<std::string>& problems() const noexcept { return problems_; }

 private:
  std::vector<std::string> problems_;
};

using EnvLookup = std::function<std::optional<std::string>(const std::string&)>;

struct Config {
  enum class Environment { Development, Production };

  Environment environment = Environment::Development;  // CARBON_ENV
  std::string httpBind = "127.0.0.1";                    // CARBON_HTTP_BIND
  std::uint16_t httpPort = 8080;                         // CARBON_HTTP_PORT
  std::string databaseUrl;                               // CARBON_DATABASE_URL (required, secret)
  std::size_t dbPoolSize = 8;                            // CARBON_DB_POOL_SIZE (1..64)
  std::vector<std::string> allowedOrigins;               // CARBON_ALLOWED_ORIGINS (comma separated)
  LogLevel logLevel = LogLevel::Info;                    // CARBON_LOG_LEVEL
  std::filesystem::path migrationsDir = "db/migrations"; // CARBON_MIGRATIONS_DIR
  bool autoMigrate = false;                              // CARBON_AUTO_MIGRATE (dev convenience)
  std::optional<std::string> redisUrl;                   // CARBON_REDIS_URL (optional; unused so far)
  std::size_t maxRequestBytes = 1024 * 1024;             // CARBON_MAX_REQUEST_BYTES (1 KiB..100 MiB)
  std::optional<std::filesystem::path> frontendDir = "frontend";  // CARBON_FRONTEND_DIR ("none" = API only)
  bool allowSelfRegistration = true;                     // CARBON_ALLOW_SELF_REGISTRATION (default: off in production)
  std::size_t sessionTtlHours = 12;                      // CARBON_SESSION_TTL_HOURS (1..168) absolute lifetime
  std::size_t sessionIdleMinutes = 120;                  // CARBON_SESSION_IDLE_MINUTES (5..1440)
  enum class Argon2Profile { Interactive, Moderate } argon2Profile = Argon2Profile::Interactive;  // CARBON_ARGON2_PROFILE

  bool isProduction() const noexcept { return environment == Environment::Production; }

  // Throws ConfigError listing every invalid/missing setting.
  static Config fromEnvironment(const EnvLookup& lookup);
  static Config fromProcessEnvironment();

  // Safe to log: credentials in URLs are masked.
  nlohmann::json redactedSummary() const;
};

// "postgresql://user:secret@host/db" -> "postgresql://user:***@host/db"
std::string redactUrlCredentials(const std::string& url);

}  // namespace carbon
