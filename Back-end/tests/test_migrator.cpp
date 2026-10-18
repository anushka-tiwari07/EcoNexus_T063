#include "carbon/db/migrator.hpp"

#include "test_support.hpp"

#include <fstream>
#include <atomic>
#include <iomanip>
#include <thread>

using namespace carbon;
using namespace carbon::db;
namespace fs = std::filesystem;

namespace {
// Temp directory of migration files for loader/runner tests.
class MigrationDir {
 public:
  MigrationDir() : path_(fs::temp_directory_path() / ("carbon_mig_" + randomHex(6))) { fs::create_directories(path_); }
  ~MigrationDir() { std::error_code ec; fs::remove_all(path_, ec); }
  void write(const std::string& file, const std::string& content) const { std::ofstream(path_ / file, std::ios::binary) << content; }
  const fs::path& path() const { return path_; }

 private:
  fs::path path_;
};

constexpr std::size_t kRealMigrationCount = 8;

bool tableExists(pqxx::connection& c, const std::string& name) {
  pqxx::nontransaction tx(c);
  return tx.exec_params1("SELECT to_regclass($1) IS NOT NULL", "public." + name)[0].as<bool>();
}
}  // namespace

// ---- loader (no database needed) ----
TEST(MigrationLoader, LoadsSortedAndIgnoresOtherFiles) {
  MigrationDir d;
  d.write("0002_second.sql", "SELECT 2;");
  d.write("0001_first.sql", "SELECT 1;");
  d.write("README.md", "ignore me");
  d.write("notes.sql", "SELECT 0;");  // does not match NNNN_name.sql
  const auto files = loadMigrations(d.path());
  ASSERT_EQ(files.size(), 2u);
  EXPECT_EQ(files[0].version, "0001");
  EXPECT_EQ(files[1].name, "second");
  EXPECT_EQ(files[0].checksum, sha256Hex("SELECT 1;"));
}

TEST(MigrationLoader, RejectsBadDirectories) {
  EXPECT_THROW(loadMigrations("/definitely/not/here"), MigrationError);
  MigrationDir empty;
  EXPECT_THROW(loadMigrations(empty.path()), MigrationError);
  MigrationDir blank;
  blank.write("0001_blank.sql", "  \n ");
  EXPECT_THROW(loadMigrations(blank.path()), MigrationError);
}

TEST(MigrationLoader, RealMigrationsLoadInOrder) {
  const auto files = loadMigrations(tsup::migrationsDir());
  ASSERT_EQ(files.size(), kRealMigrationCount);
  for (std::size_t i = 0; i < files.size(); ++i) EXPECT_EQ(files[i].version, (std::ostringstream() << std::setfill('0') << std::setw(4) << (i + 1)).str());
}

// ---- runner (needs PostgreSQL) ----
TEST(Migrator, AppliesEverythingAndIsIdempotent) {
  REQUIRE_TEST_DB();
  tsup::TempDatabase db;
  const auto files = loadMigrations(tsup::migrationsDir());
  pqxx::connection conn(db.url());

  const auto before = inspectMigrations(conn, files);
  EXPECT_EQ(before.pending.size(), kRealMigrationCount);
  EXPECT_FALSE(before.upToDate());

  const auto first = applyMigrations(conn, files);
  EXPECT_EQ(first.applied.size(), kRealMigrationCount);
  const auto second = applyMigrations(conn, files);
  EXPECT_TRUE(second.applied.empty());
  EXPECT_EQ(second.alreadyApplied, kRealMigrationCount);
  EXPECT_TRUE(inspectMigrations(conn, files).upToDate());
}

TEST(Migrator, RefusesModifiedAppliedMigration) {
  REQUIRE_TEST_DB();
  tsup::TempDatabase db;
  MigrationDir d;
  d.write("0001_t.sql", "CREATE TABLE t1 (id int);");
  pqxx::connection conn(db.url());
  applyMigrations(conn, loadMigrations(d.path()));

  std::ofstream(d.path() / "0001_t.sql", std::ios::binary | std::ios::trunc) << "CREATE TABLE t1 (id int, extra int);";
  const auto changed = loadMigrations(d.path());
  EXPECT_EQ(inspectMigrations(conn, changed).modified.size(), 1u);
  try {
    applyMigrations(conn, changed);
    FAIL() << "expected MigrationError";
  } catch (const MigrationError& e) {
    EXPECT_NE(std::string(e.what()).find("modified"), std::string::npos);
  }
}

TEST(Migrator, RefusesDatabaseNewerThanBuild) {
  REQUIRE_TEST_DB();
  tsup::TempDatabase db;
  MigrationDir d;
  d.write("0001_a.sql", "CREATE TABLE a (id int);");
  d.write("0002_b.sql", "CREATE TABLE b (id int);");
  pqxx::connection conn(db.url());
  applyMigrations(conn, loadMigrations(d.path()));
  fs::remove(d.path() / "0002_b.sql");
  const auto older = loadMigrations(d.path());
  EXPECT_EQ(inspectMigrations(conn, older).unknown.size(), 1u);
  EXPECT_THROW(applyMigrations(conn, older), MigrationError);
}

TEST(Migrator, FailedMigrationRollsBackOnlyItself) {
  REQUIRE_TEST_DB();
  tsup::TempDatabase db;
  MigrationDir d;
  d.write("0001_ok.sql", "CREATE TABLE good (id int);");
  d.write("0002_bad.sql", "CREATE TABLE half_done (id int); SELECT * FROM table_that_does_not_exist;");
  d.write("0003_after.sql", "CREATE TABLE never_reached (id int);");
  pqxx::connection conn(db.url());
  const auto files = loadMigrations(d.path());
  EXPECT_THROW(applyMigrations(conn, files), MigrationError);

  EXPECT_TRUE(tableExists(conn, "good"));           // earlier migration stays applied
  EXPECT_FALSE(tableExists(conn, "half_done"));     // failed migration fully rolled back
  EXPECT_FALSE(tableExists(conn, "never_reached"));  // later ones not attempted
  const auto s = inspectMigrations(conn, files);
  EXPECT_EQ(s.applied, 1u);
  EXPECT_EQ(s.pending.size(), 2u);
}

TEST(Migrator, ConcurrentRunnersApplyEachMigrationExactlyOnce) {
  REQUIRE_TEST_DB();
  tsup::TempDatabase db;
  const auto files = loadMigrations(tsup::migrationsDir());
  std::atomic<std::size_t> totalApplied{0};
  std::atomic<int> failures{0};
  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back([&] {
      try {
        pqxx::connection conn(db.url());
        totalApplied += applyMigrations(conn, files).applied.size();
      } catch (const std::exception&) {
        ++failures;
      }
    });
  }
  for (auto& t : threads) t.join();
  EXPECT_EQ(failures.load(), 0);
  EXPECT_EQ(totalApplied.load(), kRealMigrationCount);
}
