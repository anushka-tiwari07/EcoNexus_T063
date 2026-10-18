#include "carbon/config.hpp"

#include <gtest/gtest.h>

#include <map>

using namespace carbon;

namespace {
Config load(std::map<std::string, std::string> env) {
  return Config::fromEnvironment([&](const std::string& k) -> std::optional<std::string> {
    auto it = env.find(k);
    if (it == env.end()) return std::nullopt;
    return it->second;
  });
}
const std::string kDb = "postgresql://u:p@localhost:5432/carbon";
}  // namespace

TEST(Config, DefaultsWhenOnlyDatabaseUrlGiven) {
  const Config c = load({{"CARBON_DATABASE_URL", kDb}});
  EXPECT_EQ(c.httpPort, 8080);
  EXPECT_EQ(c.httpBind, "127.0.0.1");
  EXPECT_EQ(c.dbPoolSize, 8u);
  EXPECT_FALSE(c.isProduction());
  EXPECT_FALSE(c.autoMigrate);
  EXPECT_TRUE(c.allowedOrigins.empty());
  EXPECT_EQ(c.logLevel, LogLevel::Info);
  EXPECT_FALSE(c.redisUrl.has_value());
}

TEST(Config, DatabaseUrlIsRequired) {
  try {
    load({});
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& e) {
    ASSERT_EQ(e.problems().size(), 1u);
    EXPECT_NE(e.problems()[0].find("CARBON_DATABASE_URL"), std::string::npos);
  }
}

TEST(Config, ReportsAllProblemsAtOnce) {
  try {
    load({{"CARBON_HTTP_PORT", "70000"}, {"CARBON_DB_POOL_SIZE", "0"}, {"CARBON_LOG_LEVEL", "loud"},
          {"CARBON_ENV", "staging"}, {"CARBON_DATABASE_URL", "mysql://x"}});
    FAIL() << "expected ConfigError";
  } catch (const ConfigError& e) {
    EXPECT_EQ(e.problems().size(), 5u);
    EXPECT_NE(std::string(e.what()).find("5 problems"), std::string::npos);
  }
}

TEST(Config, ParsesValidValues) {
  const Config c = load({{"CARBON_DATABASE_URL", kDb}, {"CARBON_HTTP_PORT", "9090"}, {"CARBON_ENV", "Production"},
                         {"CARBON_DB_POOL_SIZE", "16"}, {"CARBON_LOG_LEVEL", "WARN"},
                         {"CARBON_ALLOWED_ORIGINS", " https://app.example.test , http://localhost:3000 "},
                         {"CARBON_REDIS_URL", "redis://localhost:6379"}, {"CARBON_MAX_REQUEST_BYTES", "2048"}});
  EXPECT_EQ(c.httpPort, 9090);
  EXPECT_TRUE(c.isProduction());
  EXPECT_EQ(c.dbPoolSize, 16u);
  EXPECT_EQ(c.logLevel, LogLevel::Warn);
  ASSERT_EQ(c.allowedOrigins.size(), 2u);
  EXPECT_EQ(c.allowedOrigins[0], "https://app.example.test");
  EXPECT_TRUE(c.redisUrl.has_value());
  EXPECT_EQ(c.maxRequestBytes, 2048u);
}

TEST(Config, RejectsWildcardAndMalformedOrigins) {
  EXPECT_THROW(load({{"CARBON_DATABASE_URL", kDb}, {"CARBON_ALLOWED_ORIGINS", "*"}}), ConfigError);
  EXPECT_THROW(load({{"CARBON_DATABASE_URL", kDb}, {"CARBON_ALLOWED_ORIGINS", "https://a.test/path"}}), ConfigError);
  EXPECT_THROW(load({{"CARBON_DATABASE_URL", kDb}, {"CARBON_ALLOWED_ORIGINS", "ftp://a.test"}}), ConfigError);
  EXPECT_THROW(load({{"CARBON_DATABASE_URL", kDb}, {"CARBON_ALLOWED_ORIGINS", "https://a.test/"}}), ConfigError);
}

