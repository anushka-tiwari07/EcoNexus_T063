// Organisations, memberships, facilities, audit history: tenant isolation and role permissions over HTTP.
#include "api_fixture.hpp"

#include <atomic>
#include <thread>

using namespace carbon;
using namespace carbon::tsup;
using nlohmann::json;

using Org = ApiFixture;

namespace {
std::string orgPath(const std::string& org, const std::string& tail = "") { return "/api/v1/organizations/" + org + tail; }
json facilityBody(const std::string& name) { return {{"name", name}, {"sector", "cement"}, {"production_unit", "tonne cement"}}; }
}  // namespace

// ---------------- every declared route really exists and is protected ----------------
TEST_F(Org, EveryDeclaredRouteExistsAndRequiresAuthenticationWhereExpected) {
  const std::string zero = "00000000-0000-0000-0000-000000000000";
  const std::set<std::string> publicPaths = {"/api/v1/health/live", "/api/v1/health/ready", "/api/v1/auth/register", "/api/v1/auth/login",
                                             "/api/v1/auth/password-reset/request", "/api/v1/auth/password-reset/confirm"};
  Client anon = client();
  for (const auto& route : http::kApiRoutes) {
    std::string path(route.path);
    for (const char* p : {"{orgId}", "{membershipId}", "{facilityId}"}) {
      const auto at = path.find(p);
      if (at != std::string::npos) path.replace(at, std::string(p).size(), zero);
    }
    const auto r = anon.send(std::string(route.method), path, route.method == "GET" ? json(nullptr) : json::object(), false);
    const std::string label = std::string(route.method) + " " + std::string(route.path);
    EXPECT_NE(r.status, 404) << label;  // would mean the route does not exist
    EXPECT_NE(r.status, 405) << label;
    EXPECT_LT(r.status, 500) << label;
    if (!publicPaths.count(std::string(route.path))) {
      EXPECT_EQ(r.status, 401) << label << " must require authentication";
    }
  }
}

// ---------------- tenant isolation ----------------
TEST_F(Org, UserCannotSeeOrTouchAnotherTenant) {
  const World w = makeWorld();
  Client b = loginAs("admin.b@test.test");
  const std::string leaked = "Plant A1";

  // listing shows only the caller's own organisations
  auto r = b.get("/api/v1/organizations");
  ASSERT_EQ(r.status, 200);
  ASSERT_EQ(r.json()["data"].size(), 1u);
  EXPECT_EQ(r.json()["data"][0]["id"], w.orgB);

  // every way of reaching org A returns 404 (existence not revealed), never 403 and never data
  for (const std::string& path : {orgPath(w.orgA), orgPath(w.orgA, "/members"), orgPath(w.orgA, "/facilities"), orgPath(w.orgA, "/facilities/" + w.facA),
                                  orgPath(w.orgA, "/audit-events")}) {
    r = b.get(path);
    EXPECT_EQ(r.status, 404) << path;
    EXPECT_EQ(r.body.find(leaked), std::string::npos) << path;
  }
  EXPECT_EQ(b.send("PATCH", orgPath(w.orgA), {{"sector", "hacked"}}).status, 404);
  EXPECT_EQ(b.send("POST", orgPath(w.orgA, "/facilities"), facilityBody("Injected")).status, 404);
  EXPECT_EQ(b.send("PATCH", orgPath(w.orgA, "/facilities/" + w.facA), {{"name", "Hijacked"}}).status, 404);
  EXPECT_EQ(b.send("POST", orgPath(w.orgA, "/members"), {{"email", "admin.b@test.test"}, {"role", "company_admin"}}).status, 404);

  // nothing changed
  EXPECT_EQ(sql("SELECT sector FROM organizations WHERE id = $1::uuid", w.orgA)[0][0].as<std::string>(), "cement");
  EXPECT_EQ(sql("SELECT name FROM facilities WHERE id = $1::uuid", w.facA)[0][0].as<std::string>(), "Plant A1");
  EXPECT_EQ(countWhere("facilities", "name = 'Injected'"), 0);
  EXPECT_EQ(countWhere("organization_memberships", "organization_id = '" + w.orgA + "' AND user_id = '" + w.userAdminB + "'"), 0);
}

