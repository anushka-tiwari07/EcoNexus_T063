// Registration, login (with lockout), logout and password reset.
//
// Security properties implemented here:
//  * Argon2id hashing; passwords never logged or stored in plaintext
//  * identical response + a dummy hash verification for unknown users (no login oracle / timing gap)
//  * per-account lockout after repeated failures (atomic SQL counter, safe under concurrency)
//  * reset tokens: 256-bit random, only the SHA-256 stored, single use, 1 hour, revoke all sessions
//  * every sensitive action writes an audit event in the same transaction
#pragma once

#include "carbon/audit/audit_service.hpp"
#include "carbon/auth/password.hpp"
#include "carbon/auth/session_service.hpp"
#include "carbon/db/pool.hpp"

#include <chrono>
#include <string>

namespace carbon::auth {

// How a reset token reaches the user. Email delivery is NOT implemented in this project; the shipped
// notifier only logs (development) so the flow can be exercised. Abstract so real transport can be added.
class ResetNotifier {
 public:
  virtual ~ResetNotifier() = default;
  virtual void send(const std::string& email, const std::string& token, std::chrono::seconds validFor) = 0;
};

struct AuthSettings {
  bool allowSelfRegistration = true;
  int maxFailedLogins = 5;
  std::chrono::seconds lockout = std::chrono::minutes(15);
  std::chrono::seconds resetTtl = std::chrono::hours(1);
};

struct RegisterInput {
  std::string email, password, displayName, organizationName, sector;
};

struct LoginResult {
  IssuedSession session;
  Principal principal;
};

class AuthService {
 public:
  AuthService(db::ConnectionPool& pool, const PasswordHasher& hasher, SessionService& sessions, audit::AuditService& audit,
              ResetNotifier& notifier, AuthSettings settings);

  LoginResult registerCompany(const RegisterInput& input, const RequestInfo& request);
  LoginResult login(const std::string& email, const std::string& password, const RequestInfo& request);
  void logout(const Principal& who, const RequestInfo& request);
  void requestPasswordReset(const std::string& email, const RequestInfo& request);  // silent whether or not the user exists
  void confirmPasswordReset(const std::string& token, const std::string& newPassword, const RequestInfo& request);

  const AuthSettings& settings() const noexcept { return settings_; }

 private:
  Principal principalFor(const IssuedSession& s) const;

  db::ConnectionPool& pool_;
  const PasswordHasher& hasher_;
  SessionService& sessions_;
  audit::AuditService& audit_;
  ResetNotifier& notifier_;
  AuthSettings settings_;
  std::string dummyHash_;  // verified against when the account is unknown/locked, to equalise timing
};

}  // namespace carbon::auth
