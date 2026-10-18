// The only ResetNotifier shipped: logs, because no email transport exists in this project.
//  * development: the one-time code is written to the log so the flow can be tried locally
//  * production: NOTHING secret is logged; an operator must plug in a real transport
#pragma once

#include "carbon/auth/auth_service.hpp"
#include "carbon/logging.hpp"

namespace carbon::auth {
class LogResetNotifier final : public ResetNotifier {
 public:
  LogResetNotifier(Logger& logger, bool development) : logger_(logger), development_(development) {}
  void send(const std::string& email, const std::string& token, std::chrono::seconds validFor) override {
    if (development_) {
      logger_.warn("DEV ONLY: password reset requested (no email transport configured)",
                   {{"email", email}, {"dev_reset_code", token}, {"valid_seconds", validFor.count()}});
    } else {
      logger_.warn("password reset requested but no email transport is configured; user was not notified");
    }
  }

 private:
  Logger& logger_;
  bool development_;
};
}  // namespace carbon::auth
