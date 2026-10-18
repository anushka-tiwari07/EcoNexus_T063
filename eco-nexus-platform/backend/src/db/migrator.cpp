#include "carbon/db/migrator.hpp"

#include "carbon/crypto.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>

namespace carbon::db {
namespace {
constexpr long long kAdvisoryLockKey = 727'272'001;  // arbitrary app-specific constant

// Holds the session-level advisory lock for the lifetime of the object.
class AdvisoryLock {
 public:
  explicit AdvisoryLock(pqxx::connection& c) : conn_(c) {
    pqxx::nontransaction tx(conn_);
    tx.exec_params("SELECT pg_advisory_lock($1)", kAdvisoryLockKey);
  }
  ~AdvisoryLock() {
    try {
      pqxx::nontransaction tx(conn_);
      tx.exec_params("SELECT pg_advisory_unlock($1)", kAdvisoryLockKey);
    } catch (...) {  // connection already gone: the server releases the lock itself
    }
  }
  AdvisoryLock(const AdvisoryLock&) = delete;
  AdvisoryLock& operator=(const AdvisoryLock&) = delete;

 private:
  pqxx::connection& conn_;
};

void ensureTable(pqxx::connection& conn) {
  pqxx::work tx(conn);
  tx.exec0(
      "CREATE TABLE IF NOT EXISTS schema_migrations ("
      " version text PRIMARY KEY,"
      " name text NOT NULL,"
      " checksum text NOT NULL,"
      " applied_at timestamptz NOT NULL DEFAULT now())");
  tx.commit();
}

std::map<std::string, std::string> readApplied(pqxx::connection& conn) {
  std::map<std::string, std::string> applied;
  pqxx::nontransaction tx(conn);
  if (tx.exec("SELECT to_regclass('public.schema_migrations') IS NOT NULL").at(0).at(0).as<bool>()) {
    for (const auto& row : tx.exec("SELECT version, checksum FROM schema_migrations")) applied[row[0].as<std::string>()] = row[1].as<std::string>();
  }
  return applied;
}

MigrationStatus compare(const std::map<std::string, std::string>& applied, const std::vector<MigrationFile>& files) {
  MigrationStatus s;
  std::map<std::string, const MigrationFile*> byVersion;
  for (const auto& f : files) byVersion[f.version] = &f;
  for (const auto& f : files) {
    const auto it = applied.find(f.version);
    if (it == applied.end()) s.pending.push_back(f.version);
    else {
      ++s.applied;
      if (it->second != f.checksum) s.modified.push_back(f.version);
    }
  }
  for (const auto& [version, checksum] : applied) {
    (void)checksum;
    if (!byVersion.count(version)) s.unknown.push_back(version);
  }
  return s;
}
}  // namespace

std::vector<MigrationFile> loadMigrations(const std::filesystem::path& dir) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) throw MigrationError("migrations directory not found: " + dir.string());

  static const std::regex kName(R"(^(\d{4})_([a-z0-9_]+)\.sql$)");
  std::vector<MigrationFile> files;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (!entry.is_regular_file()) continue;
    const std::string filename = entry.path().filename().string();
    std::smatch m;
    if (!std::regex_match(filename, m, kName)) continue;  // README, editor backups, etc.
    std::ifstream in(entry.path(), std::ios::binary);
    if (!in) throw MigrationError("cannot read migration file: " + filename);
    std::ostringstream buf;
    buf << in.rdbuf();
    MigrationFile f{m[1].str(), m[2].str(), buf.str(), {}};
    if (f.sql.find_first_not_of(" \t\r\n") == std::string::npos) throw MigrationError("migration file is empty: " + filename);
    f.checksum = sha256Hex(f.sql);
    files.push_back(std::move(f));
  }
  if (files.empty()) throw MigrationError("no migration files (NNNN_name.sql) found in " + dir.string());
  std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.version < b.version; });
  for (std::size_t i = 1; i < files.size(); ++i)
    if (files[i].version == files[i - 1].version) throw MigrationError("duplicate migration version " + files[i].version);
  return files;
}

MigrationStatus inspectMigrations(pqxx::connection& conn, const std::vector<MigrationFile>& files) {
  return compare(readApplied(conn), files);
}

MigrationReport applyMigrations(pqxx::connection& conn, const std::vector<MigrationFile>& files, Logger* logger) {
  AdvisoryLock lock(conn);
  ensureTable(conn);

  const MigrationStatus status = compare(readApplied(conn), files);
  auto join = [](const std::vector<std::string>& v) {
    std::string out;
    for (const auto& s : v) out += (out.empty() ? "" : ", ") + s;
    return out;
  };
  if (!status.modified.empty())
    throw MigrationError("applied migration(s) were modified after being applied: " + join(status.modified) +
                         ". Never edit an applied migration; add a new one.");
  if (!status.unknown.empty())
    throw MigrationError("database has migration(s) this build does not know: " + join(status.unknown) +
                         ". Deploy a newer build or restore the missing files.");

  MigrationReport report;
  report.alreadyApplied = status.applied;
  for (const auto& f : files) {
    if (std::find(status.pending.begin(), status.pending.end(), f.version) == status.pending.end()) continue;
    try {
      pqxx::work tx(conn);
      tx.exec(f.sql);  // multi-statement script; the whole file is one transaction
      tx.exec_params("INSERT INTO schema_migrations (version, name, checksum) VALUES ($1, $2, $3)", f.version, f.name, f.checksum);
      tx.commit();
    } catch (const std::exception& e) {
      throw MigrationError("migration " + f.version + "_" + f.name + " failed and was rolled back: " + e.what());
    }
    if (logger) logger->info("migration applied", {{"version", f.version}, {"name", f.name}});
    report.applied.push_back(f.version);
  }
  return report;
}

}  // namespace carbon::db
