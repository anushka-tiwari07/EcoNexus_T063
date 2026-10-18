#include "carbon/request_info.hpp"

#include <arpa/inet.h>

namespace carbon {
std::optional<std::string> inetOrNull(const std::string& ip) {
  unsigned char buf[16];
  if (ip.empty() || ip.size() > 45) return std::nullopt;
  if (::inet_pton(AF_INET, ip.c_str(), buf) == 1 || ::inet_pton(AF_INET6, ip.c_str(), buf) == 1) return ip;
  return std::nullopt;
}

std::string sanitizeHeaderText(std::string_view text, std::size_t max) {
  std::string out;
  for (const char ch : text) {
    const auto c = static_cast<unsigned char>(ch);
    if (c >= 0x20 && c != 0x7f) out.push_back(ch);
    if (out.size() >= max) break;
  }
  return out;
}
}  // namespace carbon
