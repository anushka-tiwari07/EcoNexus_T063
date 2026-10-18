#include "carbon/http/app.hpp"

#include "test_support.hpp"

#include <sstream>
#include <thread>

using namespace carbon;
using namespace carbon::http;
using tsup::httpRequest;

namespace {
class FakeProbe final : public health::HealthProbe {
 public:
  FakeProbe(std::string n, bool ok, bool throws = false) : name_(std::move(n)), ok_(ok), throws_(throws) {}
  std::string name() const override { return name_; }
  health::ProbeResult check() override {
    if (throws_) throw std::runtime_error("secret-internal-detail");
    return {ok_, {}};
  }

 private:
  std::string name_;
  bool ok_, throws_;
};

// Real Crow server on an ephemeral port, so middleware runs exactly as in production.
class TestServer {
 public:
  explicit TestServer(Config cfg = defaultConfig(), bool probeOk = true, bool probeThrows = false)
      : port_(tsup::freePort()), logger_(logs_, LogLevel::Info), health_("test", logger_) {
    cfg.httpPort = port_;
    health_.addProbe(std::make_unique<FakeProbe>("fake", probeOk, probeThrows));
    configureApp(app_, cfg, logger_);
    registerHealthRoutes(app_, health_, logger_);
    registerFallbackRoutes(app_);
    crow::logger::setLogLevel(crow::LogLevel::Critical);
    future_ = app_.run_async();
    app_.wait_for_server_start();
  }
  ~TestServer() {
    app_.stop();
    future_.wait();
  }
  std::uint16_t port() const { return port_; }
  std::string logs() const { return logs_.str(); }

  static Config defaultConfig() {
    Config c;
    c.databaseUrl = "postgresql://u:p@localhost/x";
    c.allowedOrigins = {"https://app.example.test"};
    c.maxRequestBytes = 1024;
    return c;
  }

 private:
  std::uint16_t port_;
  std::ostringstream logs_;
  Logger logger_;
  health::HealthService health_;
  CarbonApp app_;
  std::future<void> future_;
};
}  // namespace

TEST(Http, LiveReturnsJsonWithSecurityHeaders) {
  TestServer s;
  const auto r = httpRequest(s.port(), "GET", "/api/v1/health/live");
  EXPECT_EQ(r.status, 200);
  EXPECT_EQ(r.json()["status"], "ok");
  EXPECT_NE(r.header("content-type")->find("application/json"), std::string::npos);
  EXPECT_EQ(r.header("x-content-type-options"), "nosniff");
  EXPECT_EQ(r.header("x-frame-options"), "DENY");
  EXPECT_EQ(r.header("cache-control"), "no-store");
  EXPECT_TRUE(r.header("content-security-policy").has_value());
  EXPECT_FALSE(r.header("strict-transport-security").has_value());  // development
  EXPECT_FALSE(r.header("access-control-allow-origin").has_value());
}

TEST(Http, RequestIdGeneratedEchoedAndSanitised) {
  TestServer s;
  const auto generated = httpRequest(s.port(), "GET", "/api/v1/health/live");
  ASSERT_TRUE(generated.header("x-request-id"));
  EXPECT_EQ(generated.header("x-request-id")->size(), 32u);

  const auto echoed = httpRequest(s.port(), "GET", "/api/v1/health/live", {{"X-Request-Id", "client-req_123"}});
  EXPECT_EQ(echoed.header("x-request-id"), "client-req_123");

  const auto replaced = httpRequest(s.port(), "GET", "/api/v1/health/live", {{"X-Request-Id", "bad id<script>"}});
  EXPECT_NE(replaced.header("x-request-id"), "bad id<script>");
  EXPECT_EQ(replaced.header("x-request-id")->size(), 32u);
}

TEST(Http, UnknownRouteGivesStandardJson404WithRequestId) {
  TestServer s;
  const auto r = httpRequest(s.port(), "GET", "/api/v1/nope", {{"X-Request-Id", "abc123"}});
  EXPECT_EQ(r.status, 404);
  const auto e = r.json();
  EXPECT_EQ(e["error"]["code"], "not_found");
  EXPECT_EQ(e["error"]["request_id"], "abc123");
  EXPECT_EQ(r.header("x-request-id"), "abc123");
}

TEST(Http, WrongMethodGivesJsonError) {
  TestServer s;
  const auto r = httpRequest(s.port(), "POST", "/api/v1/health/live", {}, "{}");
  EXPECT_TRUE(r.status == 405 || r.status == 404) << r.status;
  EXPECT_TRUE(r.json().contains("error"));
  EXPECT_TRUE(r.json()["error"].contains("request_id"));
}