TEST(Config, RejectsNonNumericAndOutOfRangeNumbers) {
  for (const char* bad : {"abc", "-1", "0", "65536", "80x", " "}) {
    if (std::string(bad) == " ") continue;  // blank counts as "unset"
    EXPECT_THROW(load({{"CARBON_DATABASE_URL", kDb}, {"CARBON_HTTP_PORT", bad}}), ConfigError) << bad;
  }
  EXPECT_THROW(load({{"CARBON_DATABASE_URL", kDb}, {"CARBON_DB_POOL_SIZE", "65"}}), ConfigError);
  EXPECT_THROW(load({{"CARBON_DATABASE_URL", kDb}, {"CARBON_MAX_REQUEST_BYTES", "10"}}), ConfigError);
}

TEST(Config, AutoMigrateForbiddenInProduction) {
  EXPECT_THROW(load({{"CARBON_DATABASE_URL", kDb}, {"CARBON_ENV", "production"}, {"CARBON_AUTO_MIGRATE", "true"}}), ConfigError);
  EXPECT_TRUE(load({{"CARBON_DATABASE_URL", kDb}, {"CARBON_AUTO_MIGRATE", "1"}}).autoMigrate);
  EXPECT_THROW(load({{"CARBON_DATABASE_URL", kDb}, {"CARBON_AUTO_MIGRATE", "yes"}}), ConfigError);
}

TEST(Config, RedactsCredentialsInSummary) {
  const Config c = load({{"CARBON_DATABASE_URL", "postgresql://alice:s3cr3t@db.internal:5432/carbon"}});
  const std::string summary = c.redactedSummary().dump();
  EXPECT_EQ(summary.find("s3cr3t"), std::string::npos);
  EXPECT_NE(summary.find("alice:***@db.internal"), std::string::npos);
}

TEST(Config, RedactUrlCredentialsEdgeCases) {
  EXPECT_EQ(redactUrlCredentials("postgresql://localhost/db"), "postgresql://localhost/db");
  EXPECT_EQ(redactUrlCredentials("postgresql://user@localhost/db"), "postgresql://user@localhost/db");
  EXPECT_EQ(redactUrlCredentials("postgresql://u:p@h/db?x=a@b"), "postgresql://u:***@h/db?x=a@b");
  EXPECT_EQ(redactUrlCredentials("not a url"), "not a url");
}

TEST(Config, SessionAndRegistrationSettings) {
  Config c = load({{"CARBON_DATABASE_URL", kDb}});
  EXPECT_TRUE(c.allowSelfRegistration);  // development default
  EXPECT_EQ(c.sessionTtlHours, 12u);
  EXPECT_EQ(c.sessionIdleMinutes, 120u);
  EXPECT_EQ(c.argon2Profile, Config::Argon2Profile::Interactive);

  c = load({{"CARBON_DATABASE_URL", kDb}, {"CARBON_ENV", "production"}});
  EXPECT_FALSE(c.allowSelfRegistration);  // production default: closed
  c = load({{"CARBON_DATABASE_URL", kDb}, {"CARBON_ENV", "production"}, {"CARBON_ALLOW_SELF_REGISTRATION", "true"}, {"CARBON_ARGON2_PROFILE", "Moderate"},
            {"CARBON_SESSION_TTL_HOURS", "24"}, {"CARBON_SESSION_IDLE_MINUTES", "30"}});
  EXPECT_TRUE(c.allowSelfRegistration);
  EXPECT_EQ(c.argon2Profile, Config::Argon2Profile::Moderate);
  EXPECT_EQ(c.sessionTtlHours, 24u);
  EXPECT_EQ(c.sessionIdleMinutes, 30u);

  for (const auto& kv : std::vector<std::pair<std::string, std::string>>{{"CARBON_SESSION_TTL_HOURS", "0"}, {"CARBON_SESSION_TTL_HOURS", "169"},
                                                                         {"CARBON_SESSION_IDLE_MINUTES", "4"}, {"CARBON_ARGON2_PROFILE", "weak"},
                                                                         {"CARBON_ALLOW_SELF_REGISTRATION", "maybe"}})
    EXPECT_THROW(load({{"CARBON_DATABASE_URL", kDb}, {kv.first, kv.second}}), ConfigError) << kv.first << "=" << kv.second;
}
