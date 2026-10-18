// Authentication, sessions, CSRF, password reset, lockout, rate limiting - end to end over HTTP.
#include "api_fixture.hpp"

#include "carbon/crypto.hpp"

using namespace carbon;
using namespace carbon::tsup;
using nlohmann::json;

namespace {
json registerBody(const std::string& email = "new.user@corp.test", const std::string& org = "Corp One") {
  return {{"email", email}, {"password", kPw}, {"display_name", "New User"}, {"organization_name", org}, {"sector", "cement"}};
}
}  // namespace

using Auth = ApiFixture;

TEST_F(Auth, RegisterCreatesUserOrganizationMembershipSessionAndAudit) {
  Client c = client();
  const auto r = c.send("POST", "/api/v1/auth/register", registerBody(), false);
  ASSERT_EQ(r.status, 201) << r.body;
  const json j = r.json();
  EXPECT_EQ(j["user"]["email"], "new.user@corp.test");
  EXPECT_FALSE(j["user"]["is_platform_admin"].get<bool>());
  ASSERT_EQ(j["memberships"].size(), 1u);
  EXPECT_EQ(j["memberships"][0]["role"], "company_admin");
  EXPECT_EQ(j["csrf_token"].get<std::string>().size(), 64u);
  EXPECT_EQ(j.dump().find(kPw), std::string::npos);  // password never echoed

  const std::string cookie = r.header("set-cookie").value();
  EXPECT_NE(cookie.find("HttpOnly"), std::string::npos);
  EXPECT_NE(cookie.find("SameSite=Strict"), std::string::npos);
  EXPECT_NE(cookie.find("Path=/"), std::string::npos);
  EXPECT_EQ(cookie.find("Secure"), std::string::npos);  // development over plain http

  // Stored securely: Argon2id hash, hashed session token.
  const auto user = sql("SELECT password_hash FROM users WHERE email = 'new.user@corp.test'");
  EXPECT_EQ(user[0][0].as<std::string>().rfind("$argon2id$", 0), 0u);
  EXPECT_EQ(user[0][0].as<std::string>().find(kPw), std::string::npos);
  EXPECT_EQ(countWhere("sessions", "token_hash = '" + sha256Hex(c.sessionToken()) + "'"), 1);
  EXPECT_EQ(countWhere("sessions", "token_hash = '" + c.sessionToken() + "'"), 0);  // raw token never stored
  EXPECT_EQ(countWhere("audit_events", "action = 'user.registered'"), 1);
  EXPECT_EQ(countWhere("audit_events", "action = 'organization.created' AND organization_id IS NOT NULL"), 1);
  EXPECT_EQ(c.get("/api/v1/auth/me").status, 200);  // registration logs the user in
}

TEST_F(Auth, RegisterValidation) {
  Client c = client();
  auto post = [&](json body) { return c.send("POST", "/api/v1/auth/register", body, false); };
  json weak = registerBody(); weak["password"] = "short";
  auto r = post(weak);
  EXPECT_EQ(r.status, 422);
  EXPECT_EQ(r.json()["error"]["details"][0]["field"], "password");
  json badEmail = registerBody("not-an-email");
  EXPECT_EQ(post(badEmail).status, 422);
  json extra = registerBody(); extra["is_platform_admin"] = true;  // mass-assignment attempt
  r = post(extra);
  EXPECT_EQ(r.status, 422);
  EXPECT_EQ(countWhere("users", "is_platform_admin"), 0);
  json missing = registerBody(); missing.erase("sector");
  EXPECT_EQ(post(missing).status, 422);
  EXPECT_EQ(c.send("POST", "/api/v1/auth/register", nullptr, false, {{"Content-Type", "text/plain"}}).status, 415);
  EXPECT_EQ(countWhere("users"), 0);  // nothing was created by any rejected attempt
}

