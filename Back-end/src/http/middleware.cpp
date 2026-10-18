#include "carbon/http/middleware.hpp"

#include "carbon/crypto.hpp"
#include "carbon/errors.hpp"

#include <algorithm>

namespace carbon::http {

bool ApiMiddleware::validRequestId(const std::string& id) {
  if (id.empty() || id.size() > 64) return false;
  return std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; });
}

bool ApiMiddleware::originAllowed(const std::string& origin) const {
  return !origin.empty() && std::find(settings.allowedOrigins.begin(), settings.allowedOrigins.end(), origin) != settings.allowedOrigins.end();
}

void ApiMiddleware::decorate(crow::response& res, const context& ctx) const {
  res.set_header("X-Request-Id", ctx.requestId);
  res.set_header("X-Content-Type-Options", "nosniff");
  res.set_header("X-Frame-Options", "DENY");
  res.set_header("Referrer-Policy", "no-referrer");
  if (ctx.isApi) {
    res.set_header("Cache-Control", "no-store");
    res.set_header("Content-Security-Policy", "default-src 'none'; frame-ancestors 'none'");
  } else {
    // Frontend: own scripts/styles only (inline style attributes are used by the UI and Chart.js), no external loads.
    res.set_header("Cache-Control", "no-cache");  // always revalidate so a new build is picked up
    res.set_header("Content-Security-Policy",
                   "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; "
                   "connect-src 'self'; base-uri 'none'; form-action 'self'; frame-ancestors 'none'");
  }
  if (settings.hsts) res.set_header("Strict-Transport-Security", "max-age=31536000; includeSubDomains");
  if (originAllowed(ctx.origin)) {
    res.set_header("Access-Control-Allow-Origin", ctx.origin);  // exact echo of an allow-listed origin, never "*"
    res.set_header("Access-Control-Expose-Headers", "X-Request-Id");
    res.add_header("Vary", "Origin");
  }
}

void ApiMiddleware::fillError(crow::response& res, const context& ctx, int status, const std::string& code, const std::string& message) const {
  res.code = status;
  res.set_header("Content-Type", "application/json; charset=utf-8");
  res.body = makeErrorBody(code, message, ctx.requestId).dump();
  decorate(res, ctx);
}

void ApiMiddleware::reject(crow::response& res, const context& ctx, int status, const std::string& code, const std::string& message) const {
  fillError(res, ctx, status, code, message);
  res.end();
}

// Crow answers OPTIONS requests itself and then calls only after_handle (before_handle never runs),
// so the per-request context must be initialisable from either entry point.
void ApiMiddleware::ensureContext(const crow::request& req, context& ctx) const {
  if (!ctx.requestId.empty()) return;
  ctx.start = std::chrono::steady_clock::now();
  const std::string supplied = req.get_header_value("X-Request-Id");
  ctx.requestId = validRequestId(supplied) ? supplied : randomHex(16);  // never trust/echo arbitrary header text
  ctx.origin = req.get_header_value("Origin");
  ctx.isApi = req.url.rfind("/api/", 0) == 0;
}

void ApiMiddleware::before_handle(crow::request& req, crow::response& res, context& ctx) {
  ensureContext(req, ctx);
  if (req.body.size() > settings.maxBodyBytes) reject(res, ctx, 413, defaultErrorCode(413), defaultErrorMessage(413));
}

void ApiMiddleware::after_handle(crow::request& req, crow::response& res, context& ctx) {
  ensureContext(req, ctx);

  if (req.method == crow::HTTPMethod::Options && !ctx.origin.empty()) {  // CORS preflight
    if (originAllowed(ctx.origin)) {
      res.code = 204;
      res.body.clear();
      res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
      res.set_header("Access-Control-Allow-Headers", "Content-Type, X-Request-Id, Idempotency-Key, X-CSRF-Token");
      res.set_header("Access-Control-Max-Age", "600");
    } else {
      fillError(res, ctx, 403, "cors_origin_not_allowed", "Origin is not allowed");
    }
  } else if (res.code >= 400) {
    // Our own handlers produce {"error":{...}} (request id injected here). Valid JSON of any other shape
    // (e.g. the readiness report on 503) is left alone. Non-JSON bodies from the framework itself
    // (unknown route, wrong method...) become standard JSON errors.
    nlohmann::json body = nlohmann::json::parse(res.body, nullptr, false);
    if (body.is_object() && body.contains("error") && body["error"].is_object()) {
      body["error"]["request_id"] = ctx.requestId;
      res.body = body.dump();
    } else if (!body.is_object() && !body.is_array()) {
      res.body = makeErrorBody(defaultErrorCode(res.code), defaultErrorMessage(res.code), ctx.requestId).dump();
    }
    res.set_header("Content-Type", "application/json; charset=utf-8");
  }
  decorate(res, ctx);

  if (settings.logger) {
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ctx.start).count();
    // Path only: query strings may carry sensitive values.
    settings.logger->info("http request", {{"request_id", ctx.requestId},
                                          {"method", crow::method_name(req.method)},
                                          {"path", req.url},
                                          {"status", res.code},
                                          {"duration_ms", static_cast<double>(static_cast<long long>(ms * 100)) / 100.0}});
  }
}

}  // namespace carbon::http
