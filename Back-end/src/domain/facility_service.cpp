#include "carbon/domain/facility_service.hpp"

#include "carbon/errors.hpp"

namespace carbon::domain {
namespace {
nlohmann::json str(const pqxx::field& f) { return f.is_null() ? nlohmann::json(nullptr) : nlohmann::json(f.as<std::string>()); }

const std::string kSelect = "SELECT id, organization_id, name, sector, state, address, production_unit, boundary_notes, is_active, " +
                            isoTs("created_at") + " AS created_at, " + isoTs("updated_at") + " AS updated_at FROM facilities ";

nlohmann::json facilityJson(const pqxx::row& r) {
  return {{"id", r["id"].as<std::string>()}, {"organization_id", r["organization_id"].as<std::string>()}, {"name", r["name"].as<std::string>()},
          {"sector", r["sector"].as<std::string>()}, {"state", str(r["state"])}, {"address", str(r["address"])},
          {"production_unit", r["production_unit"].as<std::string>()}, {"boundary_notes", str(r["boundary_notes"])},
          {"is_active", r["is_active"].as<bool>()}, {"created_at", r["created_at"].as<std::string>()}, {"updated_at", r["updated_at"].as<std::string>()}};
}
}  // namespace

nlohmann::json FacilityService::list(const Uuid& org, const PageRequest& page) const {
  return pool_.inTransaction([&](pqxx::work& tx) {
    const std::string like = "%" + escapeLike(page.q) + "%";
    const std::string where = "WHERE organization_id = $1::uuid AND ($2 = '' OR name ILIKE $3 ESCAPE '\\') AND ($4 = '' OR is_active = ($4 = 'true'))";
    const std::string active = page.isActive ? (*page.isActive ? "true" : "false") : "";
    const std::string sortCol = page.sort == "sector" ? "sector" : (page.sort == "created_at" ? "created_at" : "name");
    const std::size_t total = tx.exec_params1("SELECT count(*) FROM facilities " + where, org.str(), page.q, like, active)[0].as<std::size_t>();
    const auto rows = tx.exec_params(kSelect + where + " ORDER BY " + sortCol + (page.descending ? " DESC" : " ASC") + ", id LIMIT $5 OFFSET $6",
                                     org.str(), page.q, like, active, page.limit, page.offset);
    nlohmann::json data = nlohmann::json::array();
    for (const auto& r : rows) data.push_back(facilityJson(r));
    return pageJson(std::move(data), page, total);
  });
}

nlohmann::json FacilityService::get(const Uuid& org, const Uuid& facility) const {
  return pool_.inTransaction([&](pqxx::work& tx) {
    const auto rows = tx.exec_params(kSelect + "WHERE organization_id = $1::uuid AND id = $2::uuid", org.str(), facility.str());
    if (rows.empty()) throw notFound("Facility not found");
    return facilityJson(rows[0]);
  });
}

nlohmann::json FacilityService::create(const auth::Principal& who, const Uuid& org, const NewFacility& f, const RequestInfo& req) {
  try {
    return pool_.inTransaction([&](pqxx::work& tx) {
      const std::string id = tx.exec_params1(
          "INSERT INTO facilities (organization_id, name, sector, production_unit, state, address, boundary_notes) VALUES ($1::uuid, $2, $3, $4, $5, $6, $7) RETURNING id",
          org.str(), f.name, f.sector, f.productionUnit, f.state, f.address, f.boundaryNotes)[0].as<std::string>();
      audit_.record(tx, {who.userId, org, "facility.created", "facility", id, {{"name", f.name}}}, req);
      return facilityJson(tx.exec_params(kSelect + "WHERE id = $1::uuid", id)[0]);
    });
  } catch (const pqxx::unique_violation&) {
    throw conflict("A facility with this name already exists in the organization");
  }
}

nlohmann::json FacilityService::update(const auth::Principal& who, const Uuid& org, const Uuid& facility, const FacilityPatch& p, const RequestInfo& req) {
  try {
    return pool_.inTransaction([&](pqxx::work& tx) {
      if (tx.exec_params("SELECT 1 FROM facilities WHERE organization_id = $1::uuid AND id = $2::uuid FOR UPDATE", org.str(), facility.str()).empty())
        throw notFound("Facility not found");
      nlohmann::json changed = nlohmann::json::array();
      auto set = [&](const char* col, const auto& v) {
        if (!v) return;
        tx.exec_params(std::string("UPDATE facilities SET ") + col + " = $3 WHERE organization_id = $1::uuid AND id = $2::uuid", org.str(), facility.str(), *v);
        changed.push_back(col);
      };
      set("name", p.name); set("sector", p.sector); set("production_unit", p.productionUnit);
      set("state", p.state); set("address", p.address); set("boundary_notes", p.boundaryNotes); set("is_active", p.isActive);
      if (!changed.empty()) audit_.record(tx, {who.userId, org, "facility.updated", "facility", facility.str(), {{"fields", changed}}}, req);
      return facilityJson(tx.exec_params(kSelect + "WHERE id = $1::uuid", facility.str())[0]);
    });
  } catch (const pqxx::unique_violation&) {
    throw conflict("A facility with this name already exists in the organization");
  }
}

}  // namespace carbon::domain