TEST_F(Auth, DuplicateEmailAndOrganizationAreConflicts) {
  Client a = client(), b = client(), c = client();
  ASSERT_EQ(a.send("POST", "/api/v1/auth/register", registerBody("dup@corp.test", "Dup Org"), false).status, 201);
  EXPECT_EQ(b.send("POST", "/api/v1/auth/register", registerBody("DUP@Corp.test", "Other Org"), false).status, 409);  // case-insensitive
  EXPECT_EQ(c.send("POST", "/api/v1/auth/register", registerBody("someone.else@corp.test", "dup org"), false).status, 409);
  EXPECT_EQ(countWhere("organizations"), 1);
  EXPECT_EQ(countWhere("users"), 1);  // the failed attempts rolled back completely
}

TEST_F(Auth, SelfRegistrationCanBeDisabled) {
  start(ApiOptions{false, false, 1000});
  Client c = client();
  const auto r = c.send("POST", "/api/v1/auth/register", registerBody(), false);
  EXPECT_EQ(r.status, 403);
  EXPECT_EQ(countWhere("users"), 0);
}

TEST_F(Auth, LoginSucceedsCaseInsensitivelyAndRehashesOldParameters) {
  makeUser("Case.User@Test.test");
  const std::string before = sql("SELECT password_hash FROM users")[0][0].as<std::string>();
  EXPECT_TRUE(strong_.needsRehash(before));
  Client c = client();
  const auto r = c.send("POST", "/api/v1/auth/login", {{"email", "case.user@test.test"}, {"password", kPw}}, false);
  ASSERT_EQ(r.status, 200) << r.body;
  const std::string after = sql("SELECT password_hash FROM users")[0][0].as<std::string>();
  EXPECT_NE(before, after);
  EXPECT_FALSE(strong_.needsRehash(after));
  EXPECT_TRUE(strong_.verify(after, kPw));
  EXPECT_EQ(loginAsStatus("case.user@test.test"), 200);  // still logs in with the upgraded hash
}

TEST_F(Auth, WrongPasswordAndUnknownEmailAreIndistinguishable) {
  makeUser("real@test.test");
  Client c1 = client(), c2 = client();
  const auto bad = c1.send("POST", "/api/v1/auth/login", {{"email", "real@test.test"}, {"password", "wrong-password-123"}}, false);
  const auto none = c2.send("POST", "/api/v1/auth/login", {{"email", "ghost@test.test"}, {"password", "wrong-password-123"}}, false);
  EXPECT_EQ(bad.status, 401);
  EXPECT_EQ(none.status, 401);
  json a = bad.json(), b = none.json();
  a["error"].erase("request_id"); b["error"].erase("request_id");
  EXPECT_EQ(a, b);
  EXPECT_FALSE(bad.header("set-cookie").has_value());
  EXPECT_EQ(countWhere("sessions"), 0);
  EXPECT_EQ(countWhere("audit_events", "action = 'auth.login.failure'"), 2);
}

TEST_F(Auth, AccountLocksAfterFiveFailuresAndRecovers) {
  makeUser("lock@test.test");
  Client c = client();
  for (int i = 0; i < 5; ++i) EXPECT_EQ(c.send("POST", "/api/v1/auth/login", {{"email", "lock@test.test"}, {"password", "nope-nope-nope-1"}}, false).status, 401);
  EXPECT_EQ(sql("SELECT locked_until IS NOT NULL AND locked_until > now() FROM users")[0][0].as<bool>(), true);
  // Even the CORRECT password is refused while locked (same response as any failure).
  EXPECT_EQ(c.send("POST", "/api/v1/auth/login", {{"email", "lock@test.test"}, {"password", kPw}}, false).status, 401);
  EXPECT_EQ(countWhere("sessions"), 0);
  sql("UPDATE users SET locked_until = now() - interval '1 second'");  // lock expires
  EXPECT_EQ(c.send("POST", "/api/v1/auth/login", {{"email", "lock@test.test"}, {"password", kPw}}, false).status, 200);
  EXPECT_EQ(sql("SELECT failed_login_count FROM users")[0][0].as<int>(), 0);
}

