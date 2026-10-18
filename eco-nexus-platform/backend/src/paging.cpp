#include "carbon/paging.hpp"

namespace carbon {

nlohmann::json pageJson(nlohmann::json data, const PageRequest& p, std::size_t total) {
  return {{"data", std::move(data)}, {"page", {{"limit", p.limit}, {"offset", p.offset}, {"total", total}}}};
}

std::string escapeLike(std::string_view text) {
  std::string out;
  for (const char c : text) {
    if (c == '%' || c == '_' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

std::string isoTs(const std::string& column) {
  return "to_char(" + column + " AT TIME ZONE 'UTC', 'YYYY-MM-DD\"T\"HH24:MI:SS.MS\"Z\"')";
}

}  // namespace carbon
