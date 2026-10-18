#include "carbon/auth/principal.hpp"

#include "carbon/errors.hpp"

namespace carbon::auth {

std::optional<Role> parseRole(std::string_view t) {
  if (t == "company_admin") return Role::CompanyAdmin;
  if (t == "company_operator") return Role::CompanyOperator;
  if (t == "consultant") return Role::Consultant;
  if (t == "reviewer") return Role::Reviewer;
  return std::nullopt;
}

std::string_view toString(Role r) {
  switch (r) {
    case Role::CompanyAdmin: return "company_admin";
    case Role::CompanyOperator: return "company_operator";
    case Role::Consultant: return "consultant";
    case Role::Reviewer: return "reviewer";
  }
  return "reviewer";
}

bool roleHas(Role role, Permission p) {
  switch (role) {
    case Role::CompanyAdmin: return true;
    case Role::CompanyOperator: return p == Permission::OrgRead || p == Permission::FacilityRead;
    case Role::Consultant:
      return p == Permission::OrgRead || p == Permission::FacilityRead || p == Permission::FacilityWrite || p == Permission::AuditRead;
    case Role::Reviewer: return p == Permission::OrgRead || p == Permission::FacilityRead || p == Permission::AuditRead;
  }
  return false;
}

bool platformAdminHas(Permission p) {
  return p == Permission::OrgRead || p == Permission::MembersRead || p == Permission::MembersManage || p == Permission::AuditRead;
}

std::optional<Role> Principal::roleIn(const Uuid& org) const {
  for (const auto& m : memberships)
    if (m.organizationId == org) return m.role;
  return std::nullopt;
}

void requireOrgPermission(const Principal& who, const Uuid& org, Permission permission) {
  if (const auto role = who.roleIn(org)) {
    if (roleHas(*role, permission)) return;
    throw forbidden();
  }
  if (who.platformAdmin && platformAdminHas(permission)) return;
  throw notFound();  // no standing: do not reveal that the organisation exists
}

void requirePlatformAdmin(const Principal& who) {
  if (!who.platformAdmin) throw forbidden();
}

}  // namespace carbon::auth
