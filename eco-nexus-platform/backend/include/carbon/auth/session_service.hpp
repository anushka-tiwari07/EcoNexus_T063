// Server-side sessions. The browser holds an opaque random token (HttpOnly cookie); the database holds
// only its SHA-256. Sessions expire absolutely (ttl) and after inactivity (idle), and can be revoked.
#pragma once

#include "carbon/auth/principal.hpp"
#include "carbon/db/pool.hpp"
#include "carbon/request_info.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace carbon::auth {

struct SessionSettings {
  std::chrono::seconds ttl = std::chrono::hours(12);
  std::chrono::seconds idle = std::chrono::hours(2);
};

struct IssuedSession {
  std::string id;
  std::string token;      // raw secret: shown to the client once, never stored or logged
  std::string csrfToken;
  std::chrono::seconds ttl;
};

class SessionService {
 public:
  SessionService(db::ConnectionPool& pool, SessionSettings settings) : pool_(pool), settings_(settings) {}

  IssuedSession issue(pqxx::work& tx, const Uuid& userId, const RequestInfo& request) const;
  std::optional<Principal> authenticate(std::string_view rawToken) const;  // nullopt = no valid session
  void revokeAllForUser(pqxx::work& tx, const Uuid& userId) const;
  std::size_t purgeExpired() const;  // deletes sessions that ended more than a day ago

  const SessionSettings& settings() const noexcept { return settings_; }
  static std::string hashToken(std::string_view raw);

 private:
  db::ConnectionPool& pool_;
  SessionSettings settings_;
};

}  // namespace carbon::auth
