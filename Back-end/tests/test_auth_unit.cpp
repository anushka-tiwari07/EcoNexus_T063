// Pure-logic tests (no database): password policy/hashing, rate limiting, roles, ids, body validation.
#include "carbon/auth/password.hpp"
#include "carbon/auth/principal.hpp"
#include "carbon/auth/rate_limiter.hpp"
#include "carbon/errors.hpp"
#include "carbon/paging.hpp"
#include "carbon/request_info.hpp"
#include "carbon/uuid.hpp"
#include "carbon/validation.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>

using namespace carbon;
using namespace carbon::auth;
using namespace std::chrono_literals;

// ---------- password policy ----------
TEST(PasswordPolicy, EnforcesLengthBounds) {
  EXPECT_TRUE(validatePasswordPolicy("short1!", "a@b.test").has_value());
  EXPECT_TRUE(validatePasswordPolicy(std::string(129, 'x') + "Q1", "a@b.test").has_value());
  EXPECT_FALSE(validatePasswordPolicy("correct horse battery staple", "a@b.test").has_value());
}

TEST(PasswordPolicy, RejectsWeakPatterns) {
  EXPECT_TRUE(validatePasswordPolicy("            ", "a@b.test").has_value());      // whitespace only
  EXPECT_TRUE(validatePasswordPolicy("aaaaaaaaaaaaaaaa", "a@b.test").has_value());  // single repeated character
  EXPECT_TRUE(validatePasswordPolicy("MyPassword1234!", "a@b.test").has_value());   // contains a common password
  EXPECT_TRUE(validatePasswordPolicy("jonathan.smith-2026", "jonathan.smith@corp.test").has_value());  // contains email name
  EXPECT_FALSE(validatePasswordPolicy("sunny-meadow-tractor-91", "jonathan.smith@corp.test").has_value());
}

// ---------- Argon2id ----------
TEST(Argon2, ProducesVerifiablePhcStrings) {
  const auto h = ArgonPasswordHasher::cheapForTests();
  const std::string a = h.hash("correct horse battery staple"), b = h.hash("correct horse battery staple");
  EXPECT_EQ(a.rfind("$argon2id$", 0), 0u);
  EXPECT_NE(a, b);  // random salt
  EXPECT_TRUE(h.verify(a, "correct horse battery staple"));
  EXPECT_FALSE(h.verify(a, "correct horse battery stapl"));
  EXPECT_FALSE(h.verify(a, ""));
  EXPECT_EQ(a.find("correct"), std::string::npos);  // plaintext never appears
}

TEST(Argon2, MalformedHashesFailClosedWithoutThrowing) {
  const auto h = ArgonPasswordHasher::cheapForTests();
  EXPECT_FALSE(h.verify("", "x"));
  EXPECT_FALSE(h.verify("plaintext", "plaintext"));
  EXPECT_FALSE(h.verify("$argon2id$garbage", "x"));
  EXPECT_FALSE(h.verify(std::string(500, 'a'), "x"));
  EXPECT_TRUE(h.needsRehash("not-a-hash"));
}

TEST(Argon2, DetectsWeakerParametersForRehash) {
  const auto cheap = ArgonPasswordHasher::cheapForTests();
  const auto stronger = ArgonPasswordHasher(3, 16 * 1024 * 1024);
  const std::string old = cheap.hash("correct horse battery staple");
  EXPECT_FALSE(cheap.needsRehash(old));
  EXPECT_TRUE(stronger.needsRehash(old));
  EXPECT_TRUE(stronger.verify(old, "correct horse battery staple"));  // old hashes still verify
}

// ---------- rate limiter ----------
TEST(RateLimiter, BlocksAfterLimitAndRecoversWithTime) {
  auto now = std::chrono::steady_clock::now();
  RateLimiter rl(3, 60s, [&] { return now; });
  for (int i = 0; i < 3; ++i) EXPECT_TRUE(rl.hit("ip").allowed);
  const auto blocked = rl.hit("ip");
  EXPECT_FALSE(blocked.allowed);
  EXPECT_GT(blocked.retryAfter.count(), 0);
  EXPECT_LE(blocked.retryAfter.count(), 61);
  now += 30s;
  EXPECT_FALSE(rl.hit("ip").allowed);  // window not over
  now += 31s;
  EXPECT_TRUE(rl.hit("ip").allowed);   // oldest events expired
}

TEST(RateLimiter, KeysAreIndependentAndBlockedHitsDoNotExtendTheBan) {
  auto now = std::chrono::steady_clock::now();
  RateLimiter rl(2, 10s, [&] { return now; });
  rl.hit("a"); rl.hit("a");
  EXPECT_FALSE(rl.hit("a").allowed);
  EXPECT_TRUE(rl.hit("b").allowed);
  for (int i = 0; i < 50; ++i) rl.hit("a");  // hammering must not push the window forward
  now += 11s;
  EXPECT_TRUE(rl.hit("a").allowed);
}

