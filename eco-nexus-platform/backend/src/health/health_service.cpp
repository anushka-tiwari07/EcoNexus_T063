#include "carbon/health/health_service.hpp"

namespace carbon::health {

ProbeResult DatabaseProbe::check() {
  const auto start = std::chrono::steady_clock::now();
  pqxx::row row = pool_.inTransaction([](pqxx::work& tx) { return tx.exec1("SELECT 1"); });
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  return {row[0].as<int>() == 1, {{"latency_ms", ms}}};
}

ProbeResult MigrationProbe::check() {
  auto lease = pool_.acquire();
  const db::MigrationStatus s = db::inspectMigrations(*lease, files_);
  // Counts only: version names are not needed by monitors.
  return {s.upToDate(), {{"applied", s.applied}, {"pending", s.pending.size()}, {"unknown", s.unknown.size()}, {"modified", s.modified.size()}}};
}

nlohmann::json HealthService::live() const {
  const auto up = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started_).count();
  return {{"status", "ok"}, {"version", version_}, {"uptime_seconds", up}};
}

ReadyReport HealthService::ready() {
  ReadyReport report;
  report.ready = true;
  nlohmann::json checks = nlohmann::json::object();
  for (auto& probe : probes_) {
    nlohmann::json entry;
    try {
      ProbeResult r = probe->check();
      entry = std::move(r.detail);
      entry["status"] = r.ok ? "ok" : "fail";
      if (!r.ok) report.ready = false;
    } catch (const std::exception& e) {
      // Details go to the log; the public response stays generic.
      logger_.warn("health probe failed", {{"probe", probe->name()}, {"error", e.what()}});
      entry = {{"status", "fail"}, {"error", "check failed"}};
      report.ready = false;
    }
    checks[probe->name()] = std::move(entry);
  }
  report.body = {{"status", report.ready ? "ready" : "not_ready"}, {"version", version_}, {"checks", std::move(checks)}};
  return report;
}

}  // namespace carbon::health
