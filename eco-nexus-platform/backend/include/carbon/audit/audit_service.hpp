// Audit trail writer/reader. record() runs INSIDE the caller's transaction, so a business change and
// its audit event commit or roll back together. audit_events is append-only at the database level.
#pragma once

#include "carbon/db/pool.hpp"
#include "carbon/paging.hpp"
#include "carbon/request_info.hpp"
#include "carbon/uuid.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace carbon::audit {

struct AuditEntry {
  std::optional<Uuid> actor;
  std::optional<Uuid> organization;
  std::string action;      // e.g. "auth.login.success", "facility.created"
  std::string entityType;  // e.g. "user", "facility"
  std::string entityId;
  nlohmann::json details = nlohmann::json::object();  // never put secrets here
};

class AuditService {
 public:
  explicit AuditService(db::ConnectionPool& pool) : pool_(pool) {}

  void record(pqxx::work& tx, const AuditEntry& entry, const RequestInfo& request) const;
  void recordStandalone(const AuditEntry& entry, const RequestInfo& request) const;  // own transaction

  nlohmann::json listForOrganization(const Uuid& org, const PageRequest& page) const;

 private:
  db::ConnectionPool& pool_;
};

}  // namespace carbon::audit