// ---------- roles ----------
TEST(Roles, ParseRoundTripAndRejectUnknown) {
  for (const char* r : {"company_admin", "company_operator", "consultant", "reviewer"}) EXPECT_EQ(toString(*parseRole(r)), r);
  EXPECT_FALSE(parseRole("platform_admin").has_value());  // platform admin is a user flag, never an org role
  EXPECT_FALSE(parseRole("COMPANY_ADMIN").has_value());
  EXPECT_FALSE(parseRole("").has_value());
}

TEST(Roles, PermissionMatrixIsExactlyAsDocumented) {
  using P = Permission;
  struct Row { Role role; std::vector<P> allowed; };
  const std::vector<Row> matrix = {
      {Role::CompanyAdmin, {P::OrgRead, P::OrgUpdate, P::MembersRead, P::MembersManage, P::FacilityRead, P::FacilityWrite, P::AuditRead}},
      {Role::CompanyOperator, {P::OrgRead, P::FacilityRead}},
      {Role::Consultant, {P::OrgRead, P::FacilityRead, P::FacilityWrite, P::AuditRead}},
      {Role::Reviewer, {P::OrgRead, P::FacilityRead, P::AuditRead}}};
  for (const auto& row : matrix)
    for (int p = 0; p <= static_cast<int>(P::AuditRead); ++p) {
      const bool expected = std::find(row.allowed.begin(), row.allowed.end(), static_cast<P>(p)) != row.allowed.end();
      EXPECT_EQ(roleHas(row.role, static_cast<P>(p)), expected) << toString(row.role) << " permission " << p;
    }
}

TEST(Roles, PlatformAdminHasNoFacilityAccess) {
  EXPECT_FALSE(platformAdminHas(Permission::FacilityRead));
  EXPECT_FALSE(platformAdminHas(Permission::FacilityWrite));
  EXPECT_FALSE(platformAdminHas(Permission::OrgUpdate));
  EXPECT_TRUE(platformAdminHas(Permission::MembersManage));
}

namespace {
Principal principal(bool platformAdmin, std::vector<Membership> m) {
  return Principal{*Uuid::parse("11111111-1111-1111-1111-111111111111"), "u@x.test", "U", platformAdmin, std::move(m),
                   *Uuid::parse("22222222-2222-2222-2222-222222222222"), "csrf"};
}
const Uuid kOrgA = *Uuid::parse("aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa");
const Uuid kOrgB = *Uuid::parse("bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb");
}  // namespace

TEST(Authorization, NonMemberSeesNotFoundMemberWithoutPermissionSeesForbidden) {
  const auto op = principal(false, {{kOrgA, Role::CompanyOperator}});
  EXPECT_NO_THROW(requireOrgPermission(op, kOrgA, Permission::FacilityRead));
  try { requireOrgPermission(op, kOrgA, Permission::FacilityWrite); FAIL(); } catch (const ApiError& e) { EXPECT_EQ(e.status(), 403); }
  try { requireOrgPermission(op, kOrgB, Permission::FacilityRead); FAIL(); } catch (const ApiError& e) { EXPECT_EQ(e.status(), 404); }
}

TEST(Authorization, PlatformAdminLimitedToPlatformPermissions) {
  const auto pa = principal(true, {});
  EXPECT_NO_THROW(requireOrgPermission(pa, kOrgA, Permission::MembersManage));
  EXPECT_NO_THROW(requirePlatformAdmin(pa));
  try { requireOrgPermission(pa, kOrgA, Permission::FacilityRead); FAIL(); } catch (const ApiError& e) { EXPECT_EQ(e.status(), 404); }
  EXPECT_THROW(requirePlatformAdmin(principal(false, {{kOrgA, Role::CompanyAdmin}})), ApiError);
}

TEST(Authorization, MembershipInOneOrgGivesNothingInAnother) {
  const auto admin = principal(false, {{kOrgA, Role::CompanyAdmin}});
  for (int p = 0; p <= static_cast<int>(Permission::AuditRead); ++p) {
    try { requireOrgPermission(admin, kOrgB, static_cast<Permission>(p)); FAIL() << p; } catch (const ApiError& e) { EXPECT_EQ(e.status(), 404); }
  }
}