TEST(Http, ReadyReflectsProbeResults) {
  {
    TestServer ok;
    const auto r = httpRequest(ok.port(), "GET", "/api/v1/health/ready");
    EXPECT_EQ(r.status, 200);
    EXPECT_EQ(r.json()["status"], "ready");
  }
  {
    TestServer bad(TestServer::defaultConfig(), false);
    const auto r = httpRequest(bad.port(), "GET", "/api/v1/health/ready");
    EXPECT_EQ(r.status, 503);
    EXPECT_EQ(r.json()["checks"]["fake"]["status"], "fail") << r.body;
  }
}

TEST(Http, ThrowingProbeDoesNotLeakInternals) {
  TestServer s(TestServer::defaultConfig(), true, true);
  const auto r = httpRequest(s.port(), "GET", "/api/v1/health/ready");
  EXPECT_EQ(r.status, 503);
  EXPECT_EQ(r.body.find("secret-internal-detail"), std::string::npos);
  EXPECT_NE(s.logs().find("secret-internal-detail"), std::string::npos);  // but it IS in the server log
}

TEST(Http, CorsAllowListIsExactAndNeverWildcard) {
  TestServer s;
  const auto ok = httpRequest(s.port(), "GET", "/api/v1/health/live", {{"Origin", "https://app.example.test"}});
  EXPECT_EQ(ok.header("access-control-allow-origin"), "https://app.example.test");
  const auto other = httpRequest(s.port(), "GET", "/api/v1/health/live", {{"Origin", "https://evil.example.test"}});
  EXPECT_FALSE(other.header("access-control-allow-origin").has_value());
  const auto prefix = httpRequest(s.port(), "GET", "/api/v1/health/live", {{"Origin", "https://app.example.test.evil.test"}});
  EXPECT_FALSE(prefix.header("access-control-allow-origin").has_value());
}

TEST(Http, CorsPreflight) {
  TestServer s;
  const auto ok = httpRequest(s.port(), "OPTIONS", "/api/v1/health/live", {{"Origin", "https://app.example.test"}, {"Access-Control-Request-Method", "GET"}});
  EXPECT_EQ(ok.status, 204);
  EXPECT_EQ(ok.header("access-control-allow-origin"), "https://app.example.test");
  EXPECT_TRUE(ok.header("access-control-allow-methods").has_value());
  EXPECT_TRUE(ok.header("x-request-id").has_value());

  const auto denied = httpRequest(s.port(), "OPTIONS", "/api/v1/health/live", {{"Origin", "https://evil.example.test"}});
  EXPECT_EQ(denied.status, 403);
  EXPECT_EQ(denied.json()["error"]["code"], "cors_origin_not_allowed");
  EXPECT_FALSE(denied.header("access-control-allow-origin").has_value());
}

TEST(Http, OversizedBodyRejectedWith413) {
  TestServer s;  // limit 1024 bytes
  const auto r = httpRequest(s.port(), "POST", "/api/v1/health/live", {}, std::string(4096, 'x'));
  EXPECT_EQ(r.status, 413);
  EXPECT_EQ(r.json()["error"]["code"], "payload_too_large");
}

TEST(Http, HstsOnlyInProduction) {
  Config c = TestServer::defaultConfig();
  c.environment = Config::Environment::Production;
  TestServer s(c);
  EXPECT_TRUE(httpRequest(s.port(), "GET", "/api/v1/health/live").header("strict-transport-security").has_value());
}

TEST(Http, AccessLogHasRequestIdAndNeverQueryString) {
  TestServer s;
  httpRequest(s.port(), "GET", "/api/v1/health/live?token=topsecretvalue", {{"X-Request-Id", "log-check-1"}});
  std::string logs;
  for (int i = 0; i < 20 && logs.find("log-check-1") == std::string::npos; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    logs = s.logs();
  }
  EXPECT_NE(logs.find("log-check-1"), std::string::npos);
  EXPECT_EQ(logs.find("topsecretvalue"), std::string::npos);
  EXPECT_NE(logs.find("/api/v1/health/live"), std::string::npos);
}

TEST(Http, EveryDeclaredRouteReallyExists) {
  TestServer s;
  for (const auto& route : kApiRoutes) {
    if (route.path.substr(0, 15) != "/api/v1/health/") continue;  // business routes are verified in test_api_integration.cpp
    const auto r = httpRequest(s.port(), std::string(route.method), std::string(route.path));
    EXPECT_TRUE(r.status == 200 || r.status == 503) << route.path << " -> " << r.status;
  }
}
