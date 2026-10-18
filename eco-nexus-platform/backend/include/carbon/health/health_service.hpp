// Health monitoring. HealthProbe is an abstract interface (runtime polymorphism);
// HealthService aggregates probes and never lets a failing probe take the endpoint down.
#pragma once

#include "carbon/db/migrator.hpp"
#include "carbon/db/pool.hpp"
#include "carbon/logging.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace carbon::health {

struct ProbeResult {
  bool ok = false;
  nlohmann::json detail = nlohmann::json::object();  // must not contain secrets or internal paths
};

class HealthProbe {
 public:
  virtual ~HealthProbe() = default;
  virtual std::string name() const = 0;
  virtual ProbeResult check() = 0;  // may throw; HealthService converts that into a failed check
};

// SELECT 1 through the pool, with latency.
class DatabaseProbe final : public HealthProbe {
 public:
  explicit DatabaseProbe(db::ConnectionPool& pool) : pool_(pool) {}
  std::string name() const override { return "database"; }
  ProbeResult check() override;

 private:
  db::ConnectionPool& pool_;
};

// Ready only when every migration file has been applied and none were altered.
class MigrationProbe final : public HealthProbe {
 public:
  MigrationProbe(db::ConnectionPool& pool, std::vector<db::MigrationFile> files) : pool_(pool), files_(std::move(files)) {}
  std::string name() const override { return "migrations"; }
  ProbeResult check() override;

 private:
  db::ConnectionPool& pool_;
  std::vector<db::MigrationFile> files_;
};

struct ReadyReport {
  bool ready = false;
  nlohmann::json body;
};

class HealthService {
 public:
  HealthService(std::string version, Logger& logger) : version_(std::move(version)), logger_(logger), started_(std::chrono::steady_clock::now()) {}

  void addProbe(std::unique_ptr<HealthProbe> probe) { probes_.push_back(std::move(probe)); }

  nlohmann::json live() const;  // process is up; no dependencies are touched
  ReadyReport ready();          // all probes must pass

 private:
  std::string version_;
  Logger& logger_;
  std::chrono::steady_clock::time_point started_;
  std::vector<std::unique_ptr<HealthProbe>> probes_;
};

}  // namespace carbon::health