TEST_F(Auth, SuccessfulLoginResetsFailureCounter) {
  makeUser("reset.counter@test.test");
  Client c = client();
  for (int i = 0; i < 3; ++i) c.send("POST", "/api/v1/auth/login", {{"email", "reset.counter@test.test"}, {"password", "nope-nope-nope-1"}}, false);
  EXPECT_EQ(sql("SELECT failed_login_count FROM users")[0][0].as<int>(), 3);
  EXPECT_EQ(c.send("POST", "/api/v1/auth/login", {{"email", "reset.counter@test.test"}, {"password", kPw}}, false).status, 200);
  EXPECT_EQ(sql("SELECT failed_login_count FROM users")[0][0].as<int>(), 0);
}

TEST_F(Auth, InactiveUserCannotLogIn) {
  makeUser("gone@test.test");
  sql("UPDATE users SET is_active = false");
  Client c = client();
  EXPECT_EQ(c.send("POST", "/api/v1/auth/login", {{"email", "gone@test.test"}, {"password", kPw}}, false).status, 401);
}

TEST_F(Auth, MeRequiresAValidSession) {
  Client none = client();
  EXPECT_EQ(none.get("/api/v1/auth/me").status, 401);
  Client garbage = client();
  garbage.setCookie(std::string(64, 'a'));
  EXPECT_EQ(garbage.get("/api/v1/auth/me").status, 401);
  garbage.setCookie("short");
  EXPECT_EQ(garbage.get("/api/v1/auth/me").status, 401);
  garbage.setCookie("' OR 1=1 --");
  EXPECT_EQ(garbage.get("/api/v1/auth/me").status, 401);
  makeUser("me@test.test");
  Client ok = loginAs("me@test.test");
  const auto r = ok.get("/api/v1/auth/me");
  EXPECT_EQ(r.status, 200);
  EXPECT_EQ(r.json()["user"]["email"], "me@test.test");
}

TEST_F(Auth, SessionsExpireAbsolutelyAndWhenIdle) {
  makeUser("exp@test.test");
  Client abs = loginAs("exp@test.test");
  sql("UPDATE sessions SET created_at = now() - interval '13 hours', expires_at = now() - interval '1 minute'");
  EXPECT_EQ(abs.get("/api/v1/auth/me").status, 401);

  Client idle = loginAs("exp@test.test");
  sql("UPDATE sessions SET last_seen_at = now() - interval '3 hours' WHERE revoked_at IS NULL AND expires_at > now()");
  EXPECT_EQ(idle.get("/api/v1/auth/me").status, 401);
}

TEST_F(Auth, DeactivatedUserLosesExistingSession) {
  makeUser("later.disabled@test.test");
  Client c = loginAs("later.disabled@test.test");
  EXPECT_EQ(c.get("/api/v1/auth/me").status, 200);
  sql("UPDATE users SET is_active = false");
  EXPECT_EQ(c.get("/api/v1/auth/me").status, 401);
}

TEST_F(Auth, LogoutRevokesTheSessionServerSide) {
  makeUser("out@test.test");
  Client c = loginAs("out@test.test");
  const std::string token = c.sessionToken();
  const auto r = c.send("POST", "/api/v1/auth/logout");
  EXPECT_EQ(r.status, 204);
  EXPECT_NE(r.header("set-cookie")->find("Max-Age=0"), std::string::npos);
  Client replay = client();
  replay.setCookie(token);  // an attacker who copied the cookie earlier
  EXPECT_EQ(replay.get("/api/v1/auth/me").status, 401);
  EXPECT_EQ(countWhere("audit_events", "action = 'auth.logout'"), 1);
}

TEST_F(Auth, CsrfIsRequiredForStateChangingRequests) {
  makeUser("csrf@test.test");
  Client c = loginAs("csrf@test.test");
  auto r = c.send("POST", "/api/v1/auth/logout", nullptr, /*csrf=*/false);
  EXPECT_EQ(r.status, 403);
  EXPECT_EQ(r.json()["error"]["code"], "csrf_failed");
  r = c.send("POST", "/api/v1/auth/logout", nullptr, false, {{"X-CSRF-Token", std::string(64, '0')}});
  EXPECT_EQ(r.status, 403);
  r = c.send("POST", "/api/v1/auth/logout", nullptr, false, {{"X-CSRF-Token", ""}});
  EXPECT_EQ(r.status, 403);
  EXPECT_EQ(c.get("/api/v1/auth/me").status, 200);  // session untouched by the rejected attempts; GET needs no token
  EXPECT_EQ(c.send("POST", "/api/v1/auth/logout").status, 204);
}

