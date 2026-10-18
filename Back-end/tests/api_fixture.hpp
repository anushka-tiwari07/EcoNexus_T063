// Full-stack fixture: temp PostgreSQL database + migrations + every service + real Crow server.
// All users/organisations created here are FABRICATED test data.
#pragma once

#include "carbon/audit/audit_service.hpp"
#include "carbon/auth/auth_service.hpp"
#include "carbon/auth/password.hpp"
#include "carbon/auth/rate_limiter.hpp"
#include "carbon/auth/reset_notifier.hpp"
#include "carbon/auth/session_service.hpp"
#include "carbon/db/migrator.hpp"
#include "carbon/domain/facility_service.hpp"
#include "carbon/domain/organization_service.hpp"
#include "carbon/http/api_routes.hpp"

#include "test_support.hpp"

#include <future>
#include <map>
#include <mutex>
#include <sstream>

namespace carbon::tsup {

inline const std::string kPw = "sunny-meadow-tractor-91";

// Captures reset tokens instead of "sending" them.
class CapturingNotifier final : public auth::ResetNotifier {
 public:
  void send(const std::string& email, const std::string& token, std::chrono::seconds) override {
    std::lock_guard<std::mutex> l(m_);
    tokens_[email] = token;
    ++count_;
  }
  std::string tokenFor(const std::string& email) const { std::lock_guard<std::mutex> l(m_); auto it = tokens_.find(email); return it == tokens_.end() ? "" : it->second; }
  int count() const { std::lock_guard<std::mutex> l(m_); return count_; }

 private:
  mutable std::mutex m_;
  std::map<std::string, std::string> tokens_;
  int count_ = 0;
};

// HTTP client with a cookie jar and CSRF handling (like the browser frontend will behave).
class Client {
 public:
  explicit Client(std::uint16_t port, std::string cookieName = "carbon_session") : port_(port), cookieName_(std::move(cookieName)) {}

  HttpResponse send(const std::string& method, const std::string& path, const nlohmann::json& body = nullptr, bool csrf = true,
                    std::map<std::string, std::string> extra = {}) {
    std::map<std::string, std::string> h = std::move(extra);
    if (!body.is_null()) h["Content-Type"] = "application/json";
    if (!cookie_.empty()) h["Cookie"] = cookieName_ + "=" + cookie_;
    if (csrf && !csrf_.empty()) h["X-CSRF-Token"] = csrf_;
    HttpResponse r = httpRequest(port_, method, path, h, body.is_null() ? "" : body.dump());
    for (const auto& [k, v] : r.headers) {
      if (k != "set-cookie") continue;
      const auto semi = v.find(';');
      const std::string pair = v.substr(0, semi);
      const auto eq = pair.find('=');
      cookie_ = pair.substr(eq + 1);
      cookieName_ = pair.substr(0, eq);
    }
    if (r.status < 300 && !r.body.empty() && nlohmann::json::accept(r.body)) {
      const auto j = nlohmann::json::parse(r.body);
      if (j.is_object() && j.contains("csrf_token")) csrf_ = j["csrf_token"].get<std::string>();
    }
    return r;
  }
  HttpResponse get(const std::string& path) { return send("GET", path); }
  const std::string& sessionToken() const { return cookie_; }
  const std::string& csrfToken() const { return csrf_; }
  void setCookie(const std::string& v) { cookie_ = v; }

