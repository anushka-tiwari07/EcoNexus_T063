// Roles, permissions and the authenticated principal.
//
// Policy summary (also in docs/security.md):
//  * Company data (facilities, later: emissions...) is reachable ONLY through an organisation membership.
//  * A user outside an organisation gets 404 (existence is not revealed); a member lacking the
//    permission gets 403.
//  * Platform administrators manage the platform (create organisations, assign memberships, read
//    organisation metadata/audit) but have NO implicit access to company facility data.
#pragma once

#include "carbon/uuid.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace carbon::auth {

enum class Role { CompanyAdmin, CompanyOperator, Consultant, Reviewer };
enum class Permission { OrgRead, OrgUpdate, MembersRead, MembersManage, FacilityRead, FacilityWrite, AuditRead };

std::optional<Role> parseRole(std::string_view text);
std::string_view toString(Role role);
bool roleHas(Role role, Permission permission);
bool platformAdminHas(Permission permission);  // the narrow set a platform admin may do on any org

struct Membership {
  Uuid organizationId;
  Role role;
};

struct Principal {
  Uuid userId;
  std::string email;
  std::string displayName;
  bool platformAdmin = false;
  std::vector<Membership> memberships;
  Uuid sessionId;
  std::string csrfToken;

  std::optional<Role> roleIn(const Uuid& org) const;
};

// Throw ApiError: 404 if the caller has no standing in `org`, 403 if a member lacks `permission`.
void requireOrgPermission(const Principal& who, const Uuid& org, Permission permission);
void requirePlatformAdmin(const Principal& who);  // 403 otherwise

}  // namespace carbon::auth
