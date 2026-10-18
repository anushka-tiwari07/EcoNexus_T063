// Organisations and memberships. Authorisation is decided by the HTTP layer via requireOrgPermission();
// these services additionally scope every query by organisation id, so a mismatched id can never leak rows.
#pragma once

#include "carbon/audit/audit_service.hpp"
#include "carbon/auth/principal.hpp"
#include "carbon/db/pool.hpp"
#include "carbon/paging.hpp"

#include <nlohmann/json.hpp>

#include <optional>

namespace carbon::domain {

struct NewOrganization {
  std::string name, sector;
  std::optional<std::string> legalName, state, gstin;
};
struct OrganizationPatch {
  std::optional<std::string> name, legalName, sector, state, gstin;
};

class OrganizationService {
 public:
  OrganizationService(db::ConnectionPool& pool, audit::AuditService& audit) : pool_(pool), audit_(audit) {}

  // Members see their organisations; platform admins see all (with role = null).
  nlohmann::json list(const auth::Principal& who, const PageRequest& page) const;
  nlohmann::json create(const auth::Principal& who, const NewOrganization& org, const RequestInfo& req);  // platform admin only (checked by caller)
  nlohmann::json get(const Uuid& org) const;
  nlohmann::json update(const auth::Principal& who, const Uuid& org, const OrganizationPatch& patch, const RequestInfo& req);

  nlohmann::json listMembers(const Uuid& org, const PageRequest& page) const;
  nlohmann::json addMember(const auth::Principal& who, const Uuid& org, const std::string& email, auth::Role role, const RequestInfo& req);
  nlohmann::json changeRole(const auth::Principal& who, const Uuid& org, const Uuid& membership, auth::Role role, const RequestInfo& req);
  void removeMember(const auth::Principal& who, const Uuid& org, const Uuid& membership, const RequestInfo& req);

 private:
  db::ConnectionPool& pool_;
  audit::AuditService& audit_;
};

}  // namespace carbon::domain
