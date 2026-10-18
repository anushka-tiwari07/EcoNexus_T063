// Strongly typed UUID: ids coming from URLs/JSON are parsed once, so malformed text never reaches SQL.
#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace carbon {
class Uuid {
 public:
  static std::optional<Uuid> parse(std::string_view text);  // 8-4-4-4-12 hex, normalised to lower case
  const std::string& str() const noexcept { return value_; }
  bool operator==(const Uuid& o) const noexcept { return value_ == o.value_; }
  bool operator!=(const Uuid& o) const noexcept { return value_ != o.value_; }

 private:
  explicit Uuid(std::string v) : value_(std::move(v)) {}
  std::string value_;
};
}  // namespace carbon
