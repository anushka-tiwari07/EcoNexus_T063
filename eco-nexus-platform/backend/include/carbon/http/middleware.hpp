// Cross-cutting HTTP concerns as one Crow middleware:
//   request IDs, structured access logging, security headers, CORS allow-list,
//   request-size limit, and normalisation of every error into the standard JSON shape.
#pragma once

#include "carbon/logging.hpp"

#include <crow.h>

#include <chrono>
#include <string>
#include <vector>

namespace carbon::http {

struct ApiMiddleware {
  struct context {
    std::string requestId;
    std::string origin;
    bool isApi = true;  // path under /api/: strict headers; everything else is the static frontend
    std::chrono::steady_clock::time_point start;
  };

  struct Settings {
    Logger* logger = nullptr;
    std::vector<std::string> allowedOrigins;
    std::size_t maxBodyBytes = 1024 * 1024;
    bool hsts = false;  // enable in production behind TLS
  } settings;

  void before_handle(crow::request& req, crow::response& res, context& ctx);
  void after_handle(crow::request& req, crow::response& res, context& ctx);

  static bool validRequestId(const std::string& id);

 private:
  bool originAllowed(const std::string& origin) const;
  void decorate(crow::response& res, const context& ctx) const;
  void fillError(crow::response& res, const context& ctx, int status, const std::string& code, const std::string& message) const;
  void reject(crow::response& res, const context& ctx, int status, const std::string& code, const std::string& message) const;
  void ensureContext(const crow::request& req, context& ctx) const;
};

}  // namespace carbon::http