TEST_F(Org, FacilityIdFromAnotherTenantIsNotReachableThroughMyOrganisationPath) {
  const World w = makeWorld();
  Client a = loginAs("admin.a@test.test");
  EXPECT_EQ(a.get(orgPath(w.orgA, "/facilities/" + w.facB)).status, 404);
  EXPECT_EQ(a.send("PATCH", orgPath(w.orgA, "/facilities/" + w.facB), {{"name", "Stolen"}}).status, 404);
  EXPECT_EQ(sql("SELECT name FROM facilities WHERE id = $1::uuid", w.facB)[0][0].as<std::string>(), "Plant B1");
}

TEST_F(Org, MembershipIdOfAnotherOrganisationCannotBeModified) {
  const World w = makeWorld();
  Client a = loginAs("admin.a@test.test");
  const std::string bMembership = sql("SELECT id FROM organization_memberships WHERE organization_id = $1::uuid AND user_id = $2::uuid", w.orgB, w.userAdminB)[0][0].as<std::string>();
  EXPECT_EQ(a.send("PATCH", orgPath(w.orgA, "/members/" + bMembership), {{"role", "reviewer"}}).status, 404);
  EXPECT_EQ(a.send("DELETE", orgPath(w.orgA, "/members/" + bMembership)).status, 404);
  EXPECT_EQ(countWhere("organization_memberships", "id = '" + bMembership + "' AND role = 'company_admin'"), 1);
}

TEST_F(Org, ConsultantWorksInMultipleClientsButSeesEachOnlyThroughItsOwnMembership) {
  const World w = makeWorld();
  Client c = loginAs("consultant@test.test");
  const auto r = c.get("/api/v1/organizations");
  EXPECT_EQ(r.json()["data"].size(), 2u);
  EXPECT_EQ(c.get(orgPath(w.orgA, "/facilities")).status, 200);
  EXPECT_EQ(c.get(orgPath(w.orgB, "/facilities")).status, 200);
  const std::string orgC = makeOrg("Org C (test)");
  EXPECT_EQ(c.get(orgPath(orgC, "/facilities")).status, 404);  // not a client of C
}

TEST_F(Org, MalformedAndMassAssignmentInputsAreRejected) {
  const World w = makeWorld();
  Client a = loginAs("admin.a@test.test");
  EXPECT_EQ(a.get(orgPath("not-a-uuid")).status, 404);
  EXPECT_EQ(a.get(orgPath("%27%20OR%201%3D1%20--")).status, 404);
  auto body = facilityBody("Smuggler");
  body["organization_id"] = w.orgB;  // try to plant a facility in another tenant
  auto r = a.send("POST", orgPath(w.orgA, "/facilities"), body);
  EXPECT_EQ(r.status, 422);
  EXPECT_EQ(countWhere("facilities", "name = 'Smuggler'"), 0);
  r = a.send("PATCH", orgPath(w.orgA, "/facilities/" + w.facA), {{"organization_id", w.orgB}});
  EXPECT_EQ(r.status, 422);
  EXPECT_EQ(sql("SELECT organization_id FROM facilities WHERE id = $1::uuid", w.facA)[0][0].as<std::string>(), w.orgA);
  EXPECT_EQ(a.send("PATCH", orgPath(w.orgA), {{"is_active", false}}).status, 422);  // not a patchable field
}