TEST_F(Auth, CsrfTokenFromAnotherSessionIsRejected) {
  makeUser("one@test.test");
  makeUser("two@test.test");
  Client a = loginAs("one@test.test"), b = loginAs("two@test.test");
  EXPECT_EQ(a.send("POST", "/api/v1/auth/logout", nullptr, false, {{"X-CSRF-Token", b.csrfToken()}}).status, 403);
}

TEST_F(Auth, PasswordResetFlow) {
  makeUser("forgot@test.test");
  Client victim = loginAs("forgot@test.test");  // an existing session that must die on reset

  Client c = client();
  const auto known = c.send("POST", "/api/v1/auth/password-reset/request", {{"email", "forgot@test.test"}}, false);
  const auto unknown = c.send("POST", "/api/v1/auth/password-reset/request", {{"email", "nobody@test.test"}}, false);
  EXPECT_EQ(known.status, 202);
  EXPECT_EQ(unknown.status, 202);
  EXPECT_EQ(known.json(), unknown.json());  // no account oracle
  EXPECT_EQ(notifier.count(), 1);           // only the real account got a token
  const std::string token = notifier.tokenFor("forgot@test.test");
  ASSERT_EQ(token.size(), 64u);
  EXPECT_EQ(countWhere("password_reset_tokens", "token_hash = '" + token + "'"), 0);  // raw token not stored
  EXPECT_EQ(countWhere("password_reset_tokens", "token_hash = '" + sha256Hex(token) + "'"), 1);

  // weak new password is refused WITHOUT consuming the token
  EXPECT_EQ(c.send("POST", "/api/v1/auth/password-reset/confirm", {{"token", token}, {"new_password", "short"}}, false).status, 422);

  const std::string newPw = "brand-new-orchard-pass-77";
  EXPECT_EQ(c.send("POST", "/api/v1/auth/password-reset/confirm", {{"token", token}, {"new_password", newPw}}, false).status, 204);
  EXPECT_EQ(victim.get("/api/v1/auth/me").status, 401);  // all sessions revoked
  Client fresh = client();
  EXPECT_EQ(fresh.send("POST", "/api/v1/auth/login", {{"email", "forgot@test.test"}, {"password", kPw}}, false).status, 401);
  EXPECT_EQ(fresh.send("POST", "/api/v1/auth/login", {{"email", "forgot@test.test"}, {"password", newPw}}, false).status, 200);
  // single use
  EXPECT_EQ(c.send("POST", "/api/v1/auth/password-reset/confirm", {{"token", token}, {"new_password", "another-valid-pass-123"}}, false).status, 400);
  EXPECT_EQ(countWhere("audit_events", "action = 'auth.password_reset.completed'"), 1);
}

TEST_F(Auth, ResetTokenRulesExpiryUnknownAndSupersession) {
  makeUser("t@test.test");
  Client c = client();
  auto request = [&] { c.send("POST", "/api/v1/auth/password-reset/request", {{"email", "t@test.test"}}, false); return notifier.tokenFor("t@test.test"); };
  auto confirm = [&](const std::string& t) { return c.send("POST", "/api/v1/auth/password-reset/confirm", {{"token", t}, {"new_password", "valid-new-password-42"}}, false).status; };

  EXPECT_EQ(confirm(std::string(64, 'a')), 400);  // unknown
  EXPECT_EQ(confirm("short"), 400);

  const std::string first = request();
  const std::string second = request();  // issuing a new token invalidates the previous one
  ASSERT_NE(first, second);
  EXPECT_EQ(confirm(first), 400);

  sql("UPDATE password_reset_tokens SET created_at = now() - interval '2 hours', expires_at = now() - interval '1 hour'");
  EXPECT_EQ(confirm(second), 400);  // expired
}

