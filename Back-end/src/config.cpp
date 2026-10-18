#include "carbon/config.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <sstream>

namespace carbon {
namespace {
std::string trim(std::string s) {
  auto notSpace = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
  s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
  return s;
}

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

bool startsWith(const std::string& s, const std::string& prefix) { return s.rfind(prefix, 0) == 0; }

std::optional<unsigned long long> parseUnsigned(const std::string& s) {
  unsigned long long v = 0;
  const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
  if (s.empty() || ec != std::errc{} || ptr != s.data() + s.size()) return std::nullopt;
  return v;
}

// Origin = scheme://host[:port], no path, no trailing slash, never "*".
bool validOrigin(const std::string& o) {
  const bool http = startsWith(o, "http://"), https = startsWith(o, "https://");
  if (!http && !https) return false;
  const std::string rest = o.substr(http ? 7 : 8);
  return !rest.empty() && rest.find_first_of("/?# \t") == std::string::npos;
}
}  // namespace

ConfigError::ConfigError(std::vector<std::string> problems)
    : std::runtime_error([&] {
        std::ostringstream os;
        os << "Invalid configuration (" << problems.size() << " problem" << (problems.size() == 1 ? "" : "s") << "):";
        for (const auto& p : problems) os << "\n  - " << p;
        return os.str();
      }()),
      problems_(std::move(problems)) {}

std::string redactUrlCredentials(const std::string& url) {
  const auto scheme = url.find("://");
  if (scheme == std::string::npos) return url;
  const auto authorityStart = scheme + 3;
  const auto at = url.find('@', authorityStart);
  const auto slash = url.find('/', authorityStart);
  if (at == std::string::npos || (slash != std::string::npos && at > slash)) return url;
  const auto colon = url.find(':', authorityStart);
  if (colon == std::string::npos || colon > at) return url;
  return url.substr(0, colon + 1) + "***" + url.substr(at);
}

Config Config::fromEnvironment(const EnvLookup& lookup) {
  Config c;
  std::vector<std::string> problems;
  auto get = [&](const char* name) -> std::optional<std::string> {
    auto v = lookup(name);
    if (!v) return std::nullopt;
    std::string t = trim(*v);
    if (t.empty()) return std::nullopt;
    return t;
  };

  if (auto v = get("CARBON_ENV")) {
    const std::string e = lower(*v);
    if (e == "development") c.environment = Environment::Development;
    else if (e == "production") c.environment = Environment::Production;
    else problems.push_back("CARBON_ENV must be 'development' or 'production' (got '" + *v + "')");
  }

  if (auto v = get("CARBON_HTTP_BIND")) c.httpBind = *v;

  if (auto v = get("CARBON_HTTP_PORT")) {
    const auto n = parseUnsigned(*v);
    if (!n || *n < 1 || *n > 65535) problems.push_back("CARBON_HTTP_PORT must be an integer in 1..65535 (got '" + *v + "')");
    else c.httpPort = static_cast<std::uint16_t>(*n);
  }

  if (auto v = get("CARBON_DATABASE_URL")) {
    if (!startsWith(*v, "postgresql://") && !startsWith(*v, "postgres://"))
      problems.push_back("CARBON_DATABASE_URL must start with postgresql:// or postgres://");
    else c.databaseUrl = *v;
  } else {
    problems.push_back("CARBON_DATABASE_URL is required (e.g. postgresql://user:password@localhost:5432/carbon)");
  }

  if (auto v = get("CARBON_DB_POOL_SIZE")) {
    const auto n = parseUnsigned(*v);
    if (!n || *n < 1 || *n > 64) problems.push_back("CARBON_DB_POOL_SIZE must be an integer in 1..64 (got '" + *v + "')");
    else c.dbPoolSize = static_cast<std::size_t>(*n);
  }

  if (auto v = get("CARBON_ALLOWED_ORIGINS")) {
    std::stringstream ss(*v);
    for (std::string item; std::getline(ss, item, ',');) {
      item = trim(item);
      if (item.empty()) continue;
      if (item == "*") problems.push_back("CARBON_ALLOWED_ORIGINS must not contain '*'; list explicit origins");
      else if (!validOrigin(item)) problems.push_back("CARBON_ALLOWED_ORIGINS entry '" + item + "' must look like https://host[:port] (no path)");
      else c.allowedOrigins.push_back(item);
    }
  }

  if (auto v = get("CARBON_LOG_LEVEL")) {
    if (auto lvl = parseLogLevel(*v)) c.logLevel = *lvl;
    else problems.push_back("CARBON_LOG_LEVEL must be one of debug, info, warn, error (got '" + *v + "')");
  }

  if (auto v = get("CARBON_MIGRATIONS_DIR")) c.migrationsDir = *v;

  if (auto v = get("CARBON_AUTO_MIGRATE")) {
    const std::string b = lower(*v);
    if (b == "true" || b == "1") c.autoMigrate = true;
    else if (b == "false" || b == "0") c.autoMigrate = false;
    else problems.push_back("CARBON_AUTO_MIGRATE must be true/false (got '" + *v + "')");
  }

  if (auto v = get("CARBON_REDIS_URL")) {
    if (!startsWith(*v, "redis://") && !startsWith(*v, "rediss://")) problems.push_back("CARBON_REDIS_URL must start with redis:// or rediss://");
    else c.redisUrl = *v;
  }

  if (auto v = get("CARBON_MAX_REQUEST_BYTES")) {
    const auto n = parseUnsigned(*v);
    if (!n || *n < 1024 || *n > 100ULL * 1024 * 1024) problems.push_back("CARBON_MAX_REQUEST_BYTES must be an integer in 1024..104857600 (got '" + *v + "')");
    else c.maxRequestBytes = static_cast<std::size_t>(*n);
  }

  if (auto v = get("CARBON_FRONTEND_DIR")) {
    if (lower(*v) == "none") c.frontendDir.reset();
    else c.frontendDir = *v;
  }

  c.allowSelfRegistration = !c.isProduction();  // production default: closed unless explicitly opened
  if (auto v = get("CARBON_ALLOW_SELF_REGISTRATION")) {
    const std::string b = lower(*v);
    if (b == "true" || b == "1") c.allowSelfRegistration = true;
    else if (b == "false" || b == "0") c.allowSelfRegistration = false;
    else problems.push_back("CARBON_ALLOW_SELF_REGISTRATION must be true/false (got '" + *v + "')");
  }

  if (auto v = get("CARBON_SESSION_TTL_HOURS")) {
    const auto n = parseUnsigned(*v);
    if (!n || *n < 1 || *n > 168) problems.push_back("CARBON_SESSION_TTL_HOURS must be an integer in 1..168 (got '" + *v + "')");
    else c.sessionTtlHours = static_cast<std::size_t>(*n);
  }

  if (auto v = get("CARBON_SESSION_IDLE_MINUTES")) {
    const auto n = parseUnsigned(*v);
    if (!n || *n < 5 || *n > 1440) problems.push_back("CARBON_SESSION_IDLE_MINUTES must be an integer in 5..1440 (got '" + *v + "')");
    else c.sessionIdleMinutes = static_cast<std::size_t>(*n);
  }

  if (auto v = get("CARBON_ARGON2_PROFILE")) {
    const std::string p = lower(*v);
    if (p == "interactive") c.argon2Profile = Argon2Profile::Interactive;
    else if (p == "moderate") c.argon2Profile = Argon2Profile::Moderate;
    else problems.push_back("CARBON_ARGON2_PROFILE must be 'interactive' or 'moderate' (got '" + *v + "')");
  }

  if (c.isProduction() && c.autoMigrate) problems.push_back("CARBON_AUTO_MIGRATE=true is not allowed when CARBON_ENV=production; run carbon_migrate explicitly");

  if (!problems.empty()) throw ConfigError(std::move(problems));
  return c;
}

Config Config::fromProcessEnvironment() {
  return fromEnvironment([](const std::string& name) -> std::optional<std::string> {
    const char* v = std::getenv(name.c_str());
    if (!v) return std::nullopt;
    return std::string(v);
  });
}

nlohmann::json Config::redactedSummary() const {
  return {{"environment", isProduction() ? "production" : "development"},
          {"http_bind", httpBind},
          {"http_port", httpPort},
          {"database_url", redactUrlCredentials(databaseUrl)},
          {"db_pool_size", dbPoolSize},
          {"allowed_origins", allowedOrigins},
          {"log_level", std::string(toString(logLevel))},
          {"migrations_dir", migrationsDir.string()},
          {"auto_migrate", autoMigrate},
          {"redis_configured", redisUrl.has_value()},
          {"max_request_bytes", maxRequestBytes},
          {"frontend_dir", frontendDir ? frontendDir->string() : std::string("none")},
          {"allow_self_registration", allowSelfRegistration},
          {"session_ttl_hours", sessionTtlHours},
          {"session_idle_minutes", sessionIdleMinutes},
          {"argon2_profile", argon2Profile == Argon2Profile::Moderate ? "moderate" : "interactive"}};
}

}  // namespace carbon
