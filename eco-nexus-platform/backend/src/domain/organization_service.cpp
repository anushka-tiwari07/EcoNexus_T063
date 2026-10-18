#include "carbon/domain/organization_service.hpp"

#include "carbon/errors.hpp"

namespace carbon::domain {
namespace {
nlohmann::json str(const pqxx::field& f) { return f.is_null() ? nlohmann::json(nullptr) : nlohmann::json(f.as<std::string>()); }

const std::string kOrgCols = "o.id, o.name, o.legal_name, o.sector, o.state, o.gstin, o.is_active, ";

nlohmann::json orgJson(const pqxx::row& r, bool withRole) {
  nlohmann::json j = {{"id", r["id"].as<std::string>()}, {"name", r["name"].as<std::string>()}, {"legal_name", str(r["legal_name"])},
                      {"sector", r["sector"].as<std::string>()}, {"state", str(r["state"])}, {"gstin", str(r["gstin"])},
                      {"is_active", r["is_active"].as<bool>()}, {"created_at", r["created_at"].as<std::string>()}};
  if (withRole) j["role"] = str(r["role"]);
  return j;
}

nlohmann::json memberJson(const pqxx::row& r) {
  return {{"id", r["id"].as<std::string>()}, {"user_id", r["user_id"].as<std::string>()}, {"email", r["email"].as<std::string>()},
          {"display_name", r["display_name"].as<std::string>()}, {"role", r["role"].as<std::string>()}, {"created_at", r["created_at"].as<std::string>()}};
}

void requireOrgExists(pqxx::work& tx, const Uuid& org) {
  if (tx.exec_params("SELECT 1 FROM organizations WHERE id = $1::uuid", org.str()).empty()) throw notFound();
}
}  // namespace

nlohmann::json OrganizationService::list(const auth::Principal& who, const PageRequest& page) const {
  return pool_.inTransaction([&](pqxx::work& tx) {
    const std::string like = "%" + escapeLike(page.q) + "%";
    const std::string filter = "($2 = '' OR o.name ILIKE $3 ESCAPE '\\')";
    const std::string sortCol = page.sort == "sector" ? "o.sector" : (page.sort == "created_at" ? "o.created_at" : "o.name");
    const std::string order = " ORDER BY " + sortCol + (page.descending ? " DESC" : " ASC") + ", o.id";
    pqxx::result rows;
    std::size_t total = 0;
    if (who.platformAdmin) {
      const std::string adminFilter = "($1 = '' OR o.name ILIKE $2 ESCAPE '\\')";
      total = tx.exec_params1("SELECT count(*) FROM organizations o WHERE " + adminFilter, page.q, like)[0].as<std::size_t>();
      rows = tx.exec_params("SELECT " + kOrgCols + isoTs("o.created_at") + " AS created_at, NULL::text AS role FROM organizations o WHERE " + adminFilter +
                            order + " LIMIT $3 OFFSET $4", page.q, like, page.limit, page.offset);
    } else {
      total = tx.exec_params1("SELECT count(*) FROM organizations o JOIN organization_memberships m ON m.organization_id = o.id AND m.user_id = $1::uuid WHERE " + filter,
                              who.userId.str(), page.q, like)[0].as<std::size_t>();
      rows = tx.exec_params("SELECT " + kOrgCols + isoTs("o.created_at") + " AS created_at, m.role FROM organizations o "
                            "JOIN organization_memberships m ON m.organization_id = o.id AND m.user_id = $1::uuid WHERE " + filter + order + " LIMIT $4 OFFSET $5",
                            who.userId.str(), page.q, like, page.limit, page.offset);
    }
    nlohmann::json data = nlohmann::json::array();
    for (const auto& r : rows) data.push_back(orgJson(r, true));
    return pageJson(std::move(data), page, total);
  });
}

nlohmann::json OrganizationService::get(const Uuid& org) const {
  return pool_.inTransaction([&](pqxx::work& tx) {
    const auto rows = tx.exec_params("SELECT " + kOrgCols + isoTs("o.created_at") + " AS created_at FROM organizations o WHERE o.id = $1::uuid", org.str());
    if (rows.empty()) throw notFound();
    return orgJson(rows[0], false);
  });
}

nlohmann::json OrganizationService::create(const auth::Principal& who, const NewOrganization& n, const RequestInfo& req) {
  try {
    return pool_.inTransaction([&](pqxx::work& tx) {
      const auto row = tx.exec_params1(
          "INSERT INTO organizations (name, legal_name, sector, state, gstin) VALUES ($1, $2, $3, $4, $5) RETURNING id", n.name, n.legalName, n.sector, n.state, n.gstin);
      const std::string id = row[0].as<std::string>();
      audit_.record(tx, {who.userId, *Uuid::parse(id), "organization.created", "organization", id, {{"via", "platform_admin"}}}, req);
      const auto rows = tx.exec_params("SELECT " + kOrgCols + isoTs("o.created_at") + " AS created_at FROM organizations o WHERE o.id = $1::uuid", id);
      return orgJson(rows[0], false);
    });
  } catch (const pqxx::unique_violation&) {
    throw conflict("An organization with this name already exists");
  }
}

nlohmann::json OrganizationService::update(const auth::Principal& who, const Uuid& org, const OrganizationPatch& p, const RequestInfo& req) {
  try {
    return pool_.inTransaction([&](pqxx::work& tx) {
      requireOrgExists(tx, org);
      nlohmann::json changed = nlohmann::json::array();
      auto set = [&](const char* col, const std::optional<std::string>& v) {
        if (!v) return;
        tx.exec_params(std::string("UPDATE organizations SET ") + col + " = $2 WHERE id = $1::uuid", org.str(), *v);
        changed.push_back(col);
      };
      set("name", p.name); set("legal_name", p.legalName); set("sector", p.sector); set("state", p.state); set("gstin", p.gstin);
      if (!changed.empty()) audit_.record(tx, {who.userId, org, "organization.updated", "organization", org.str(), {{"fields", changed}}}, req);
      const auto rows = tx.exec_params("SELECT " + kOrgCols + isoTs("o.created_at") + " AS created_at FROM organizations o WHERE o.id = $1::uuid", org.str());
      return orgJson(rows[0], false);
    });
  } catch (const pqxx::unique_violation&) {
    throw conflict("An organization with this name already exists");
  }
}

nlohmann::json OrganizationService::listMembers(const Uuid& org, const PageRequest& page) const {
  return pool_.inTransaction([&](pqxx::work& tx) {
    requireOrgExists(tx, org);
    const std::string like = "%" + escapeLike(page.q) + "%";
    const std::string filter = "m.organization_id = $1::uuid AND ($2 = '' OR u.email ILIKE $3 ESCAPE '\\' OR u.display_name ILIKE $3 ESCAPE '\\')";
    const std::string sortCol = page.sort == "role" ? "m.role" : (page.sort == "created_at" ? "m.created_at" : "u.email");
    const std::size_t total = tx.exec_params1("SELECT count(*) FROM organization_memberships m JOIN users u ON u.id = m.user_id WHERE " + filter, org.str(), page.q, like)[0].as<std::size_t>();
    const auto rows = tx.exec_params("SELECT m.id, m.user_id, u.email, u.display_name, m.role, " + isoTs("m.created_at") + " AS created_at FROM organization_memberships m "
                                     "JOIN users u ON u.id = m.user_id WHERE " + filter + " ORDER BY " + sortCol + (page.descending ? " DESC" : " ASC") + ", m.id LIMIT $4 OFFSET $5",
                                     org.str(), page.q, like, page.limit, page.offset);
    nlohmann::json data = nlohmann::json::array();
    for (const auto& r : rows) data.push_back(memberJson(r));
    return pageJson(std::move(data), page, total);
  });
}

nlohmann::json OrganizationService::addMember(const auth::Principal& who, const Uuid& org, const std::string& email, auth::Role role, const RequestInfo& req) {
  try {
    return pool_.inTransaction([&](pqxx::work& tx) {
      requireOrgExists(tx, org);
      const auto user = tx.exec_params("SELECT id FROM users WHERE lower(email) = lower($1) AND is_active", email);
      if (user.empty()) throw notFound("No active user with that email address");
      const std::string userId = user[0][0].as<std::string>();
      const auto row = tx.exec_params1("INSERT INTO organization_memberships (organization_id, user_id, role) VALUES ($1::uuid, $2::uuid, $3) RETURNING id",
                                       org.str(), userId, std::string(auth::toString(role)));
      const std::string id = row[0].as<std::string>();
      audit_.record(tx, {who.userId, org, "membership.added", "membership", id, {{"user_id", userId}, {"role", std::string(auth::toString(role))}}}, req);
      const auto rows = tx.exec_params("SELECT m.id, m.user_id, u.email, u.display_name, m.role, " + isoTs("m.created_at") + " AS created_at FROM organization_memberships m "
                                       "JOIN users u ON u.id = m.user_id WHERE m.id = $1::uuid", id);
      return memberJson(rows[0]);
    });
  } catch (const pqxx::unique_violation&) {
    throw conflict("This user is already a member of the organization");
  }
}

namespace {
// Locks the organisation's membership rows so concurrent demotions/removals serialise, then enforces
// the invariant "an organisation always keeps at least one company_admin".
struct LockedMembership { std::string userId, role; };
LockedMembership lockAndFind(pqxx::work& tx, const Uuid& org, const Uuid& membership, std::size_t& adminCount) {
  const auto rows = tx.exec_params("SELECT id, user_id, role FROM organization_memberships WHERE organization_id = $1::uuid ORDER BY id FOR UPDATE", org.str());  // fixed order => no lock-order deadlocks
  std::optional<LockedMembership> found;
  adminCount = 0;
  for (const auto& r : rows) {
    if (r["role"].as<std::string>() == "company_admin") ++adminCount;
    if (r["id"].as<std::string>() == membership.str()) found = LockedMembership{r["user_id"].as<std::string>(), r["role"].as<std::string>()};
  }
  if (!found) throw notFound("Membership not found");  // also covers a membership id belonging to another organisation
  return *found;
}
}  // namespace

nlohmann::json OrganizationService::changeRole(const auth::Principal& who, const Uuid& org, const Uuid& membership, auth::Role role, const RequestInfo& req) {
  return pool_.inTransaction([&](pqxx::work& tx) {
    std::size_t admins = 0;
    const LockedMembership current = lockAndFind(tx, org, membership, admins);
    if (current.role == "company_admin" && role != auth::Role::CompanyAdmin && admins <= 1)
      throw conflict("An organization must keep at least one company administrator");
    tx.exec_params("UPDATE organization_memberships SET role = $2 WHERE id = $1::uuid", membership.str(), std::string(auth::toString(role)));
    audit_.record(tx, {who.userId, org, "membership.role_changed", "membership", membership.str(),
                       {{"user_id", current.userId}, {"from", current.role}, {"to", std::string(auth::toString(role))}}}, req);
    const auto rows = tx.exec_params("SELECT m.id, m.user_id, u.email, u.display_name, m.role, " + isoTs("m.created_at") + " AS created_at FROM organization_memberships m "
                                     "JOIN users u ON u.id = m.user_id WHERE m.id = $1::uuid", membership.str());
    return memberJson(rows[0]);
  });
}

void OrganizationService::removeMember(const auth::Principal& who, const Uuid& org, const Uuid& membership, const RequestInfo& req) {
  pool_.inTransaction([&](pqxx::work& tx) {
    std::size_t admins = 0;
    const LockedMembership current = lockAndFind(tx, org, membership, admins);
    if (current.role == "company_admin" && admins <= 1) throw conflict("An organization must keep at least one company administrator");
    tx.exec_params("DELETE FROM organization_memberships WHERE id = $1::uuid", membership.str());
    audit_.record(tx, {who.userId, org, "membership.removed", "membership", membership.str(), {{"user_id", current.userId}, {"role", current.role}}}, req);
  });
}

}  // namespace carbon::domain
