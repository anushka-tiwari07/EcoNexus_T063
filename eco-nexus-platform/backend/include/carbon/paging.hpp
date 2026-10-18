// Pagination/filter/sort request model and shared SQL helpers. Sort columns come ONLY from
// per-endpoint whitelists, so user text never becomes SQL.
#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace carbon {

struct PageRequest {
  std::size_t limit = 25;   // 1..100
  std::size_t offset = 0;
  std::string sort;         // validated against a whitelist by the HTTP layer
  bool descending = false;
  std::string q;            // free-text filter (matched with LIKE, wildcards escaped)
  std::optional<bool> isActive;
  std::string action;       // audit filter
};

nlohmann::json pageJson(nlohmann::json data, const PageRequest& page, std::size_t total);

// Escapes % _ \ so user text matches literally inside LIKE ... ESCAPE '\'.
std::string escapeLike(std::string_view text);
// SQL expression rendering a timestamptz column as ISO-8601 UTC text.
std::string isoTs(const std::string& column);

}  // namespace carbon
