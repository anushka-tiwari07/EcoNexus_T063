// Sliding-window rate limiter (in-memory, per process). Good for a single instance; a multi-instance
// deployment would back this with Redis (planned, not implemented). Clock is injectable for tests.
#pragma once

#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace carbon::auth {

class RateLimiter {
 public:
  using Clock = std::function<std::chrono::steady_clock::time_point()>;
  struct Decision {
    bool allowed;
    std::chrono::seconds retryAfter;
  };

  RateLimiter(std::size_t maxEvents, std::chrono::seconds window, Clock clock = [] { return std::chrono::steady_clock::now(); })
      : max_(maxEvents), window_(window), clock_(std::move(clock)) {}

  Decision hit(const std::string& key);  // records the attempt; blocked attempts are not counted

 private:
  std::size_t max_;
  std::chrono::seconds window_;
  Clock clock_;
  std::mutex mutex_;
  std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>> events_;
};

}  // namespace carbon::auth
