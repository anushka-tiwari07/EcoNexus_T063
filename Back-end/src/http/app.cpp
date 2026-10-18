#include "carbon/http/app.hpp"

#include "carbon/errors.hpp"

#include <system_error>

namespace carbon::http {
namespace {
crow::response json(int status, const nlohmann::json& body) {
  crow::response res(status, body.dump());
  res.set_header("Content-Type", "application/json; charset=utf-8");
  return res;
}

crow::response errorJson(const ApiError& e) {
  return json(e.status(), makeErrorBody(e.code(), e.what(), "", e.details()));  // request id added by middleware
}

// Handler wrapper: ApiError -> its status; anything else -> generic 500 (details only in the log).
template <class F>
crow::response guarded(Logger& logger, const std::string& requestId, F&& f) {
  try {
    return f();
  } catch (const ApiError& e) {
    return errorJson(e);
  } catch (const std::exception& e) {
    logger.error("unhandled exception in handler", {{"request_id", requestId}, {"what", e.what()}});
  } catch (...) {
    logger.error("unknown exception in handler", {{"request_id", requestId}});
  }
  return errorJson(internalError());
}
}  // namespace

void CrowLogBridge::log(const std::string& raw, crow::LogLevel level) {
  std::string message = raw;
  while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) message.pop_back();
  switch (level) {
    case crow::LogLevel::Debug: logger_.debug(message, {{"source", "crow"}}); break;
    case crow::LogLevel::Info: logger_.info(message, {{"source", "crow"}}); break;
    case crow::LogLevel::Warning: logger_.warn(message, {{"source", "crow"}}); break;
    default: logger_.error(message, {{"source", "crow"}}); break;
  }
}

void configureApp(CarbonApp& app, const Config& config, Logger& logger) {
  auto& mw = app.get_middleware<ApiMiddleware>();
  mw.settings.logger = &logger;
  mw.settings.allowedOrigins = config.allowedOrigins;
  mw.settings.maxBodyBytes = config.maxRequestBytes;
  mw.settings.hsts = config.isProduction();
  app.server_name("carbon");
  app.bindaddr(config.httpBind).port(config.httpPort).multithreaded();
}

void registerHealthRoutes(CarbonApp& app, health::HealthService& health, Logger& logger) {
  CROW_ROUTE(app, "/api/v1/health/live").methods(crow::HTTPMethod::Get)([&](const crow::request& req) {
    return guarded(logger, app.get_context<ApiMiddleware>(req).requestId, [&] { return json(200, health.live()); });
  });
  CROW_ROUTE(app, "/api/v1/health/ready").methods(crow::HTTPMethod::Get)([&](const crow::request& req) {
    return guarded(logger, app.get_context<ApiMiddleware>(req).requestId, [&] {
      const auto report = health.ready();
      return json(report.ready ? 200 : 503, report.body);
    });
  });
}

void registerStaticRoutes(CarbonApp& app, const std::filesystem::path& root) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path canonicalRoot = fs::canonical(root, ec);
  if (ec) throw std::runtime_error("frontend directory not found: " + root.string());

  const auto serve = [canonicalRoot](crow::response& res, const std::string& rel) {
    std::error_code e;
    const fs::path requested = rel.empty() ? fs::path("index.html") : fs::path(rel);
    bool bad = requested.is_absolute() || rel.find('\0') != std::string::npos;
    for (const auto& part : requested)
      if (part == ".." || (!part.empty() && part.string()[0] == '.')) bad = true;  // no traversal, no dotfiles
    const fs::path full = bad ? fs::path() : fs::weakly_canonical(canonicalRoot / requested, e);
    const bool inside = !bad && !e && full.string().rfind(canonicalRoot.string() + "/", 0) == 0;
    if (!inside || !fs::is_regular_file(full, e)) {
      res = errorJson(notFound());
      res.end();
      return;
    }
    res.set_static_file_info_unsafe(full.string());  // path is validated above
    res.end();
  };
  CROW_ROUTE(app, "/")([serve](const crow::request&, crow::response& res) { serve(res, ""); });
  CROW_ROUTE(app, "/<path>")([serve](const crow::request& req, crow::response& res, std::string rel) {
    if (req.url.rfind("/api/", 0) == 0) {  // unknown API routes must stay JSON 404s, never file lookups
      res = errorJson(notFound());
      res.end();
      return;
    }
    serve(res, rel);
  });
}

void registerFallbackRoutes(CarbonApp& app) {
  CROW_CATCHALL_ROUTE(app)([](crow::response& res) {
    res = errorJson(notFound());
    res.end();
  });
}

}  // namespace carbon::http
