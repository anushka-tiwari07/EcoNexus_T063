#include "carbon/validation.hpp"

#include "carbon/errors.hpp"

#include <algorithm>
#include <cctype>

namespace carbon {
namespace {
std::string trimmed(std::string s) {
  auto sp = [](unsigned char c) { return std::isspace(c); };
  s.erase(s.begin(), std::find_if_not(s.begin(), s.end(), sp));
  s.erase(std::find_if_not(s.rbegin(), s.rend(), sp).base(), s.end());
  return s;
}
}  // namespace

BodyReader::BodyReader(const nlohmann::json& object) : obj_(object) {
  if (!object.is_object()) throw badRequest("Request body must be a JSON object");
}

void BodyReader::fail(const std::string& key, const std::string& message) { problems_.push_back({{"field", key}, {"message", message}}); }

std::optional<std::string> BodyReader::readString(const std::string& key, std::size_t minLen, std::size_t maxLen, bool required) {
  seen_.insert(key);
  const auto it = obj_.find(key);
  if (it == obj_.end()) {
    if (required) fail(key, "is required");
    return std::nullopt;
  }
  if (!it->is_string()) {
    fail(key, "must be a string");
    return std::nullopt;
  }
  const std::string value = trimmed(it->get<std::string>());
  if (std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; })) {
    fail(key, "must not contain control characters");
    return std::nullopt;
  }
  if (value.size() < minLen || value.size() > maxLen) {
    fail(key, "length must be between " + std::to_string(minLen) + " and " + std::to_string(maxLen) + " characters");
    return std::nullopt;
  }
  return value;
}

std::string BodyReader::requireString(const std::string& key, std::size_t minLen, std::size_t maxLen) {
  return readString(key, minLen, maxLen, true).value_or("");
}
std::optional<std::string> BodyReader::optionalString(const std::string& key, std::size_t minLen, std::size_t maxLen) {
  return readString(key, minLen, maxLen, false);
}

std::string BodyReader::requireSecret(const std::string& key, std::size_t minLen, std::size_t maxLen) {
  seen_.insert(key);
  const auto it = obj_.find(key);
  if (it == obj_.end()) { fail(key, "is required"); return ""; }
  if (!it->is_string()) { fail(key, "must be a string"); return ""; }
  std::string v = it->get<std::string>();
  if (v.find('\0') != std::string::npos) { fail(key, "contains invalid characters"); return ""; }
  if (v.size() < minLen || v.size() > maxLen) { fail(key, "length must be between " + std::to_string(minLen) + " and " + std::to_string(maxLen) + " characters"); return ""; }
  return v;
}

std::optional<bool> BodyReader::optionalBool(const std::string& key) {
  seen_.insert(key);
  const auto it = obj_.find(key);
  if (it == obj_.end()) return std::nullopt;
  if (!it->is_boolean()) {
    fail(key, "must be a boolean");
    return std::nullopt;
  }
  return it->get<bool>();
}

void BodyReader::finish() {
  for (const auto& [key, value] : obj_.items()) {
    (void)value;
    if (!seen_.count(key)) fail(key, "unknown field");
  }
  if (!problems_.empty()) throw unprocessable("Validation failed", nlohmann::json(problems_));
}

nlohmann::json parseJsonObject(const std::string& body, const std::string& contentType) {
  if (contentType.rfind("application/json", 0) != 0) throw ApiError(415, "unsupported_media_type", "Content-Type must be application/json");
  if (body.empty()) throw badRequest("Request body is required");
  nlohmann::json parsed = nlohmann::json::parse(body, nullptr, false);
  if (parsed.is_discarded()) throw badRequest("Request body is not valid JSON");
  return parsed;
}

bool looksLikeEmail(const std::string& e) {
  if (e.size() < 3 || e.size() > 254) return false;
  const auto at = e.find('@');
  if (at == std::string::npos || at == 0 || at != e.rfind('@')) return false;
  const std::string domain = e.substr(at + 1);
  const auto dot = domain.find('.');
  if (dot == std::string::npos || dot == 0 || domain.back() == '.') return false;
  return std::none_of(e.begin(), e.end(), [](unsigned char c) { return std::isspace(c) || c < 0x20 || c == 0x7f; });
}

}  // namespace carbon