// ---------------- roles ----------------
TEST_F(Org, PermissionMatrixOverHttp) {
  const World w = makeWorld();
  makeUser("newbie@test.test");
  struct Actor { std::string name; Client c; };
  std::vector<Actor> actors;
  actors.push_back({"admin", loginAs("admin.a@test.test")});
  actors.push_back({"operator", loginAs("operator.a@test.test")});
  actors.push_back({"reviewer", loginAs("reviewer.a@test.test")});
  actors.push_back({"consultant", loginAs("consultant@test.test")});

  int n = 0;
  struct Op { std::string label; std::function<HttpResponse(Client&)> call; std::vector<int> expected; };  // expected: admin, operator, reviewer, consultant
  const std::vector<Op> ops = {
      {"GET org", [&](Client& c) { return c.get(orgPath(w.orgA)); }, {200, 200, 200, 200}},
      {"PATCH org", [&](Client& c) { return c.send("PATCH", orgPath(w.orgA), {{"state", "Gujarat"}}); }, {200, 403, 403, 403}},
      {"GET members", [&](Client& c) { return c.get(orgPath(w.orgA, "/members")); }, {200, 403, 403, 403}},
      {"POST member", [&](Client& c) { return c.send("POST", orgPath(w.orgA, "/members"), {{"email", "newbie@test.test"}, {"role", "reviewer"}}); }, {201, 403, 403, 403}},
      {"GET facilities", [&](Client& c) { return c.get(orgPath(w.orgA, "/facilities")); }, {200, 200, 200, 200}},
      {"GET facility", [&](Client& c) { return c.get(orgPath(w.orgA, "/facilities/" + w.facA)); }, {200, 200, 200, 200}},
      {"POST facility", [&](Client& c) { return c.send("POST", orgPath(w.orgA, "/facilities"), facilityBody("F" + std::to_string(++n))); }, {201, 403, 403, 201}},
      {"PATCH facility", [&](Client& c) { return c.send("PATCH", orgPath(w.orgA, "/facilities/" + w.facA), {{"state", "Goa"}}); }, {200, 403, 403, 200}},
      {"GET audit", [&](Client& c) { return c.get(orgPath(w.orgA, "/audit-events")); }, {200, 403, 200, 200}},
  };
  for (const auto& op : ops)
    for (std::size_t i = 0; i < actors.size(); ++i) {
      const auto r = op.call(actors[i].c);
      EXPECT_EQ(r.status, op.expected[i]) << actors[i].name << " " << op.label << " -> " << r.body;
    }
}

TEST_F(Org, WriteRequestsNeedCsrfEvenForPrivilegedRoles) {
  const World w = makeWorld();
  Client a = loginAs("admin.a@test.test");
  const auto r = a.send("POST", orgPath(w.orgA, "/facilities"), facilityBody("NoCsrf"), /*csrf=*/false);
  EXPECT_EQ(r.status, 403);
  EXPECT_EQ(r.json()["error"]["code"], "csrf_failed");
  EXPECT_EQ(countWhere("facilities", "name = 'NoCsrf'"), 0);
}

TEST_F(Org, PlatformAdminManagesPlatformButNotCompanyData) {
  const World w = makeWorld();
  Client p = loginAs("platform@test.test");
  Client a = loginAs("admin.a@test.test");

  EXPECT_EQ(a.send("POST", "/api/v1/organizations", {{"name", "Rogue Org"}, {"sector", "steel"}}).status, 403);  // only platform admins create orgs
  auto r = p.send("POST", "/api/v1/organizations", {{"name", "Created By Platform"}, {"sector", "steel"}, {"gstin", "27ABCDE1234F1Z5"}});
  ASSERT_EQ(r.status, 201) << r.body;
  const std::string newOrg = r.json()["id"];
  EXPECT_EQ(p.send("POST", "/api/v1/organizations", {{"name", "Bad Gstin"}, {"sector", "steel"}, {"gstin", "lowercase1234567"}}).status, 422);
  EXPECT_EQ(p.send("POST", "/api/v1/organizations", {{"name", "created by platform"}, {"sector", "steel"}}).status, 409);

  // sees every organisation, may bootstrap the first admin of a new one
  r = p.get("/api/v1/organizations?limit=100");
  EXPECT_EQ(r.json()["page"]["total"], 3);
  EXPECT_EQ(p.get(orgPath(w.orgA)).status, 200);
  EXPECT_EQ(p.get(orgPath(w.orgA, "/members")).status, 200);
  EXPECT_EQ(p.send("POST", orgPath(newOrg, "/members"), {{"email", "admin.a@test.test"}, {"role", "company_admin"}}).status, 201);
  EXPECT_EQ(p.get(orgPath(w.orgA, "/audit-events")).status, 200);

  // ...but has NO implicit access to company facility data
  EXPECT_EQ(p.get(orgPath(w.orgA, "/facilities")).status, 404);
  EXPECT_EQ(p.get(orgPath(w.orgA, "/facilities/" + w.facA)).status, 404);
  EXPECT_EQ(p.send("POST", orgPath(w.orgA, "/facilities"), facilityBody("PlatformInjected")).status, 404);
  EXPECT_EQ(p.send("PATCH", orgPath(w.orgA), {{"sector", "x1"}}).status, 404);
}

