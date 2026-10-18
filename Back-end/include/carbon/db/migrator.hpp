// Forward-only SQL migration runner.
//  * files: NNNN_snake_case.sql, applied in version order, each inside one transaction
//  * applied versions + SHA-256 checksums are recorded in schema_migrations
//  * editing an already-applied file is detected and refused (never silently diverge)
//  * a PostgreSQL advisory lock makes concurrent runners safe
#pragma once

#include "carbon/logging.hpp"

#include <pqxx/pqxx>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace carbon::db {

class MigrationError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct MigrationFile {
  std::string version;   // "0001"
  std::string name;      // "core_identity"
  std::string sql;
  std::string checksum;  // sha256 hex of sql
};

struct MigrationReport {
  std::vector<std::string> applied;  // versions applied by this call
  std::size_t alreadyApplied = 0;
};

struct MigrationStatus {
  std::size_t applied = 0;
  std::vector<std::string> pending;   // in files, not yet in the database
  std::vector<std::string> unknown;   // in the database, missing from files (newer build?)
  std::vector<std::string> modified;  // applied, but file content changed afterwards
  bool upToDate() const noexcept { return pending.empty() && unknown.empty() && modified.empty(); }
};

std::vector<MigrationFile> loadMigrations(const std::filesystem::path& dir);
MigrationReport applyMigrations(pqxx::connection& conn, const std::vector<MigrationFile>& files, Logger* logger = nullptr);
MigrationStatus inspectMigrations(pqxx::connection& conn, const std::vector<MigrationFile>& files);  // read-only

}  // namespace carbon::db
