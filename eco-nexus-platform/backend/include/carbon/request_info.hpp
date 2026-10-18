// Per-request facts passed from the HTTP layer to services (audit trail, session metadata).
#pragma once
#include <optional>
#include <string>

namespace carbon {
struct RequestInfo {
  std::string requestId;
  std::string ip;         // remote address as seen by the server (X-Forwarded-For is NOT trusted)
  std::string userAgent;  // sanitised and truncated
};

// Returns the address only if it is a valid IPv4/IPv6 literal (safe to cast to inet), else nullopt.
std::optional<std::string> inetOrNull(const std::string& ip);
// Strips control characters and truncates to `max` bytes.
std::string sanitizeHeaderText(std::string_view text, std::size_t max = 512);
}  // namespace carbon
