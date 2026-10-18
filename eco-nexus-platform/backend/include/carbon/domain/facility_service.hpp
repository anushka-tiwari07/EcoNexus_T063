// Facilities: every query is scoped by organisation id, so a facility id from another tenant is simply "not found".
#pragma once

#include "carbon/audit/audit_service.hpp"
#include "carbon/auth/principal.hpp"
#include "carbon/db/pool.hpp"
#include "carbon/paging.hpp"

#include <nlohmann/json.hpp>

#include <optional>

namespace carbon::domain {

struct NewFacility {
  std::string name, sector, productionUnit;
  std::optional<std::string> state, address, boundaryNotes;
};
struct FacilityPatch {
  std::optional<std::string> name, sector, productionUnit, state, address, boundaryNotes;
  std::optional<bool> isActive;
};

class FacilityService {
 public:
  FacilityService(db::ConnectionPool& pool, audit::AuditService& audit) : pool_(pool), audit_(audit) {}

  nlohmann::json list(const Uuid& org, const PageRequest& page) const;
  nlohmann::json get(const Uuid& org, const Uuid& facility) const;
  nlohmann::json create(const auth::Principal& who, const Uuid& org, const NewFacility& f, const RequestInfo& req);
  nlohmann::json update(const auth::Principal& who, const Uuid& org, const Uuid& facility, const FacilityPatch& p, const RequestInfo& req);

 private:
  db::ConnectionPool& pool_;
  audit::AuditService& audit_;
};

}  // namespace carbon::domain
