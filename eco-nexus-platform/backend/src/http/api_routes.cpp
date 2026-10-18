#include "carbon/http/api_routes.hpp"

#include "carbon/crypto.hpp"
#include "carbon/errors.hpp"
#include "carbon/validation.hpp"

#include <openssl/crypto.h>

#include <charconv>
#include <regex>

namespace carbon::http {
namespace {
using auth::Permission;
using auth::Principal;

// ---------- request plumbing ----------
crow::response jsonResponse(int status, const nlohmann::json& body) {
  crow::response res(status, body.dump());
  res.set_header("Content-Type", "application/json; charset=utf-8");
  return res;
}
crow::response noContent() { return crow::response(204); }

crow::response errorResponse(const ApiError& e) {
  crow::response res = jsonResponse(e.status(), makeErrorBody(e.code(), e.what(), "", e.details()));
  if (e.status() == 429 && e.details().is_object() && e.details().contains("retry_after_seconds"))
    res.set_header("Retry-After", std::to_string(e.details()["retry_after_seconds"].get<long long>()));
  return res;
}

Uuid parseId(const std::string& text) {
  if (auto id = Uuid::parse(text)) return *id;
  throw notFound();  // a malformed id cannot exist
}

void rateLimit(auth::RateLimiter& limiter, const crow::request& req) {
  const auto d = limiter.hit(req.remote_ip_address);
  if (!d.allowed) throw ApiError(429, "rate_limited", "Too many requests; try again later", {{"retry_after_seconds", d.retryAfter.count()}});
}

std::optional<std::string> cookieValue(const std::string& header, const std::string& name) {
  std::size_t pos = 0;
  while (pos < header.size()) {
    const auto end = header.find(';', pos);
    std::string part = header.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    const auto first = part.find_first_not_of(' ');
    if (first != std::string::npos) part.erase(0, first);
    const auto eq = part.find('=');
    if (eq != std::string::npos && part.compare(0, eq, name) == 0) return part.substr(eq + 1);
    if (end == std::string::npos) break;
    pos = end + 1;
  }
  return std::nullopt;
}

bool constantTimeEquals(const std::string& a, const std::string& b) {
  return a.size() == b.size() && CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

bool isUnsafe(crow::HTTPMethod m) {
  return m == crow::HTTPMethod::Post || m == crow::HTTPMethod::Put || m == crow::HTTPMethod::Patch || m == crow::HTTPMethod::Delete;
}

PageRequest parsePage(const crow::request& req, std::initializer_list<const char*> sorts, const char* defaultSort, bool activeFilter = false, bool actionFilter = false) {
  PageRequest p;
  p.sort = defaultSort;
  nlohmann::json problems = nlohmann::json::array();
  auto problem = [&](const char* f, const std::string& m) { problems.push_back({{"field", f}, {"message", m}}); };
  auto number = [&](const char* name, std::size_t lo, std::size_t hi, std::size_t& out) {
    const char* raw = req.url_params.get(name);
    if (!raw) return;
    std::size_t v = 0;
    const std::string s = raw;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (s.empty() || ec != std::errc{} || ptr != s.data() + s.size() || v < lo || v > hi) problem(name, "must be an integer between " + std::to_string(lo) + " and " + std::to_string(hi));
    else out = v;
  };
  number("limit", 1, 100, p.limit);
  number("offset", 0, 1000000000, p.offset);
  if (const char* s = req.url_params.get("sort")) {
    bool ok = false;
    for (const char* allowed : sorts) ok = ok || std::string(allowed) == s;
    if (ok) p.sort = s; else problem("sort", "unsupported sort field");
  }
  if (const char* o = req.url_params.get("order")) {
    const std::string order = o;
    if (order == "asc") p.descending = false;
    else if (order == "desc") p.descending = true;
    else problem("order", "must be 'asc' or 'desc'");
  }
  if (const char* q = req.url_params.get("q")) {
    p.q = q;
    if (p.q.size() > 100) problem("q", "must be at most 100 characters");
    for (const unsigned char c : p.q) if (c < 0x20 || c == 0x7f) { problem("q", "must not contain control characters"); break; }
  }
  if (activeFilter) {
    if (const char* a = req.url_params.get("is_active")) {
      const std::string v = a;
      if (v == "true") p.isActive = true; else if (v == "false") p.isActive = false; else problem("is_active", "must be 'true' or 'false'");
    }
  }
  if (actionFilter) {
    if (const char* a = req.url_params.get("action")) {
      p.action = a;
      if (p.action.size() > 64) problem("action", "must be at most 64 characters");
    }
  }
  if (!problems.empty()) throw unprocessable("Invalid query parameters", problems);
  return p;
}

nlohmann::json sessionJson(const Principal& p, bool withCsrf) {
  nlohmann::json memberships = nlohmann::json::array();
  for (const auto& m : p.memberships) memberships.push_back({{"organization_id", m.organizationId.str()}, {"role", std::string(auth::toString(m.role))}});
  nlohmann::json j = {{"user", {{"id", p.userId.str()}, {"email", p.email}, {"display_name", p.displayName}, {"is_platform_admin", p.platformAdmin}}},
                      {"memberships", std::move(memberships)}};
  if (withCsrf) j["csrf_token"] = p.csrfToken;
  return j;
}

struct Handler {
  CarbonApp& app;
  ApiDeps& d;

  RequestInfo info(const crow::request& req) const {
    return {app.get_context<ApiMiddleware>(req).requestId, req.remote_ip_address, sanitizeHeaderText(req.get_header_value("User-Agent"))};
  }

  Principal authenticate(const crow::request& req, bool enforceCsrf = true) const {
    const auto token = cookieValue(req.get_header_value("Cookie"), sessionCookieName(d.config));
    if (!token) throw unauthorized();
    auto principal = d.sessions.authenticate(*token);
    if (!principal) throw unauthorized("Session expired or invalid");
    if (enforceCsrf && isUnsafe(req.method) && !constantTimeEquals(req.get_header_value("X-CSRF-Token"), principal->csrfToken))
      throw ApiError(403, "csrf_failed", "Missing or invalid CSRF token");
    return std::move(*principal);
  }

  void setCookie(crow::response& res, const std::string& value, long long maxAge) const {
    std::string c = sessionCookieName(d.config) + "=" + value + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=" + std::to_string(maxAge);
    if (d.config.isProduction()) c += "; Secure";
    res.add_header("Set-Cookie", c);
  }

  template <class F>
  crow::response run(const crow::request& req, F&& f) const {
    try {
      return f(info(req));
    } catch (const ApiError& e) {
      return errorResponse(e);
    } catch (const db::PoolTimeout&) {
      return errorResponse(serviceUnavailable("Database is busy; try again"));
    } catch (const pqxx::broken_connection&) {
      d.logger.error("database connection lost", {{"request_id", info(req).requestId}});
      return errorResponse(serviceUnavailable("Database unavailable"));
    } catch (const std::exception& e) {
      d.logger.error("unhandled exception in handler", {{"request_id", info(req).requestId}, {"what", e.what()}});
    } catch (...) {
      d.logger.error("unknown exception in handler", {{"request_id", info(req).requestId}});
    }
    return errorResponse(internalError());
  }
};
}  // namespace

std::string sessionCookieName(const Config& config) { return config.isProduction() ? "__Host-carbon_session" : "carbon_session"; }

void registerApiRoutes(CarbonApp& app, ApiDeps& d) {
  Handler h{app, d};

  // ================= auth =================
  CROW_ROUTE(app, "/api/v1/auth/register").methods(crow::HTTPMethod::Post)([&d, h](const crow::request& req) {
    return h.run(req, [&](const RequestInfo& ri) {
      rateLimit(d.registerLimiter, req);
      const auto body = parseJsonObject(req.body, req.get_header_value("Content-Type"));
      BodyReader r(body);
      auth::RegisterInput in;
      in.email = r.requireString("email", 3, 254);
      in.password = r.requireSecret("password", 1, 1024);  // exact policy is checked by the service
      in.displayName = r.requireString("display_name", 1, 80);
      in.organizationName = r.requireString("organization_name", 2, 120);
      in.sector = r.requireString("sector", 2, 60);
      r.finish();
      const auto result = d.auth.registerCompany(in, ri);
      crow::response res = jsonResponse(201, sessionJson(result.principal, true));
      h.setCookie(res, result.session.token, result.session.ttl.count());
      return res;
    });
  });

  CROW_ROUTE(app, "/api/v1/auth/login").methods(crow::HTTPMethod::Post)([&d, h](const crow::request& req) {
    return h.run(req, [&](const RequestInfo& ri) {
      rateLimit(d.loginLimiter, req);
      const auto body = parseJsonObject(req.body, req.get_header_value("Content-Type"));
      BodyReader r(body);
      const std::string email = r.requireString("email", 3, 254);
      const std::string password = r.requireSecret("password", 1, 1024);
      r.finish();
      const auto result = d.auth.login(email, password, ri);
      crow::response res = jsonResponse(200, sessionJson(result.principal, true));
      h.setCookie(res, result.session.token, result.session.ttl.count());
      return res;
    });
  });

  CROW_ROUTE(app, "/api/v1/auth/logout").methods(crow::HTTPMethod::Post)([&d, h](const crow::request& req) {
    return h.run(req, [&](const RequestInfo& ri) {
      const Principal who = h.authenticate(req);
      d.auth.logout(who, ri);
      crow::response res = noContent();
      h.setCookie(res, "", 0);
      return res;
    });
  });

  CROW_ROUTE(app, "/api/v1/auth/me").methods(crow::HTTPMethod::Get)([&d, h](const crow::request& req) {
    return h.run(req, [&](const RequestInfo&) { return jsonResponse(200, sessionJson(h.authenticate(req, false), true)); });
  });

  CROW_ROUTE(app, "/api/v1/auth/password-reset/request").methods(crow::HTTPMethod::Post)([&d, h](const crow::request& req) {
    return h.run(req, [&](const RequestInfo& ri) {
      rateLimit(d.resetLimiter, req);
      const auto body = parseJsonObject(req.body, req.get_header_value("Content-Type"));
      BodyReader r(body);
      const std::string email = r.requireString("email", 3, 254);
      r.finish();
      d.auth.requestPasswordReset(email, ri);
      return jsonResponse(202, {{"status", "accepted"}, {"message", "If an account exists for that address, a reset message has been issued."}});
    });
  });

  CROW_ROUTE(app, "/api/v1/auth/password-reset/confirm").methods(crow::HTTPMethod::Post)([&d, h](const crow::request& req) {
    return h.run(req, [&](const RequestInfo& ri) {
      rateLimit(d.resetLimiter, req);
      const auto body = parseJsonObject(req.body, req.get_header_value("Content-Type"));
      BodyReader r(body);
      const std::string token = r.requireSecret("token", 1, 256);
      const std::string password = r.requireSecret("new_password", 1, 1024);
      r.finish();
      d.auth.confirmPasswordReset(token, password, ri);
      return noContent();
    });
  });

  // ================= organisations =================
  CROW_ROUTE(app, "/api/v1/organizations").methods(crow::HTTPMethod::Get, crow::HTTPMethod::Post)([&d, h](const crow::request& req) {
    return h.run(req, [&](const RequestInfo& ri) {
      const Principal who = h.authenticate(req);
      if (req.method == crow::HTTPMethod::Get) return jsonResponse(200, d.orgs.list(who, parsePage(req, {"name", "sector", "created_at"}, "name")));
      auth::requirePlatformAdmin(who);
      const auto body = parseJsonObject(req.body, req.get_header_value("Content-Type"));
      BodyReader r(body);
      domain::NewOrganization n;
      n.name = r.requireString("name", 2, 120);
      n.sector = r.requireString("sector", 2, 60);
      n.legalName = r.optionalString("legal_name", 1, 200);
      n.state = r.optionalString("state", 1, 80);
      n.gstin = r.optionalString("gstin", 15, 15);
      if (n.gstin && !std::regex_match(*n.gstin, std::regex("^[0-9A-Z]{15}$"))) r.fail("gstin", "must be 15 upper-case letters/digits");
      r.finish();
      return jsonResponse(201, d.orgs.create(who, n, ri));
    });
  });

  CROW_ROUTE(app, "/api/v1/organizations/<string>").methods(crow::HTTPMethod::Get, crow::HTTPMethod::Patch)([&d, h](const crow::request& req, const std::string& orgText) {
    return h.run(req, [&](const RequestInfo& ri) {
      const Principal who = h.authenticate(req);
      const Uuid org = parseId(orgText);
      if (req.method == crow::HTTPMethod::Get) {
        auth::requireOrgPermission(who, org, Permission::OrgRead);
        return jsonResponse(200, d.orgs.get(org));
      }
      auth::requireOrgPermission(who, org, Permission::OrgUpdate);
      const auto body = parseJsonObject(req.body, req.get_header_value("Content-Type"));
      BodyReader r(body);
      domain::OrganizationPatch p;
      p.name = r.optionalString("name", 2, 120);
      p.legalName = r.optionalString("legal_name", 1, 200);
      p.sector = r.optionalString("sector", 2, 60);
      p.state = r.optionalString("state", 1, 80);
      p.gstin = r.optionalString("gstin", 15, 15);
      if (p.gstin && !std::regex_match(*p.gstin, std::regex("^[0-9A-Z]{15}$"))) r.fail("gstin", "must be 15 upper-case letters/digits");
      if (!p.name && !p.legalName && !p.sector && !p.state && !p.gstin) r.fail("body", "at least one field must be provided");
      r.finish();
      return jsonResponse(200, d.orgs.update(who, org, p, ri));
    });
  });

  // ================= memberships =================
  CROW_ROUTE(app, "/api/v1/organizations/<string>/members").methods(crow::HTTPMethod::Get, crow::HTTPMethod::Post)([&d, h](const crow::request& req, const std::string& orgText) {
    return h.run(req, [&](const RequestInfo& ri) {
      const Principal who = h.authenticate(req);
      const Uuid org = parseId(orgText);
      if (req.method == crow::HTTPMethod::Get) {
        auth::requireOrgPermission(who, org, Permission::MembersRead);
        return jsonResponse(200, d.orgs.listMembers(org, parsePage(req, {"email", "role", "created_at"}, "email")));
      }
      auth::requireOrgPermission(who, org, Permission::MembersManage);
      const auto body = parseJsonObject(req.body, req.get_header_value("Content-Type"));
      BodyReader r(body);
      const std::string email = r.requireString("email", 3, 254);
      const std::string roleText = r.requireString("role", 1, 40);
      const auto role = auth::parseRole(roleText);
      if (!role && !roleText.empty()) r.fail("role", "must be one of company_admin, company_operator, consultant, reviewer");
      r.finish();
      return jsonResponse(201, d.orgs.addMember(who, org, email, *role, ri));
    });
  });

  CROW_ROUTE(app, "/api/v1/organizations/<string>/members/<string>").methods(crow::HTTPMethod::Patch, crow::HTTPMethod::Delete)(
      [&d, h](const crow::request& req, const std::string& orgText, const std::string& memberText) {
        return h.run(req, [&](const RequestInfo& ri) {
          const Principal who = h.authenticate(req);
          const Uuid org = parseId(orgText);
          auth::requireOrgPermission(who, org, Permission::MembersManage);
          const Uuid membership = parseId(memberText);
          if (req.method == crow::HTTPMethod::Delete) {
            d.orgs.removeMember(who, org, membership, ri);
            return noContent();
          }
          const auto body = parseJsonObject(req.body, req.get_header_value("Content-Type"));
          BodyReader r(body);
          const std::string roleText = r.requireString("role", 1, 40);
          const auto role = auth::parseRole(roleText);
          if (!role && !roleText.empty()) r.fail("role", "must be one of company_admin, company_operator, consultant, reviewer");
          r.finish();
          return jsonResponse(200, d.orgs.changeRole(who, org, membership, *role, ri));
        });
      });

  // ================= facilities =================
  CROW_ROUTE(app, "/api/v1/organizations/<string>/facilities").methods(crow::HTTPMethod::Get, crow::HTTPMethod::Post)([&d, h](const crow::request& req, const std::string& orgText) {
    return h.run(req, [&](const RequestInfo& ri) {
      const Principal who = h.authenticate(req);
      const Uuid org = parseId(orgText);
      if (req.method == crow::HTTPMethod::Get) {
        auth::requireOrgPermission(who, org, Permission::FacilityRead);
        return jsonResponse(200, d.facilities.list(org, parsePage(req, {"name", "sector", "created_at"}, "name", true)));
      }
      auth::requireOrgPermission(who, org, Permission::FacilityWrite);
      const auto body = parseJsonObject(req.body, req.get_header_value("Content-Type"));
      BodyReader r(body);
      domain::NewFacility f;
      f.name = r.requireString("name", 1, 120);
      f.sector = r.requireString("sector", 2, 60);
      f.productionUnit = r.requireString("production_unit", 1, 30);
      f.state = r.optionalString("state", 1, 80);
      f.address = r.optionalString("address", 1, 300);
      f.boundaryNotes = r.optionalString("boundary_notes", 1, 2000);
      r.finish();
      crow::response res = jsonResponse(201, d.facilities.create(who, org, f, ri));
      res.set_header("Location", "/api/v1/organizations/" + org.str() + "/facilities/" + nlohmann::json::parse(res.body)["id"].get<std::string>());
      return res;
    });
  });

  CROW_ROUTE(app, "/api/v1/organizations/<string>/facilities/<string>").methods(crow::HTTPMethod::Get, crow::HTTPMethod::Patch)(
      [&d, h](const crow::request& req, const std::string& orgText, const std::string& facText) {
        return h.run(req, [&](const RequestInfo& ri) {
          const Principal who = h.authenticate(req);
          const Uuid org = parseId(orgText);
          if (req.method == crow::HTTPMethod::Get) {
            auth::requireOrgPermission(who, org, Permission::FacilityRead);
            return jsonResponse(200, d.facilities.get(org, parseId(facText)));
          }
          auth::requireOrgPermission(who, org, Permission::FacilityWrite);
          const Uuid facility = parseId(facText);
          const auto body = parseJsonObject(req.body, req.get_header_value("Content-Type"));
          BodyReader r(body);
          domain::FacilityPatch p;
          p.name = r.optionalString("name", 1, 120);
          p.sector = r.optionalString("sector", 2, 60);
          p.productionUnit = r.optionalString("production_unit", 1, 30);
          p.state = r.optionalString("state", 1, 80);
          p.address = r.optionalString("address", 1, 300);
          p.boundaryNotes = r.optionalString("boundary_notes", 1, 2000);
          p.isActive = r.optionalBool("is_active");
          if (!p.name && !p.sector && !p.productionUnit && !p.state && !p.address && !p.boundaryNotes && !p.isActive) r.fail("body", "at least one field must be provided");
          r.finish();
          return jsonResponse(200, d.facilities.update(who, org, facility, p, ri));
        });
      });

  // ================= audit history =================
  CROW_ROUTE(app, "/api/v1/organizations/<string>/audit-events").methods(crow::HTTPMethod::Get)([&d, h](const crow::request& req, const std::string& orgText) {
    return h.run(req, [&](const RequestInfo&) {
      const Principal who = h.authenticate(req);
      const Uuid org = parseId(orgText);
      auth::requireOrgPermission(who, org, Permission::AuditRead);
      PageRequest page = parsePage(req, {"occurred_at", "action"}, "occurred_at", false, true);
      if (!req.url_params.get("order")) page.descending = true;  // newest first by default
      return jsonResponse(200, d.audit.listForOrganization(org, page));
    });
  });
}

}  // namespace carbon::http
