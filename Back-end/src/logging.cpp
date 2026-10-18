#include "carbon/logging.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace carbon {
namespace {
std::string lower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

bool looksSecret(const std::string& key) {
  static constexpr std::array<std::string_view, 7> kNeedles = {"password", "passwd", "secret", "token",
                                                                "authorization", "cookie", "api_key"};
  const std::string k = lower(key);
  if (k.find("apikey") != std::string::npos) return true;
  return std::any_of(kNeedles.begin(), kNeedles.end(), [&](std::string_view n) { return k.find(n) != std::string::npos; });
}

std::string isoTimestamp() {
  using namespace std::chrono;
  const auto now = system_clock::now();
  const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
  const std::time_t t = system_clock::to_time_t(now);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S", &tm);
  char out[48];
  std::snprintf(out, sizeof out, "%s.%03dZ", buf, static_cast<int>(ms.count()));
  return out;
}
}  // namespace

std::optional<LogLevel> parseLogLevel(std::string_view text) {
  const std::string t = lower(text);
  if (t == "debug") return LogLevel::Debug;
  if (t == "info") return LogLevel::Info;
  if (t == "warn" || t == "warning") return LogLevel::Warn;
  if (t == "error") return LogLevel::Error;
  return std::nullopt;
}

std::string_view toString(LogLevel level) {
  switch (level) {
    case LogLevel::Debug: return "debug";
    case LogLevel::Info: return "info";
    case LogLevel::Warn: return "warn";
    case LogLevel::Error: return "error";
  }
  return "info";
}

nlohmann::json redactSecrets(nlohmann::json fields) {
  if (fields.is_object()) {
    for (auto& [key, value] : fields.items()) {
      if (looksSecret(key)) value = "[REDACTED]";
      else value = redactSecrets(std::move(value));
    }
  } else if (fields.is_array()) {
    for (auto& v : fields) v = redactSecrets(std::move(v));
  }
  return fields;
}

void Logger::log(LogLevel level, std::string_view message, nlohmann::json fields) {
  if (level < min_) return;
  nlohmann::json line = {{"ts", isoTimestamp()}, {"level", toString(level)}, {"msg", message}};
  if (!fields.is_null() && !(fields.is_object() && fields.empty())) line["fields"] = redactSecrets(std::move(fields));
  // One write under the lock keeps lines from different threads from interleaving.
  const std::string text = line.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
  std::lock_guard<std::mutex> lock(mutex_);
  out_ << text << '\n';
  out_.flush();
}

}  // namespace carbon
