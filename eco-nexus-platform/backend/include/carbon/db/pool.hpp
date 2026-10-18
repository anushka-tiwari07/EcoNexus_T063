// Bounded, thread-safe PostgreSQL connection pool with RAII leases.
#pragma once

#include <pqxx/pqxx>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace carbon::db {

class PoolTimeout : public std::runtime_error {
 public:
  PoolTimeout() : std::runtime_error("timed out waiting for a database connection") {}
};

class ConnectionPool {
 public:
  // RAII handle: returns the connection to the pool on destruction. Must not outlive the pool.
  class Lease {
   public:
    Lease(Lease&& other) noexcept : pool_(other.pool_), conn_(std::move(other.conn_)), broken_(other.broken_) { other.pool_ = nullptr; }
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;
    Lease& operator=(Lease&&) = delete;
    ~Lease();

    pqxx::connection& operator*() const noexcept { return *conn_; }
    pqxx::connection* operator->() const noexcept { return conn_.get(); }
    void discard() noexcept { broken_ = true; }  // connection is not returned to the pool

   private:
    friend class ConnectionPool;
    Lease(ConnectionPool& pool, std::unique_ptr<pqxx::connection> conn) : pool_(&pool), conn_(std::move(conn)) {}
    ConnectionPool* pool_;
    std::unique_ptr<pqxx::connection> conn_;
    bool broken_ = false;
  };

  ConnectionPool(std::string connectionUrl, std::size_t maxConnections);
  ConnectionPool(const ConnectionPool&) = delete;
  ConnectionPool& operator=(const ConnectionPool&) = delete;

  // Connections are opened lazily. Throws PoolTimeout, or pqxx::broken_connection if the DB is down.
  Lease acquire(std::chrono::milliseconds timeout = std::chrono::seconds(5));

  // Runs fn(pqxx::work&) in one transaction: commit on return, roll back on any exception.
  template <class F>
  auto inTransaction(F&& fn) {
    Lease lease = acquire();
    try {
      pqxx::work tx(*lease);
      if constexpr (std::is_void_v<std::invoke_result_t<F, pqxx::work&>>) {
        fn(tx);
        tx.commit();
      } else {
        auto result = fn(tx);
        tx.commit();
        return result;
      }
    } catch (const pqxx::broken_connection&) {
      lease.discard();
      throw;
    }
  }

  std::size_t maxConnections() const noexcept { return max_; }
  std::size_t openConnections() const;
  std::size_t idleConnections() const;

 private:
  void release(std::unique_ptr<pqxx::connection> conn, bool broken) noexcept;

  const std::string url_;
  const std::size_t max_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<std::unique_ptr<pqxx::connection>> idle_;
  std::size_t created_ = 0;  // open + currently being opened
};

}  // namespace carbon::db
