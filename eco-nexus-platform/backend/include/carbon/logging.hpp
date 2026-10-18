// Thread-safe structured (JSON-lines) logger with automatic secret redaction.
#pragma once

#include <nlohmann/json.hpp>

#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

namespace carbon {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

std::optional<LogLevel> parseLogLevel(std::string_view text);  // case-insensitive
std::string_view toString(LogLevel level);

// Replaces values of keys that look secret (password, token, secret, authorization, cookie, api key).
nlohmann::json redactSecrets(nlohmann::json fields);

class Logger {
 public:
  Logger(std::ostream& out, LogLevel minimum) : out_(out), min_(minimum) {}
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  void log(LogLevel level, std::string_view message, nlohmann::json fields = nlohmann::json::object());
  void debug(std::string_view m, nlohmann::json f = nlohmann::json::object()) { log(LogLevel::Debug, m, std::move(f)); }
  void info(std::string_view m, nlohmann::json f = nlohmann::json::object()) { log(LogLevel::Info, m, std::move(f)); }
  void warn(std::string_view m, nlohmann::json f = nlohmann::json::object()) { log(LogLevel::Warn, m, std::move(f)); }
  void error(std::string_view m, nlohmann::json f = nlohmann::json::object()) { log(LogLevel::Error, m, std::move(f)); }
  LogLevel level() const noexcept { return min_; }

 private:
  std::ostream& out_;
  LogLevel min_;
  std::mutex mutex_;
};

}  // namespace carbon