// ---------------- memberships ----------------
TEST_F(Org, MembershipLifecycle) {
  const World w = makeWorld();
  makeUser("joiner@test.test");
  Client a = loginAs("admin.a@test.test");
  auto r = a.send("POST", orgPath(w.orgA, "/members"), {{"email", "Joiner@Test.test"}, {"role", "company_operator"}});
  ASSERT_EQ(r.status, 201) << r.body;
  const std::string mid = r.json()["id"];
  EXPECT_EQ(r.json()["role"], "company_operator");
  EXPECT_EQ(a.send("POST", orgPath(w.orgA, "/members"), {{"email", "joiner@test.test"}, {"role", "reviewer"}}).status, 409);
  EXPECT_EQ(a.send("POST", orgPath(w.orgA, "/members"), {{"email", "nobody@test.test"}, {"role", "reviewer"}}).status, 404);
  EXPECT_EQ(a.send("POST", orgPath(w.orgA, "/members"), {{"email", "joiner@test.test"}, {"role", "platform_admin"}}).status, 422);
  EXPECT_EQ(a.send("POST", orgPath(w.orgA, "/members"), {{"email", "joiner@test.test"}, {"role", "COMPANY_ADMIN"}}).status, 422);

  r = a.send("PATCH", orgPath(w.orgA, "/members/" + mid), {{"role", "reviewer"}});
  EXPECT_EQ(r.status, 200);
  EXPECT_EQ(r.json()["role"], "reviewer");
  EXPECT_EQ(a.send("DELETE", orgPath(w.orgA, "/members/" + mid)).status, 204);
  EXPECT_EQ(a.send("DELETE", orgPath(w.orgA, "/members/" + mid)).status, 404);
  EXPECT_EQ(a.get(orgPath(w.orgA, "/members?q=joiner")).json()["page"]["total"], 0);
}

TEST_F(Org, OrganisationAlwaysKeepsAtLeastOneAdmin) {
  const World w = makeWorld();
  Client a = loginAs("admin.a@test.test");
  const std::string self = sql("SELECT id FROM organization_memberships WHERE organization_id = $1::uuid AND user_id = $2::uuid", w.orgA, w.userAdminA)[0][0].as<std::string>();
  EXPECT_EQ(a.send("PATCH", orgPath(w.orgA, "/members/" + self), {{"role", "reviewer"}}).status, 409);
  EXPECT_EQ(a.send("DELETE", orgPath(w.orgA, "/members/" + self)).status, 409);
  // with a second admin, one may step down
  makeUser("second.admin@test.test");
  const std::string m2 = a.send("POST", orgPath(w.orgA, "/members"), {{"email", "second.admin@test.test"}, {"role", "company_admin"}}).json()["id"];
  EXPECT_EQ(a.send("PATCH", orgPath(w.orgA, "/members/" + self), {{"role", "reviewer"}}).status, 200);
  Client second = loginAs("second.admin@test.test");
  EXPECT_EQ(second.send("DELETE", orgPath(w.orgA, "/members/" + m2)).status, 409);  // now the last admin
}

