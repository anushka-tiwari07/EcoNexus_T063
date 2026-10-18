#include "carbon/db/pool.hpp"

#include "test_support.hpp"

#include <atomic>
#include <thread>

using namespace carbon;
using namespace carbon::db;
using namespace std::chrono_literals;

TEST(Pool, ReusesReleasedConnections) {
  REQUIRE_TEST_DB();
  tsup::TempDatabase tdb;
  ConnectionPool pool(tdb.url(), 2);
  EXPECT_EQ(pool.openConnections(), 0u);  // lazy
  { auto l = pool.acquire(); }
  EXPECT_EQ(pool.openConnections(), 1u);
  EXPECT_EQ(pool.idleConnections(), 1u);
  { auto l = pool.acquire(); }
  EXPECT_EQ(pool.openConnections(), 1u);  // same connection reused
}

TEST(Pool, TimesOutWhenExhaustedThenRecovers) {
  REQUIRE_TEST_DB();
  tsup::TempDatabase tdb;
  ConnectionPool pool(tdb.url(), 1);
  auto held = std::make_unique<ConnectionPool::Lease>(pool.acquire());
  EXPECT_THROW(pool.acquire(50ms), PoolTimeout);
  held.reset();
  EXPECT_NO_THROW(pool.acquire(50ms));
}

TEST(Pool, WaiterIsWokenWhenConnectionReleased) {
  REQUIRE_TEST_DB();
  tsup::TempDatabase tdb;
  ConnectionPool pool(tdb.url(), 1);
  auto held = std::make_unique<ConnectionPool::Lease>(pool.acquire());
  std::thread releaser([&] { std::this_thread::sleep_for(100ms); held.reset(); });
  EXPECT_NO_THROW(pool.acquire(3s));
  releaser.join();
}

TEST(Pool, ConcurrentUseNeverExceedsMax) {
  REQUIRE_TEST_DB();
  tsup::TempDatabase tdb;
  ConnectionPool pool(tdb.url(), 3);
  std::atomic<int> inUse{0}, maxInUse{0}, errors{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < 12; ++t) {
    threads.emplace_back([&] {
      for (int i = 0; i < 15; ++i) {
        try {
          auto lease = pool.acquire(10s);
          const int now = ++inUse;
          int prev = maxInUse.load();
          while (now > prev && !maxInUse.compare_exchange_weak(prev, now)) {}
          pqxx::nontransaction tx(*lease);
          tx.exec("SELECT pg_sleep(0.002)");
          --inUse;
        } catch (const std::exception&) {
          ++errors;
          --inUse;
        }
      }
    });
  }
  for (auto& t : threads) t.join();
  EXPECT_EQ(errors.load(), 0);
  EXPECT_LE(maxInUse.load(), 3);
  EXPECT_GE(maxInUse.load(), 2);  // sanity: there really was concurrency
  EXPECT_LE(pool.openConnections(), 3u);
}

TEST(Pool, DiscardedLeaseIsNotReused) {
  REQUIRE_TEST_DB();
  tsup::TempDatabase tdb;
  ConnectionPool pool(tdb.url(), 2);
  {
    auto l = pool.acquire();
    l.discard();
  }
  EXPECT_EQ(pool.openConnections(), 0u);
  EXPECT_EQ(pool.idleConnections(), 0u);
}

TEST(Pool, FailedConnectFreesTheSlot) {
  ConnectionPool pool("postgresql://nobody:nopass@127.0.0.1:1/none?connect_timeout=2", 2);
  EXPECT_THROW(pool.acquire(3s), std::exception);
  EXPECT_THROW(pool.acquire(3s), std::exception);  // would time out instead if the slot had leaked
  EXPECT_EQ(pool.openConnections(), 0u);
}

TEST(Pool, TransactionCommitsOnSuccessAndRollsBackOnException) {
  REQUIRE_TEST_DB();
  tsup::TempDatabase tdb;
  ConnectionPool pool(tdb.url(), 2);
  pool.inTransaction([](pqxx::work& tx) { tx.exec0("CREATE TABLE t (x int)"); });

  const int committed = pool.inTransaction([](pqxx::work& tx) {
    tx.exec0("INSERT INTO t VALUES (1)");
    return 42;
  });
  EXPECT_EQ(committed, 42);

  EXPECT_THROW(pool.inTransaction([](pqxx::work& tx) {
    tx.exec0("INSERT INTO t VALUES (2)");
    throw std::runtime_error("boom");
  }), std::runtime_error);

  const long count = pool.inTransaction([](pqxx::work& tx) { return tx.exec1("SELECT count(*) FROM t")[0].as<long>(); });
  EXPECT_EQ(count, 1);  // the second insert was rolled back
  EXPECT_EQ(pool.openConnections(), pool.idleConnections());  // nothing leaked
}
