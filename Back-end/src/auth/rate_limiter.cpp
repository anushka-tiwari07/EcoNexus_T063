#include "carbon/auth/rate_limiter.hpp"

namespace carbon::auth {

RateLimiter::Decision RateLimiter::hit(const std::string& key) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto now = clock_();
  if (events_.size() > 10000) {  // bound memory: drop idle keys
    for (auto it = events_.begin(); it != events_.end();) {
      if (it->second.empty() || now - it->second.back() >= window_) it = events_.erase(it);
      else ++it;
    }
  }
  auto& q = events_[key];
  while (!q.empty() && now - q.front() >= window_) q.pop_front();
  if (q.size() >= max_) {
    const auto wait = std::chrono::duration_cast<std::chrono::seconds>(window_ - (now - q.front())) + std::chrono::seconds(1);
    return {false, wait};
  }
  q.push_back(now);
  return {true, std::chrono::seconds(0)};
}

}  // namespace carbon::auth
