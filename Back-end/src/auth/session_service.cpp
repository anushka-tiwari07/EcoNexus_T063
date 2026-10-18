#include "carbon/auth/session_service.hpp"

#include "carbon/crypto.hpp"

#include <algorithm>
#include <cctype>

namespace carbon::auth {

std::string SessionService::hashToken(std::string_view raw) { return sha256Hex(raw); }

IssuedSession SessionService::issue(pqxx::work& tx, const Uuid& userId, const RequestInfo& req) const {
  IssuedSession s;
  s.token = randomHex(32);
  s.csrfToken = randomHex(32);
  s.ttl = settings_.ttl;
  const auto row = tx.exec_params1(
      "INSERT INTO sessions (user_id, token_hash, csrf_token, expires_at, ip_address, user_agent) "
      "VALUES ($1::uuid, $2, $3, now() + make_interval(secs => $4::double precision), $5::inet, $6) RETURNING id",
      userId.str(), hashToken(s.token), s.csrfToken, static_cast<double>(settings_.ttl.count()), inetOrNull(req.ip),
      req.userAgent.empty() ? std::optional<std::string>() : std::optional<std::string>(req.userAgent));
  s.id = row[0].as<std::string>();
  return s;
}

std::optional<Principal> SessionService::authenticate(std::string_view raw) const {
  if (raw.size() != 64 || !std::all_of(raw.begin(), raw.end(), [](unsigned char c) { return std::isxdigit(c); })) return std::nullopt;
  const std::string hash = hashToken(raw);
  return pool_.inTransaction([&](pqxx::work& tx) -> std::optional<Principal> {
    const auto rows = tx.exec_params(
        "SELECT s.id, s.user_id, s.csrf_token, u.email, u.display_name, u.is_platform_admin, "
        "       (s.last_seen_at < now() - interval '60 seconds') AS stale "
        "FROM sessions s JOIN users u ON u.id = s.user_id "
        "WHERE s.token_hash = $1 AND s.revoked_at IS NULL AND s.expires_at > now() "
        "  AND s.last_seen_at > now() - make_interval(secs => $2::double precision) AND u.is_active",
        hash, static_cast<double>(settings_.idle.count()));
    if (rows.empty()) return std::nullopt;
    const auto& r = rows[0];
    Principal p{*Uuid::parse(r["user_id"].as<std::string>()), r["email"].as<std::string>(), r["display_name"].as<std::string>(),
                r["is_platform_admin"].as<bool>(), {}, *Uuid::parse(r["id"].as<std::string>()), r["csrf_token"].as<std::string>()};
    const auto memberships = tx.exec_params(
        "SELECT m.organization_id, m.role FROM organization_memberships m JOIN organizations o ON o.id = m.organization_id "
        "WHERE m.user_id = $1::uuid AND o.is_active ORDER BY o.name",
        p.userId.str());
    for (const auto& m : memberships) {
      if (const auto role = parseRole(m["role"].as<std::string>()))
        p.memberships.push_back({*Uuid::parse(m["organization_id"].as<std::string>()), *role});
    }
    if (r["stale"].as<bool>()) tx.exec_params("UPDATE sessions SET last_seen_at = now() WHERE id = $1::uuid", p.sessionId.str());
    return p;
  });
}

void SessionService::revokeAllForUser(pqxx::work& tx, const Uuid& userId) const {
  tx.exec_params("UPDATE sessions SET revoked_at = now() WHERE user_id = $1::uuid AND revoked_at IS NULL", userId.str());
}

std::size_t SessionService::purgeExpired() const {
  return pool_.inTransaction([](pqxx::work& tx) {
    const auto r = tx.exec("DELETE FROM sessions WHERE expires_at < now() - interval '1 day' OR revoked_at < now() - interval '1 day'");
    return static_cast<std::size_t>(r.affected_rows());
  });
}

}  // namespace carbon::auth
