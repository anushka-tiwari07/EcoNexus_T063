#include "carbon/http/app.hpp"

#include "test_support.hpp"

#include <fstream>
#include <future>

using namespace carbon;
using namespace carbon::http;
using tsup::httpRequest;
namespace fs = std::filesystem;

namespace {
// Temp site: <tmp>/secret.txt is OUTSIDE the served root <tmp>/site.
class Site {
 public:
  Site() : base_(fs::temp_directory_path() / ("carbon_site_" + randomHex(6))) {
    fs::create_directories(base_ / "site" / "js");
    std::ofstream(base_ / "site" / "index.html") << "<!doctype html><title>home</title>";
    std::ofstream(base_ / "site" / "js" / "app.js") << "console.log('hi');";
    std::ofstream(base_ / "site" / ".hidden") << "dotfile-content";
    std::ofstream(base_ / "secret.txt") << "top-secret-content";
  }
  ~Site() {
    std::error_code ec;
    fs::remove_all(base_, ec);
  }
  fs::path root() const { return base_ / "site"; }

 private:
  fs::path base_;
};

class FrontendServer {
 public:
  explicit FrontendServer(const fs::path& root) : port_(tsup::freePort()), logger_(logs_, LogLevel::Error), health_("test", logger_) {
    Config cfg;
    cfg.databaseUrl = "postgresql://u:p@localhost/x";
    cfg.httpPort = port_;
    configureApp(app_, cfg, logger_);
    registerHealthRoutes(app_, health_, logger_);
    registerStaticRoutes(app_, root);
    registerFallbackRoutes(app_);
    crow::logger::setLogLevel(crow::LogLevel::Critical);
    future_ = app_.run_async();
    app_.wait_for_server_start();
  }
  ~FrontendServer() {
    app_.stop();
    future_.wait();
  }
  std::uint16_t port() const { return port_; }

 private:
  std::uint16_t port_;
  std::ostringstream logs_;
  Logger logger_;
  health::HealthService health_;
  CarbonApp app_;
  std::future<void> future_;
};
}  // namespace

TEST(Frontend, ServesIndexAndNestedFilesWithFrontendHeaders) {
  Site site;
  FrontendServer s(site.root());
  const auto index = httpRequest(s.port(), "GET", "/");
  EXPECT_EQ(index.status, 200);
  EXPECT_NE(index.body.find("<title>home</title>"), std::string::npos);
  EXPECT_NE(index.header("content-security-policy")->find("script-src 'self'"), std::string::npos);
  EXPECT_EQ(index.header("cache-control"), "no-cache");
  EXPECT_EQ(index.header("x-content-type-options"), "nosniff");

  const auto js = httpRequest(s.port(), "GET", "/js/app.js");
  EXPECT_EQ(js.status, 200);
  EXPECT_NE(js.body.find("console.log"), std::string::npos);
}

TEST(Frontend, ApiResponsesKeepStrictHeaders) {
  Site site;
  FrontendServer s(site.root());
  const auto live = httpRequest(s.port(), "GET", "/api/v1/health/live");
  EXPECT_EQ(live.status, 200);
  EXPECT_EQ(live.header("cache-control"), "no-store");
  EXPECT_EQ(live.header("content-security-policy"), "default-src 'none'; frame-ancestors 'none'");
}

TEST(Frontend, UnknownApiPathIsJson404NotAFileLookup) {
  Site site;
  FrontendServer s(site.root());
  const auto r = httpRequest(s.port(), "GET", "/api/v1/nope");
  EXPECT_EQ(r.status, 404);
  EXPECT_EQ(r.json()["error"]["code"], "not_found");
  EXPECT_EQ(r.header("cache-control"), "no-store");
}

TEST(Frontend, MissingFileIs404) {
  Site site;
  FrontendServer s(site.root());
  EXPECT_EQ(httpRequest(s.port(), "GET", "/nope.html").status, 404);
  EXPECT_EQ(httpRequest(s.port(), "GET", "/js").status, 404);  // directories are not served
}

TEST(Frontend, TraversalAndDotfilesAreNeverServed) {
  Site site;
  FrontendServer s(site.root());
  for (const char* path : {"/../secret.txt", "/..%2fsecret.txt", "/%2e%2e/secret.txt", "/js/../../secret.txt", "/.hidden"}) {
    const auto r = httpRequest(s.port(), "GET", path);
    EXPECT_NE(r.status, 200) << path;
    EXPECT_EQ(r.body.find("top-secret-content"), std::string::npos) << path;
    EXPECT_EQ(r.body.find("dotfile-content"), std::string::npos) << path;
  }
}

TEST(Frontend, MissingDirectoryFailsFast) {
  CarbonApp app;
  EXPECT_THROW(registerStaticRoutes(app, fs::temp_directory_path() / ("no_such_dir_" + randomHex(6))), std::runtime_error);
}
