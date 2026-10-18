#include "carbon/auth/auth_service.hpp"

#include "carbon/crypto.hpp"
#include "carbon/errors.hpp"
#include "carbon/validation.hpp"

#include <algorithm>
#include <cctype>

namespace carbon::auth {
namespace {
std::string lowerTrim(std::string s) {
  auto sp = [](unsigned char c) { return std::isspace(c); };
  s.erase(s.begin(), std::find_if_not(s.begin(), s.end(), sp));
  s.erase(std::find_if_not(s.rbegin(), s.rend(), sp).base(), s.end());
  return s;
}

ApiError invalidCredentials() { return unauthorized("Invalid email or password"); }

bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }
}  // namespace

AuthService::AuthService(db::ConnectionPool& pool, const PasswordHasher& hasher, SessionService& sessions, audit::AuditService& audit,
                         ResetNotifier& notifier, AuthSettings settings)
    : pool_(pool), hasher_(hasher), sessions_(sessions), audit_(audit), notifier_(notifier), settings_(settings),
      dummyHash_(hasher.hash("dummy-password-used-only-to-equalise-timing")) {}

Principal AuthService::principalFor(const IssuedSession& s) const {
  auto p = sessions_.authenticate(s.token);
  if (!p) throw internalError();  // just-issued session must be valid
  return std::move(*p);
}

LoginResult AuthService::registerCompany(const RegisterInput& in, const RequestInfo& req) {
  if (!settings_.allowSelfRegistration) throw forbidden("Self-registration is disabled; ask a platform administrator for an account");
  const std::string email = lowerTrim(in.email);
  nlohmann::json problems = nlohmann::json::array();
  if (!looksLikeEmail(email)) problems.push_back({{"field", "email"}, {"message", "must be a valid email address"}});
  if (const auto err = validatePasswordPolicy(in.password, email)) problems.push_back({{"field", "password"}, {"message", *err}});
  if (!problems.empty()) throw unprocessable("Validation failed", problems);

  const std::string hash = hasher_.hash(in.password);  // expensive: done outside the transaction
  IssuedSession session;
  try {
    session = pool_.inTransaction([&](pqxx::work& tx) {
      const std::string userId = tx.exec_params1("INSERT INTO users (email, password_hash, display_name) VALUES ($1, $2, $3) RETURNING id",
                                                 email, hash, in.displayName)[0].as<std::string>();
      const std::string orgId = tx.exec_params1("INSERT INTO organizations (name, sector) VALUES ($1, $2) RETURNING id",
                                                in.organizationName, in.sector)[0].as<std::string>();
      tx.exec_params("INSERT INTO organization_memberships (organization_id, user_id, role) VALUES ($1::uuid, $2::uuid, 'company_admin')", orgId, userId);
      const Uuid user = *Uuid::parse(userId), org = *Uuid::parse(orgId);
      IssuedSession s = sessions_.issue(tx, user, req);
      audit_.record(tx, {user, std::nullopt, "user.registered", "user", userId, {}}, req);
      audit_.record(tx, {user, org, "organization.created", "organization", orgId, {{"via", "self_registration"}}}, req);
      return s;
    });
  } catch (const pqxx::unique_violation& e) {
    const std::string msg = e.what();
    if (contains(msg, "organizations_name_lower_key")) throw conflict("An organization with this name already exists");
    throw conflict("Registration could not be completed with these details");  // email taken (see docs/security.md)
  }
  return {session, principalFor(session)};
}

LoginResult AuthService::login(const std::string& rawEmail, const std::string& password, const RequestInfo& req) {
  const std::string email = lowerTrim(rawEmail);
  struct UserRow { std::string id, hash; bool active, locked; };
  const auto user = pool_.inTransaction([&](pqxx::work& tx) -> std::optional<UserRow> {
    const auto rows = tx.exec_params(
        "SELECT id, password_hash, is_active, (locked_until IS NOT NULL AND locked_until > now()) AS locked FROM users WHERE lower(email) = lower($1)", email);
    if (rows.empty()) return std::nullopt;
    return UserRow{rows[0]["id"].as<std::string>(), rows[0]["password_hash"].as<std::string>(), rows[0]["is_active"].as<bool>(), rows[0]["locked"].as<bool>()};
  });

  auto failureAudit = [&](const char* reason, std::optional<Uuid> actor) {
    audit_.recordStandalone({actor, std::nullopt, "auth.login.failure", "user", actor ? actor->str() : "", {{"reason", reason}, {"email_sha256", sha256Hex(email).substr(0, 16)}}}, req);
  };

  if (!user || !user->active || user->locked) {
    hasher_.verify(dummyHash_, password);  // burn the same time as a real check
    failureAudit(!user ? "unknown_user" : (!user->active ? "inactive" : "locked"), user ? Uuid::parse(user->id) : std::nullopt);
    throw invalidCredentials();
  }

  const Uuid userId = *Uuid::parse(user->id);
  if (!hasher_.verify(user->hash, password)) {
    pool_.inTransaction([&](pqxx::work& tx) {
      const auto r = tx.exec_params1(
          "UPDATE users SET failed_login_count = CASE WHEN failed_login_count + 1 >= $2 THEN 0 ELSE failed_login_count + 1 END, "
          "locked_until = CASE WHEN failed_login_count + 1 >= $2 THEN now() + make_interval(secs => $3::double precision) ELSE locked_until END "
          "WHERE id = $1::uuid RETURNING (locked_until IS NOT NULL AND locked_until > now()) AS locked",
          user->id, settings_.maxFailedLogins, static_cast<double>(settings_.lockout.count()));
      audit_.record(tx, {userId, std::nullopt, "auth.login.failure", "user", user->id, {{"reason", "bad_password"}, {"locked_now", r[0].as<bool>()}}}, req);
    });
    throw invalidCredentials();
  }

  const std::optional<std::string> newHash = hasher_.needsRehash(user->hash) ? std::optional<std::string>(hasher_.hash(password)) : std::nullopt;
  IssuedSession session = pool_.inTransaction([&](pqxx::work& tx) {
    tx.exec_params("UPDATE users SET failed_login_count = 0, locked_until = NULL, last_login_at = now() WHERE id = $1::uuid", user->id);
    if (newHash) tx.exec_params("UPDATE users SET password_hash = $2 WHERE id = $1::uuid", user->id, *newHash);  // upgrade old parameters
    IssuedSession s = sessions_.issue(tx, userId, req);
    audit_.record(tx, {userId, std::nullopt, "auth.login.success", "session", s.id, {}}, req);
    return s;
  });
  sessions_.purgeExpired();  // opportunistic housekeeping
  return {session, principalFor(session)};
}

