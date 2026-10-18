#include "carbon/db/pool.hpp"

#include <stdexcept>

namespace carbon::db {

ConnectionPool::ConnectionPool(std::string connectionUrl, std::size_t maxConnections)
    : url_(std::move(connectionUrl)), max_(maxConnections) {
  if (max_ == 0) throw std::invalid_argument("ConnectionPool needs at least one connection");
}

ConnectionPool::Lease::~Lease() {
  if (pool_ && conn_) pool_->release(std::move(conn_), broken_);
}

ConnectionPool::Lease ConnectionPool::acquire(std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  std::unique_lock<std::mutex> lock(mutex_);
  for (;;) {
    if (!cv_.wait_until(lock, deadline, [&] { return !idle_.empty() || created_ < max_; })) throw PoolTimeout();
    if (!idle_.empty()) {
      auto conn = std::move(idle_.back());
      idle_.pop_back();
      if (conn->is_open()) return Lease(*this, std::move(conn));
      --created_;  // stale connection: forget it and try again
      continue;
    }
    ++created_;  // reserve a slot, then connect without holding the lock
    lock.unlock();
    try {
      return Lease(*this, std::make_unique<pqxx::connection>(url_));
    } catch (...) {
      lock.lock();
      --created_;
      cv_.notify_one();
      throw;
    }
  }
}

void ConnectionPool::release(std::unique_ptr<pqxx::connection> conn, bool broken) noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  if (broken || !conn->is_open()) {
    --created_;
  } else {
    try {
      idle_.push_back(std::move(conn));
    } catch (...) {
      --created_;  // could not store it; the unique_ptr destructor closes the connection
    }
  }
  cv_.notify_one();
}

std::size_t ConnectionPool::openConnections() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return created_;
}
std::size_t ConnectionPool::idleConnections() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return idle_.size();
}

}  // namespace carbon::db