TEST_F(Org, ConcurrentMutualDemotionNeverLeavesAnOrganisationWithoutAdmin) {
  const std::string org = makeOrg("Race Org (test)");
  const std::string p = makeUser("p.admin@test.test"), q = makeUser("q.admin@test.test");
  makeMember(org, p, "company_admin");
  makeMember(org, q, "company_admin");
  const std::string mp = sql("SELECT id FROM organization_memberships WHERE user_id = $1::uuid", p)[0][0].as<std::string>();
  const std::string mq = sql("SELECT id FROM organization_memberships WHERE user_id = $1::uuid", q)[0][0].as<std::string>();
  Client cp = loginAs("p.admin@test.test"), cq = loginAs("q.admin@test.test");
  for (int round = 0; round < 8; ++round) {
    sql("UPDATE organization_memberships SET role = 'company_admin' WHERE organization_id = $1::uuid", org);
    std::atomic<int> ok{0}, rejected{0}, other{0};
    auto go = [&](Client& c, const std::string& target) {
      const auto r = c.send("PATCH", orgPath(org, "/members/" + target), {{"role", "company_operator"}});
      // loser: 409 (both passed the permission check, the DB lock serialised them) or 403 (the winner had
      // already demoted it before it authenticated) - both preserve the invariant.
      if (r.status == 200) ++ok; else if (r.status == 409 || r.status == 403) ++rejected; else ++other;
    };
    std::thread t1([&] { go(cp, mq); }), t2([&] { go(cq, mp); });
    t1.join(); t2.join();
    EXPECT_EQ(other.load(), 0) << "round " << round;
    EXPECT_EQ(ok.load(), 1) << "round " << round;
    EXPECT_EQ(rejected.load(), 1) << "round " << round;
    EXPECT_GE(countWhere("organization_memberships", "organization_id = '" + org + "' AND role = 'company_admin'"), 1) << "round " << round;
  }
}

// ---------------- facilities ----------------
TEST_F(Org, FacilityCrud) {
  const World w = makeWorld();
  Client a = loginAs("admin.a@test.test");
  auto r = a.send("POST", orgPath(w.orgA, "/facilities"), {{"name", "Kiln 7"}, {"sector", "cement"}, {"production_unit", "tonne cement"}, {"state", "Gujarat"}, {"boundary_notes", "Kiln + mill"}});
  ASSERT_EQ(r.status, 201) << r.body;
  const json f = r.json();
  EXPECT_EQ(f["organization_id"], w.orgA);
  EXPECT_EQ(f["is_active"], true);
  EXPECT_EQ(*r.header("location"), orgPath(w.orgA, "/facilities/" + f["id"].get<std::string>()));

  EXPECT_EQ(a.send("POST", orgPath(w.orgA, "/facilities"), facilityBody("Kiln 7")).status, 409);  // duplicate name
  // same name in another tenant is fine
  Client b = loginAs("admin.b@test.test");
  EXPECT_EQ(b.send("POST", orgPath(w.orgB, "/facilities"), facilityBody("Kiln 7")).status, 201);

  const std::string path = orgPath(w.orgA, "/facilities/" + f["id"].get<std::string>());
  r = a.send("PATCH", path, {{"address", "Plot 4"}, {"production_unit", "tonne clinker"}});
  EXPECT_EQ(r.status, 200);
  EXPECT_EQ(r.json()["address"], "Plot 4");
  EXPECT_EQ(r.json()["name"], "Kiln 7");  // untouched fields stay
  EXPECT_EQ(a.send("PATCH", path, {{"is_active", false}}).json()["is_active"], false);
  EXPECT_EQ(a.get(path).json()["is_active"], false);
  EXPECT_EQ(a.send("PATCH", path, json::object()).status, 422);  // empty patch
  EXPECT_EQ(a.send("PATCH", path, {{"name", "Plant A1"}}).status, 409);  // renaming onto an existing name
  EXPECT_EQ(a.get(orgPath(w.orgA, "/facilities/00000000-0000-0000-0000-000000000000")).status, 404);
}