void AuthService::logout(const Principal& who, const RequestInfo& req) {
  pool_.inTransaction([&](pqxx::work& tx) {
    tx.exec_params("UPDATE sessions SET revoked_at = now() WHERE id = $1::uuid AND revoked_at IS NULL", who.sessionId.str());
    audit_.record(tx, {who.userId, std::nullopt, "auth.logout", "session", who.sessionId.str(), {}}, req);
  });
}

void AuthService::requestPasswordReset(const std::string& rawEmail, const RequestInfo& req) {
  const std::string email = lowerTrim(rawEmail);
  std::string token;
  std::string realEmail;
  pool_.inTransaction([&](pqxx::work& tx) {
    const auto rows = tx.exec_params("SELECT id, email FROM users WHERE lower(email) = lower($1) AND is_active", email);
    if (rows.empty()) return;  // silent: the caller cannot tell whether the account exists
    const std::string userId = rows[0]["id"].as<std::string>();
    realEmail = rows[0]["email"].as<std::string>();
    token = randomHex(32);
    tx.exec_params("UPDATE password_reset_tokens SET used_at = now() WHERE user_id = $1::uuid AND used_at IS NULL", userId);  // one live token
    tx.exec_params("INSERT INTO password_reset_tokens (user_id, token_hash, expires_at, requested_ip) "
                   "VALUES ($1::uuid, $2, now() + make_interval(secs => $3::double precision), $4::inet)",
                   userId, sha256Hex(token), static_cast<double>(settings_.resetTtl.count()), inetOrNull(req.ip));
    audit_.record(tx, {*Uuid::parse(userId), std::nullopt, "auth.password_reset.requested", "user", userId, {}}, req);
  });
  if (!token.empty()) notifier_.send(realEmail, token, settings_.resetTtl);  // after commit
}

void AuthService::confirmPasswordReset(const std::string& token, const std::string& newPassword, const RequestInfo& req) {
  if (token.size() != 64) throw badRequest("Invalid or expired reset token");
  std::string email;
  const std::string tokenHash = sha256Hex(token);
  pool_.inTransaction([&](pqxx::work& tx) {  // peek at the account email so the policy can compare against it
    const auto rows = tx.exec_params("SELECT u.email FROM password_reset_tokens t JOIN users u ON u.id = t.user_id WHERE t.token_hash = $1", tokenHash);
    if (!rows.empty()) email = rows[0][0].as<std::string>();
  });
  if (const auto err = validatePasswordPolicy(newPassword, email)) throw unprocessable("Validation failed", {{{"field", "new_password"}, {"message", *err}}});

  const std::string hash = hasher_.hash(newPassword);
  pool_.inTransaction([&](pqxx::work& tx) {
    const auto rows = tx.exec_params(
        "SELECT t.id, t.user_id FROM password_reset_tokens t JOIN users u ON u.id = t.user_id "
        "WHERE t.token_hash = $1 AND t.used_at IS NULL AND t.expires_at > now() AND u.is_active FOR UPDATE OF t",
        tokenHash);
    if (rows.empty()) throw badRequest("Invalid or expired reset token");
    const std::string userId = rows[0]["user_id"].as<std::string>();
    tx.exec_params("UPDATE password_reset_tokens SET used_at = now() WHERE id = $1::uuid", rows[0]["id"].as<std::string>());
    tx.exec_params("UPDATE users SET password_hash = $2, failed_login_count = 0, locked_until = NULL WHERE id = $1::uuid", userId, hash);
    sessions_.revokeAllForUser(tx, *Uuid::parse(userId));  // a reset logs out every device
    audit_.record(tx, {*Uuid::parse(userId), std::nullopt, "auth.password_reset.completed", "user", userId, {}}, req);
  });
}

}  // namespace carbon::auth
