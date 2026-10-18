// carbon_seed: creates DEVELOPMENT-ONLY demo accounts and fabricated demo data. Idempotent.
// Refuses to run when CARBON_ENV=production. All emails use the reserved .test TLD; the shared password
// is public (printed below and in docs) and must never be used anywhere real.
#include "carbon/auth/password.hpp"
#include "carbon/config.hpp"
#include "carbon/db/migrator.hpp"

#include <pqxx/pqxx>

#include <iostream>

namespace {
constexpr const char* kDevPassword = "DevOnly-ChangeMe-123!";
struct Account { const char* email; const char* name; bool platformAdmin; };
}  // namespace

int main() {
  using namespace carbon;
  try {
    const Config config = Config::fromProcessEnvironment();
    if (config.isProduction()) {
      std::cerr << "carbon_seed: refusing to seed demo accounts when CARBON_ENV=production\n";
      return 2;
    }
    const auto files = db::loadMigrations(config.migrationsDir);
    pqxx::connection conn(config.databaseUrl);
    if (!db::inspectMigrations(conn, files).upToDate()) {
      std::cerr << "carbon_seed: run `carbon_migrate up` first\n";
      return 4;
    }
    const std::string hash = auth::ArgonPasswordHasher::interactive().hash(kDevPassword);
    const Account accounts[] = {{"platform.admin@dev.test", "Platform Admin (demo)", true},
                                {"admin@acme.dev.test", "Acme Admin (demo)", false},
                                {"operator@acme.dev.test", "Acme Operator (demo)", false},
                                {"consultant@dev.test", "Demo Consultant", false},
                                {"reviewer@dev.test", "Demo Reviewer", false},
                                {"admin@borealis.dev.test", "Borealis Admin (demo)", false}};
    pqxx::work tx(conn);
    auto userId = [&](const char* email) { return tx.exec_params1("SELECT id FROM users WHERE lower(email) = lower($1)", email)[0].as<std::string>(); };
    for (const auto& a : accounts)
      tx.exec_params("INSERT INTO users (email, password_hash, display_name, is_platform_admin) SELECT $1, $2, $3, $4 "
                     "WHERE NOT EXISTS (SELECT 1 FROM users WHERE lower(email) = lower($1))", a.email, hash, a.name, a.platformAdmin);
    auto org = [&](const char* name, const char* sector) {
      tx.exec_params("INSERT INTO organizations (name, sector) SELECT $1, $2 WHERE NOT EXISTS (SELECT 1 FROM organizations WHERE lower(name) = lower($1))", name, sector);
      return tx.exec_params1("SELECT id FROM organizations WHERE lower(name) = lower($1)", name)[0].as<std::string>();
    };
    const std::string acme = org("Acme Cement (demo)", "cement"), borealis = org("Borealis Steel (demo)", "steel");
    auto member = [&](const std::string& o, const char* email, const char* role) {
      tx.exec_params("INSERT INTO organization_memberships (organization_id, user_id, role) VALUES ($1::uuid, $2::uuid, $3) ON CONFLICT (organization_id, user_id) DO NOTHING", o, userId(email), role);
    };
    member(acme, "admin@acme.dev.test", "company_admin");
    member(acme, "operator@acme.dev.test", "company_operator");
    member(acme, "consultant@dev.test", "consultant");
    member(acme, "reviewer@dev.test", "reviewer");
    member(borealis, "admin@borealis.dev.test", "company_admin");
    member(borealis, "consultant@dev.test", "consultant");
    auto facility = [&](const std::string& o, const char* name, const char* sector, const char* unit) {
      tx.exec_params("INSERT INTO facilities (organization_id, name, sector, production_unit) VALUES ($1::uuid, $2, $3, $4) ON CONFLICT (organization_id, name) DO NOTHING", o, name, sector, unit);
    };
    facility(acme, "Plant 1 - Kiln Line (demo)", "cement", "tonne cement");
    facility(acme, "Plant 2 - Grinding Unit (demo)", "cement", "tonne cement");
    facility(borealis, "Borealis Rolling Mill (demo)", "steel", "tonne crude steel");
    tx.commit();

    std::cout << "Development accounts (password for ALL: " << kDevPassword << ")\n";
    for (const auto& a : accounts) std::cout << "  " << a.email << (a.platformAdmin ? "   [platform admin]" : "") << "\n";
    std::cout << "Organizations: Acme Cement (demo), Borealis Steel (demo). All data is fabricated.\n";
    return 0;
  } catch (const ConfigError& e) {
    std::cerr << "carbon_seed: " << e.what() << "\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "carbon_seed: " << e.what() << "\n";
    return 1;
  }
}