 private:
  std::uint16_t port_;
  std::string cookieName_, cookie_, csrf_;
};

struct ApiOptions {
  bool production = false;
  bool selfRegistration = true;
  std::size_t loginLimit = 1000;
};

class ApiFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    REQUIRE_TEST_DB();
    start(ApiOptions{});
  }
  void TearDown() override { stop(); }

  void start(const ApiOptions& o) {
    stop();
    db_ = std::make_unique<TempDatabase>();
    {
      pqxx::connection c(db_->url());
      db::applyMigrations(c, db::loadMigrations(migrationsDir()));
    }
    port_ = freePort();
    config_ = Config{};
    config_.databaseUrl = db_->url();
    config_.httpPort = port_;
    config_.environment = o.production ? Config::Environment::Production : Config::Environment::Development;
    config_.allowSelfRegistration = o.selfRegistration;
    logger_ = std::make_unique<Logger>(logs_, LogLevel::Info);
    pool_ = std::make_unique<db::ConnectionPool>(db_->url(), 8);
    audit_ = std::make_unique<audit::AuditService>(*pool_);
    sessions_ = std::make_unique<auth::SessionService>(*pool_, auth::SessionSettings{});
    auth_ = std::make_unique<auth::AuthService>(*pool_, strong_, *sessions_, *audit_, notifier, auth::AuthSettings{o.selfRegistration, 5, std::chrono::minutes(15), std::chrono::hours(1)});
    orgs_ = std::make_unique<domain::OrganizationService>(*pool_, *audit_);
    facilities_ = std::make_unique<domain::FacilityService>(*pool_, *audit_);
    loginLimiter_ = std::make_unique<auth::RateLimiter>(o.loginLimit, std::chrono::seconds(60));
    registerLimiter_ = std::make_unique<auth::RateLimiter>(1000, std::chrono::seconds(60));
    resetLimiter_ = std::make_unique<auth::RateLimiter>(1000, std::chrono::seconds(60));
    health_ = std::make_unique<health::HealthService>("test", *logger_);
    health_->addProbe(std::make_unique<health::DatabaseProbe>(*pool_));
    deps_ = std::make_unique<http::ApiDeps>(http::ApiDeps{config_, *logger_, *sessions_, *auth_, *orgs_, *facilities_, *audit_, *loginLimiter_, *registerLimiter_, *resetLimiter_});
    app_ = std::make_unique<http::CarbonApp>();
    http::configureApp(*app_, config_, *logger_);
    http::registerHealthRoutes(*app_, *health_, *logger_);
    http::registerApiRoutes(*app_, *deps_);
    http::registerFallbackRoutes(*app_);
    crow::logger::setLogLevel(crow::LogLevel::Critical);
    future_ = app_->run_async();
    app_->wait_for_server_start();
  }

  void stop() {
    if (app_) {
      app_->stop();
      future_.wait();
      app_.reset();
    }
    deps_.reset(); health_.reset(); registerLimiter_.reset(); resetLimiter_.reset(); loginLimiter_.reset();
    facilities_.reset(); orgs_.reset(); auth_.reset(); sessions_.reset(); audit_.reset(); pool_.reset(); db_.reset();
  }

  Client client() const { return Client(port_, config_.isProduction() ? "__Host-carbon_session" : "carbon_session"); }

  // ---- direct DB helpers (arrange phase) ----
  template <class... A>
  pqxx::result sql(const std::string& q, A&&... args) {
    return pool_->inTransaction([&](pqxx::work& tx) { return tx.exec_params(q, std::forward<A>(args)...); });
  }
  std::string makeUser(const std::string& email, bool platformAdmin = false, const std::string& pw = kPw) {
    return sql("INSERT INTO users (email, password_hash, display_name, is_platform_admin) VALUES ($1, $2, $3, $4) RETURNING id", email, cheap_.hash(pw), email, platformAdmin)[0][0].as<std::string>();
  }
  std::string makeOrg(const std::string& name) { return sql("INSERT INTO organizations (name, sector) VALUES ($1, 'cement') RETURNING id", name)[0][0].as<std::string>(); }
  void makeMember(const std::string& org, const std::string& user, const std::string& role) {
    sql("INSERT INTO organization_memberships (organization_id, user_id, role) VALUES ($1::uuid, $2::uuid, $3)", org, user, role);
  }
  std::string makeFacility(const std::string& org, const std::string& name) {
    return sql("INSERT INTO facilities (organization_id, name, sector, production_unit) VALUES ($1::uuid, $2, 'cement', 'tonne') RETURNING id", org, name)[0][0].as<std::string>();
  }
  Client loginAs(const std::string& email, const std::string& pw = kPw) {
    Client c = client();
    const auto r = c.send("POST", "/api/v1/auth/login", {{"email", email}, {"password", pw}}, false);
    EXPECT_EQ(r.status, 200) << r.body;
    return c;
  }
  int loginAsStatus(const std::string& email, const std::string& pw = kPw) {
    Client c = client();
    return c.send("POST", "/api/v1/auth/login", {{"email", email}, {"password", pw}}, false).status;
  }
  long countWhere(const std::string& table, const std::string& where = "true") { return sql("SELECT count(*) FROM " + table + " WHERE " + where)[0][0].as<long>(); }
  std::string logsAfterStop() { stop(); return logs_.str(); }

  // A two-tenant world used by the isolation / permission tests.
  struct World {
    std::string orgA, orgB, facA, facB, userAdminA, userAdminB, userOp, userReviewer, userConsultant, userPlatform;
  };
  World makeWorld() {
    World w;
    w.orgA = makeOrg("Org A (test)");
    w.orgB = makeOrg("Org B (test)");
    w.userAdminA = makeUser("admin.a@test.test");
    w.userAdminB = makeUser("admin.b@test.test");
    w.userOp = makeUser("operator.a@test.test");
    w.userReviewer = makeUser("reviewer.a@test.test");
    w.userConsultant = makeUser("consultant@test.test");
    w.userPlatform = makeUser("platform@test.test", true);
    makeMember(w.orgA, w.userAdminA, "company_admin");
    makeMember(w.orgA, w.userOp, "company_operator");
    makeMember(w.orgA, w.userReviewer, "reviewer");
    makeMember(w.orgA, w.userConsultant, "consultant");
    makeMember(w.orgB, w.userAdminB, "company_admin");
    makeMember(w.orgB, w.userConsultant, "consultant");
    w.facA = makeFacility(w.orgA, "Plant A1");
    w.facB = makeFacility(w.orgB, "Plant B1");
    return w;
  }

  std::uint16_t port_ = 0;
  Config config_;
  std::ostringstream logs_;
  CapturingNotifier notifier;
  auth::ArgonPasswordHasher cheap_ = auth::ArgonPasswordHasher::cheapForTests();
  auth::ArgonPasswordHasher strong_ = auth::ArgonPasswordHasher(2, 32 * 1024);  // slightly stronger => seeded users get re-hashed on login
  std::unique_ptr<TempDatabase> db_;
  std::unique_ptr<Logger> logger_;
  std::unique_ptr<db::ConnectionPool> pool_;
  std::unique_ptr<audit::AuditService> audit_;
  std::unique_ptr<auth::SessionService> sessions_;
  std::unique_ptr<auth::AuthService> auth_;
  std::unique_ptr<domain::OrganizationService> orgs_;
  std::unique_ptr<domain::FacilityService> facilities_;
  std::unique_ptr<auth::RateLimiter> loginLimiter_, registerLimiter_, resetLimiter_;
  std::unique_ptr<health::HealthService> health_;
  std::unique_ptr<http::ApiDeps> deps_;
  std::unique_ptr<http::CarbonApp> app_;
  std::future<void> future_;
};

}  // namespace carbon::tsup
