// Strict JSON body validation: types, lengths, control characters, and UNKNOWN FIELDS are rejected
// (prevents mass assignment, e.g. a client smuggling organization_id into a PATCH).
// All problems are collected and reported together as HTTP 422 with per-field details.
#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace carbon {

class BodyReader {
 public:
  explicit BodyReader(const nlohmann::json& object);  // throws 400 unless `object` is a JSON object

  std::string requireString(const std::string& key, std::size_t minLen, std::size_t maxLen);
  std::optional<std::string> optionalString(const std::string& key, std::size_t minLen, std::size_t maxLen);
  // Secrets (passwords/tokens): NOT trimmed, only NUL is rejected. Never echoed in error messages.
  std::string requireSecret(const std::string& key, std::size_t minLen, std::size_t maxLen);
  std::optional<bool> optionalBool(const std::string& key);
  // Records a custom problem for `key` (e.g. failed pattern check).
  void fail(const std::string& key, const std::string& message);
  bool hasProblems() const noexcept { return !problems_.empty(); }
  // Throws ApiError 422 if anything was wrong or unknown fields were present.
  void finish();

 private:
  std::optional<std::string> readString(const std::string& key, std::size_t minLen, std::size_t maxLen, bool required);
  const nlohmann::json& obj_;
  std::set<std::string> seen_;
  std::vector<nlohmann::json> problems_;
};

// Parses a request body as a JSON object. Throws 415 for a wrong Content-Type, 400 for empty/invalid JSON.
nlohmann::json parseJsonObject(const std::string& body, const std::string& contentType);

bool looksLikeEmail(const std::string& email);

}  // namespace carbon