TEST_F(Auth, PasswordResetClearsLockout) {
  makeUser("locked.reset@test.test");
  Client c = client();
  for (int i = 0; i < 5; ++i) c.send("POST", "/api/v1/auth/login", {{"email", "locked.reset@test.test"}, {"password", "nope-nope-nope-1"}}, false);
  c.send("POST", "/api/v1/auth/password-reset/request", {{"email", "locked.reset@test.test"}}, false);
  const std::string token = notifier.tokenFor("locked.reset@test.test");
  ASSERT_EQ(c.send("POST", "/api/v1/auth/password-reset/confirm", {{"token", token}, {"new_password", "after-lockout-pass-55"}}, false).status, 204);
  EXPECT_EQ(c.send("POST", "/api/v1/auth/login", {{"email", "locked.reset@test.test"}, {"password", "after-lockout-pass-55"}}, false).status, 200);
}

TEST_F(Auth, LoginIsRateLimitedPerClientAddress) {
  start(ApiOptions{false, true, 3});
  makeUser("rl@test.test");
  Client c = client();
  for (int i = 0; i < 3; ++i) EXPECT_EQ(c.send("POST", "/api/v1/auth/login", {{"email", "rl@test.test"}, {"password", "nope-nope-nope-1"}}, false).status, 401);
  const auto limited = c.send("POST", "/api/v1/auth/login", {{"email", "rl@test.test"}, {"password", kPw}}, false);
  EXPECT_EQ(limited.status, 429);
  EXPECT_EQ(limited.json()["error"]["code"], "rate_limited");
  ASSERT_TRUE(limited.header("retry-after").has_value());
  EXPECT_GT(std::stoi(*limited.header("retry-after")), 0);
  EXPECT_EQ(countWhere("sessions"), 0);  // the correct password was NOT even evaluated while limited
}

TEST_F(Auth, ProductionCookiesAreSecureAndHostPrefixed) {
  start(ApiOptions{true, true, 1000});
  Client c = client();
  const auto r = c.send("POST", "/api/v1/auth/register", registerBody(), false);
  ASSERT_EQ(r.status, 201) << r.body;
  const std::string cookie = r.header("set-cookie").value();
  EXPECT_EQ(cookie.rfind("__Host-carbon_session=", 0), 0u);
  EXPECT_NE(cookie.find("; Secure"), std::string::npos);
  EXPECT_NE(cookie.find("HttpOnly"), std::string::npos);
  EXPECT_TRUE(r.header("strict-transport-security").has_value());
  EXPECT_EQ(c.get("/api/v1/auth/me").status, 200);
}

TEST_F(Auth, ConcurrentRegistrationsOfOneEmailYieldExactlyOneAccount) {
  std::atomic<int> created{0}, conflicts{0}, other{0};
  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i) {
    threads.emplace_back([&, i] {
      Client c = client();
      const auto r = c.send("POST", "/api/v1/auth/register", registerBody("race@corp.test", "Race Org " + std::to_string(i)), false);
      if (r.status == 201) ++created; else if (r.status == 409) ++conflicts; else ++other;
    });
  }
  for (auto& t : threads) t.join();
  EXPECT_EQ(created.load(), 1);
  EXPECT_EQ(conflicts.load(), 7);
  EXPECT_EQ(other.load(), 0);
  EXPECT_EQ(countWhere("users"), 1);
  EXPECT_EQ(countWhere("organizations"), 1);  // losers left no orphan organisations behind
}

TEST_F(Auth, LogsNeverContainPasswordsOrSessionTokens) {
  Client c = client();
  c.send("POST", "/api/v1/auth/register", registerBody("logs@corp.test", "Log Org"), false);
  const std::string token = c.sessionToken(), csrf = c.csrfToken();
  c.send("POST", "/api/v1/auth/login", {{"email", "logs@corp.test"}, {"password", "wrong-password-xyz-1"}}, false);
  c.send("POST", "/api/v1/auth/password-reset/request", {{"email", "logs@corp.test"}}, false);
  const std::string resetToken = notifier.tokenFor("logs@corp.test");
  const std::string logs = logsAfterStop();
  for (const std::string& secret : {kPw, std::string("wrong-password-xyz-1"), token, csrf, resetToken})
    EXPECT_EQ(logs.find(secret), std::string::npos) << "secret leaked into logs";
  EXPECT_NE(logs.find("http request"), std::string::npos);
}
