#include "carbon/audit/audit_service.hpp"

namespace carbon::audit {

void AuditService::record(pqxx::work& tx, const AuditEntry& e, const RequestInfo& r) const {
  std::optional<std::string> actor, org;
  if (e.actor) actor = e.actor->str();
  if (e.organization) org = e.organization->str();
  // `{}` initialises a nlohmann::json to null; the table requires an object, so normalise here.
  const nlohmann::json details = e.details.is_object() ? e.details : nlohmann::json::object();
  tx.exec_params(
      "INSERT INTO audit_events (actor_user_id, organization_id, action, entity_type, entity_id, request_id, ip_address, details) "
      "VALUES ($1::uuid, $2::uuid, $3, $4, $5, $6, $7::inet, $8::jsonb)",
      actor, org, e.action, e.entityType, e.entityId, r.requestId, inetOrNull(r.ip), details.dump());
}

void AuditService::recordStandalone(const AuditEntry& e, const RequestInfo& r) const {
  pool_.inTransaction([&](pqxx::work& tx) { record(tx, e, r); });
}

nlohmann::json AuditService::listForOrganization(const Uuid& org, const PageRequest& page) const {
  return pool_.inTransaction([&](pqxx::work& tx) {
    const std::string like = "%" + escapeLike(page.action) + "%";
    const std::string where = "WHERE organization_id = $1::uuid AND ($2 = '' OR action ILIKE $3 ESCAPE '\\')";
    const std::size_t total = tx.exec_params1("SELECT count(*) FROM audit_events " + where, org.str(), page.action, like)[0].as<std::size_t>();
    const std::string order = page.sort == "action" ? "action" : "occurred_at";
    const auto rows = tx.exec_params(
        "SELECT id, " + isoTs("occurred_at") + " AS occurred_at, actor_user_id, action, entity_type, entity_id, request_id, details::text AS details "
        "FROM audit_events " + where + " ORDER BY " + order + (page.descending ? " DESC" : " ASC") + ", id LIMIT $4 OFFSET $5",
        org.str(), page.action, like, page.limit, page.offset);
    nlohmann::json data = nlohmann::json::array();
    for (const auto& row : rows) {
      data.push_back({{"id", row["id"].as<std::string>()},
                      {"occurred_at", row["occurred_at"].as<std::string>()},
                      {"actor_user_id", row["actor_user_id"].is_null() ? nlohmann::json(nullptr) : nlohmann::json(row["actor_user_id"].as<std::string>())},
                      {"action", row["action"].as<std::string>()},
                      {"entity_type", row["entity_type"].is_null() ? nlohmann::json(nullptr) : nlohmann::json(row["entity_type"].as<std::string>())},
                      {"entity_id", row["entity_id"].is_null() ? nlohmann::json(nullptr) : nlohmann::json(row["entity_id"].as<std::string>())},
                      {"request_id", row["request_id"].is_null() ? nlohmann::json(nullptr) : nlohmann::json(row["request_id"].as<std::string>())},
                      {"details", nlohmann::json::parse(row["details"].as<std::string>())}});
    }
    return pageJson(std::move(data), page, total);
  });
}

}  // namespace carbon::audit
