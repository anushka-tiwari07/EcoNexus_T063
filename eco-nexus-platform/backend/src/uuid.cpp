#include "carbon/uuid.hpp"

#include <cctype>

namespace carbon {
std::optional<Uuid> Uuid::parse(std::string_view t) {
  if (t.size() != 36) return std::nullopt;
  std::string out(t);
  for (std::size_t i = 0; i < out.size(); ++i) {
    const bool dash = (i == 8 || i == 13 || i == 18 || i == 23);
    const unsigned char c = static_cast<unsigned char>(out[i]);
    if (dash ? c != '-' : !std::isxdigit(c)) return std::nullopt;
    out[i] = static_cast<char>(std::tolower(c));
  }
  return Uuid(std::move(out));
}
}  // namespace carbon
