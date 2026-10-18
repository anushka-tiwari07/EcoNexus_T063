// carbon_migrate: `carbon_migrate up` applies pending migrations; `carbon_migrate status` reports them.
#include "carbon/config.hpp"
#include "carbon/db/migrator.hpp"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
  using namespace carbon;
  const std::string command = argc > 1 ? argv[1] : "up";
  if (command != "up" && command != "status") {
    std::cerr << "usage: carbon_migrate [up|status]\n";
    return 64;
  }
  try {
    const Config config = Config::fromProcessEnvironment();
    Logger logger(std::cout, config.logLevel);
    const auto files = db::loadMigrations(config.migrationsDir);
    pqxx::connection conn(config.databaseUrl);
    if (command == "status") {
      const auto s = db::inspectMigrations(conn, files);
      std::cout << "applied: " << s.applied << ", pending: " << s.pending.size() << ", unknown: " << s.unknown.size()
                << ", modified: " << s.modified.size() << "\n";
      return s.upToDate() ? 0 : 1;
    }
    const auto report = db::applyMigrations(conn, files, &logger);
    std::cout << "migrations applied now: " << report.applied.size() << ", already applied: " << report.alreadyApplied << "\n";
    return 0;
  } catch (const ConfigError& e) {
    std::cerr << "carbon_migrate: " << e.what() << "\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "carbon_migrate: " << e.what() << "\n";
    return 1;
  }
}