TEST_F(Org, FacilityValidation) {
  const World w = makeWorld();
  Client a = loginAs("admin.a@test.test");
  const std::string path = orgPath(w.orgA, "/facilities");
  EXPECT_EQ(a.send("POST", path, {{"name", "No Unit"}, {"sector", "cement"}}).status, 422);
  EXPECT_EQ(a.send("POST", path, {{"name", ""}, {"sector", "cement"}, {"production_unit", "t"}}).status, 422);
  EXPECT_EQ(a.send("POST", path, {{"name", std::string(121, 'x')}, {"sector", "cement"}, {"production_unit", "t"}}).status, 422);
  EXPECT_EQ(a.send("POST", path, {{"name", 5}, {"sector", "cement"}, {"production_unit", "t"}}).status, 422);
  EXPECT_EQ(a.send("POST", path, {{"name", "Ctl\u0007Char"}, {"sector", "cement"}, {"production_unit", "t"}}).status, 422);
  EXPECT_EQ(a.send("POST", path, json::array()).status, 400);
  EXPECT_EQ(a.send("POST", path, nullptr, true, {{"Content-Type", "application/json"}}).status, 400);  // empty body
  EXPECT_EQ(httpRequest(port_, "POST", path, {{"Content-Type", "application/json"}, {"Cookie", "carbon_session=" + a.sessionToken()}, {"X-CSRF-Token", a.csrfToken()}}, "{not json").status, 400);
  EXPECT_EQ(httpRequest(port_, "POST", path, {{"Content-Type", "text/plain"}, {"Cookie", "carbon_session=" + a.sessionToken()}, {"X-CSRF-Token", a.csrfToken()}}, "x").status, 415);
  EXPECT_EQ(countWhere("facilities", "organization_id = '" + w.orgA + "'"), 1);  // only the seeded one
}

TEST_F(Org, PaginationFilteringAndSorting) {
  const World w = makeWorld();
  Client a = loginAs("admin.a@test.test");
  for (const char* n : {"Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot", "100% Plant", "Under_score"})
    makeFacility(w.orgA, n);
  makeFacility(w.orgA, "Inactive One");
  sql("UPDATE facilities SET is_active = false WHERE name = 'Inactive One'");
  const std::string base = orgPath(w.orgA, "/facilities");

  auto r = a.get(base + "?limit=3&offset=0&sort=name&order=asc").json();
  EXPECT_EQ(r["page"]["total"], 10);  // 9 created + Plant A1
  ASSERT_EQ(r["data"].size(), 3u);
  EXPECT_EQ(r["data"][0]["name"], "100% Plant");
  r = a.get(base + "?limit=3&offset=9").json();
  EXPECT_EQ(r["data"].size(), 1u);  // last page
  r = a.get(base + "?limit=3&offset=100").json();
  EXPECT_EQ(r["data"].size(), 0u);
  EXPECT_EQ(r["page"]["total"], 10);
  r = a.get(base + "?sort=name&order=desc&limit=2").json();
  EXPECT_EQ(r["data"][0]["name"], "Under_score");
  EXPECT_EQ(a.get(base + "?q=ALPHA").json()["data"].size(), 1u);  // case-insensitive
  EXPECT_EQ(a.get(base + "?is_active=false").json()["page"]["total"], 1);
  EXPECT_EQ(a.get(base + "?is_active=true").json()["page"]["total"], 9);
  // LIKE wildcards in user text are matched literally
  EXPECT_EQ(a.get(base + "?q=%25").json()["page"]["total"], 1);       // '%' only matches "100% Plant"
  EXPECT_EQ(a.get(base + "?q=_").json()["page"]["total"], 1);         // '_' only matches "Under_score"
  // invalid parameters are rejected, not ignored
  for (const char* bad : {"?limit=0", "?limit=101", "?limit=abc", "?offset=-1", "?sort=password", "?order=sideways", "?is_active=maybe"})
    EXPECT_EQ(a.get(base + bad).status, 422) << bad;
}

TEST_F(Org, SqlInjectionAttemptsAreHarmless) {
  const World w = makeWorld();
  Client a = loginAs("admin.a@test.test");
  const std::string base = orgPath(w.orgA, "/facilities");
  EXPECT_EQ(a.get(base + "?q=%27%20OR%20%271%27%3D%271").json()["data"].size(), 0u);
  EXPECT_EQ(a.get(base + "?q=%27%3B%20DROP%20TABLE%20users%3B--").status, 200);
  const std::string nasty = "x'); DROP TABLE facilities;--";
  auto r = a.send("POST", base, {{"name", nasty}, {"sector", "cement"}, {"production_unit", "t"}});
  ASSERT_EQ(r.status, 201);
  EXPECT_EQ(r.json()["name"], nasty);  // stored literally
  EXPECT_EQ(a.get(base + "?q=DROP").json()["data"].size(), 1u);
  EXPECT_GE(countWhere("users"), 1);
  EXPECT_GE(countWhere("facilities"), 3);
  EXPECT_EQ(a.get(base + "?sort=name;DROP%20TABLE%20users").status, 422);
}