// ---------- ids / helpers ----------
TEST(Uuid, ParsesAndNormalises) {
  EXPECT_EQ(Uuid::parse("AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE")->str(), "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
  for (const char* bad : {"", "not-a-uuid", "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeeg", "aaaaaaaabbbbccccddddeeeeeeeeeeee",
                          "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee ", "'; DROP TABLE users;--"})
    EXPECT_FALSE(Uuid::parse(bad).has_value()) << bad;
}

TEST(RequestInfo, InetValidationAndSanitising) {
  EXPECT_TRUE(inetOrNull("127.0.0.1").has_value());
  EXPECT_TRUE(inetOrNull("::1").has_value());
  EXPECT_FALSE(inetOrNull("999.1.1.1").has_value());
  EXPECT_FALSE(inetOrNull("1.1.1.1'; --").has_value());
  EXPECT_FALSE(inetOrNull("").has_value());
  EXPECT_EQ(sanitizeHeaderText("Mozilla\r\nX-Evil: 1\x01"), "MozillaX-Evil: 1");
  EXPECT_EQ(sanitizeHeaderText(std::string(2000, 'a')).size(), 512u);
}

TEST(Paging, EscapesLikeWildcards) {
  EXPECT_EQ(escapeLike("50%_off\\"), "50\\%\\_off\\\\");
  EXPECT_EQ(escapeLike("plain"), "plain");
}

// ---------- body validation ----------
TEST(BodyReader, ValidBodyPasses) {
  const nlohmann::json j = {{"name", "  Plant 1  "}, {"active", true}};
  BodyReader r(j);
  EXPECT_EQ(r.requireString("name", 1, 20), "Plant 1");  // trimmed
  EXPECT_EQ(r.optionalBool("active"), true);
  EXPECT_FALSE(r.optionalString("missing", 1, 5).has_value());
  EXPECT_NO_THROW(r.finish());
}

TEST(BodyReader, CollectsEveryProblemAndRejectsUnknownFields) {
  const nlohmann::json j = {{"name", ""}, {"sector", 5}, {"organization_id", "smuggled"}, {"note", "bad\u0001char"}};
  BodyReader r(j);
  r.requireString("name", 1, 20);
  r.requireString("sector", 1, 20);
  r.requireString("required_field", 1, 20);
  r.optionalString("note", 1, 20);
  try {
    r.finish();
    FAIL() << "expected 422";
  } catch (const ApiError& e) {
    EXPECT_EQ(e.status(), 422);
    std::set<std::string> fields;
    for (const auto& p : e.details()) fields.insert(p["field"].get<std::string>());
    EXPECT_EQ(fields, (std::set<std::string>{"name", "sector", "required_field", "note", "organization_id"}));
  }
}

TEST(BodyReader, SecretsAreNotTrimmedAndNotEchoed) {
  const nlohmann::json j = {{"password", "  spaces kept  "}};
  BodyReader r(j);
  EXPECT_EQ(r.requireSecret("password", 1, 50), "  spaces kept  ");
  const nlohmann::json bad = {{"password", 12}};
  BodyReader r2(bad);
  r2.requireSecret("password", 1, 50);
  try { r2.finish(); FAIL(); } catch (const ApiError& e) { EXPECT_EQ(e.details().dump().find("12"), std::string::npos); }
}

TEST(BodyReader, NonObjectBodyIs400) {
  EXPECT_THROW(BodyReader(nlohmann::json::array()), ApiError);
  EXPECT_THROW(BodyReader(nlohmann::json("str")), ApiError);
}

TEST(ParseJson, ContentTypeAndSyntaxErrors) {
  try { parseJsonObject("{}", "text/plain"); FAIL(); } catch (const ApiError& e) { EXPECT_EQ(e.status(), 415); }
  try { parseJsonObject("", "application/json"); FAIL(); } catch (const ApiError& e) { EXPECT_EQ(e.status(), 400); }
  try { parseJsonObject("{not json", "application/json"); FAIL(); } catch (const ApiError& e) { EXPECT_EQ(e.status(), 400); }
  EXPECT_NO_THROW(parseJsonObject("{\"a\":1}", "application/json; charset=utf-8"));
}

TEST(Email, Shape) {
  EXPECT_TRUE(looksLikeEmail("a.b+c@example.co.in"));
  for (const char* bad : {"", "a", "a@b", "@b.test", "a@@b.test", "a b@c.test", "a@b.", "a@.b"}) EXPECT_FALSE(looksLikeEmail(bad)) << bad;
}

#include "carbon/auth/reset_notifier.hpp"
#include <sstream>

TEST(ResetNotifier, DevelopmentLogsTheCodeProductionNeverDoes) {
  std::ostringstream devLog, prodLog;
  Logger dev(devLog, LogLevel::Info), prod(prodLog, LogLevel::Debug);
  const std::string token(64, 'e');
  auth::LogResetNotifier(dev, true).send("u@x.test", token, std::chrono::hours(1));
  auth::LogResetNotifier(prod, false).send("u@x.test", token, std::chrono::hours(1));
  EXPECT_NE(devLog.str().find(token), std::string::npos);
  EXPECT_EQ(prodLog.str().find(token), std::string::npos);
  EXPECT_NE(prodLog.str().find("no email transport"), std::string::npos);
}