// ---------------- audit ----------------
TEST_F(Org, SensitiveActionsAreAuditedAndVisibleOnlyToTheirOrganisation) {
  const World w = makeWorld();
  makeUser("joiner@test.test");
  Client a = loginAs("admin.a@test.test"), b = loginAs("admin.b@test.test");
  const std::string fid = a.send("POST", orgPath(w.orgA, "/facilities"), facilityBody("Audited")).json()["id"];
  a.send("PATCH", orgPath(w.orgA, "/facilities/" + fid), {{"state", "Kerala"}});
  const std::string mid = a.send("POST", orgPath(w.orgA, "/members"), {{"email", "joiner@test.test"}, {"role", "reviewer"}}).json()["id"];
  a.send("PATCH", orgPath(w.orgA, "/members/" + mid), {{"role", "consultant"}});
  a.send("DELETE", orgPath(w.orgA, "/members/" + mid));
  b.send("POST", orgPath(w.orgB, "/facilities"), facilityBody("B Only"));

  auto r = a.get(orgPath(w.orgA, "/audit-events?limit=100"));
  ASSERT_EQ(r.status, 200);
  std::set<std::string> actions;
  const json events = r.json();  // keep alive: iterating a temporary's member would dangle
  for (const auto& e : events["data"]) {
    actions.insert(e["action"]);
    EXPECT_EQ(e["actor_user_id"], w.userAdminA);
    EXPECT_FALSE(e["request_id"].get<std::string>().empty());
  }
  for (const char* expected : {"facility.created", "facility.updated", "membership.added", "membership.role_changed", "membership.removed"})
    EXPECT_TRUE(actions.count(expected)) << expected;
  EXPECT_EQ(r.body.find("B Only"), std::string::npos);  // other tenant's events never appear
  EXPECT_EQ(r.body.find("password"), std::string::npos);

  const auto filtered = a.get(orgPath(w.orgA, "/audit-events?action=membership"));
  EXPECT_EQ(filtered.json()["page"]["total"], 3);
  const auto paged = a.get(orgPath(w.orgA, "/audit-events?limit=2&offset=0"));
  EXPECT_EQ(paged.json()["data"].size(), 2u);
  EXPECT_EQ(paged.json()["page"]["total"], 5);
  // newest first by default
  EXPECT_EQ(paged.json()["data"][0]["action"], "membership.removed");
  EXPECT_EQ(b.get(orgPath(w.orgB, "/audit-events")).json()["page"]["total"], 1);
}

TEST_F(Org, AuditTrailCannotBeAlteredThroughTheDatabaseEither) {
  const World w = makeWorld();
  Client a = loginAs("admin.a@test.test");
  a.send("POST", orgPath(w.orgA, "/facilities"), facilityBody("Immutable Trail"));
  EXPECT_THROW(sql("UPDATE audit_events SET action = 'tampered'"), pqxx::sql_error);
  EXPECT_THROW(sql("DELETE FROM audit_events"), pqxx::sql_error);
}

TEST_F(Org, FailedOperationsLeaveNoAuditOrDataBehind) {
  const World w = makeWorld();
  Client a = loginAs("admin.a@test.test");
  const long before = countWhere("audit_events", "organization_id = '" + w.orgA + "'");
  EXPECT_EQ(a.send("POST", orgPath(w.orgA, "/facilities"), facilityBody("Plant A1")).status, 409);  // duplicate -> rollback
  EXPECT_EQ(countWhere("audit_events", "organization_id = '" + w.orgA + "'"), before);
}

TEST_F(Org, OversizedRequestBodiesAreRejected) {
  const World w = makeWorld();
  Client a = loginAs("admin.a@test.test");
  json big = facilityBody("Big");
  big["boundary_notes"] = std::string(2 * 1024 * 1024, 'x');  // > CARBON_MAX_REQUEST_BYTES (1 MiB)
  EXPECT_EQ(a.send("POST", orgPath(w.orgA, "/facilities"), big).status, 413);
  EXPECT_EQ(countWhere("facilities", "name = 'Big'"), 0);
}
