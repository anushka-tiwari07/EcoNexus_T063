#include "carbon/auth/password.hpp"

#include <sodium.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <mutex>
#include <stdexcept>

namespace carbon::auth {
namespace {
void ensureSodium() {
  static std::once_flag flag;
  std::call_once(flag, [] {
    if (sodium_init() < 0) throw std::runtime_error("libsodium initialisation failed");
  });
}

std::string lowered(std::string_view s) {
  std::string o(s);
  std::transform(o.begin(), o.end(), o.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return o;
}
}  // namespace

ArgonPasswordHasher::ArgonPasswordHasher(unsigned long long ops, std::size_t mem) : ops_(ops), mem_(mem) { ensureSodium(); }
ArgonPasswordHasher ArgonPasswordHasher::interactive() { return {crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE}; }
ArgonPasswordHasher ArgonPasswordHasher::moderate() { return {crypto_pwhash_OPSLIMIT_MODERATE, crypto_pwhash_MEMLIMIT_MODERATE}; }
ArgonPasswordHasher ArgonPasswordHasher::cheapForTests() { return {crypto_pwhash_OPSLIMIT_MIN, crypto_pwhash_MEMLIMIT_MIN}; }

std::string ArgonPasswordHasher::hash(std::string_view password) const {
  std::array<char, crypto_pwhash_STRBYTES> out{};
  if (crypto_pwhash_str_alg(out.data(), password.data(), password.size(), ops_, mem_, crypto_pwhash_ALG_ARGON2ID13) != 0)
    throw std::runtime_error("password hashing failed (out of memory?)");
  return std::string(out.data());
}

bool ArgonPasswordHasher::verify(std::string_view phc, std::string_view password) const noexcept {
  if (phc.empty() || phc.size() >= crypto_pwhash_STRBYTES) return false;
  std::array<char, crypto_pwhash_STRBYTES> buf{};
  std::copy(phc.begin(), phc.end(), buf.begin());  // NUL-terminated copy required by libsodium
  return crypto_pwhash_str_verify(buf.data(), password.data(), password.size()) == 0;
}

bool ArgonPasswordHasher::needsRehash(std::string_view phc) const noexcept {
  if (phc.empty() || phc.size() >= crypto_pwhash_STRBYTES) return true;
  std::array<char, crypto_pwhash_STRBYTES> buf{};
  std::copy(phc.begin(), phc.end(), buf.begin());
  return crypto_pwhash_str_needs_rehash(buf.data(), ops_, mem_) != 0;  // 1 = weaker params, -1 = invalid
}

std::optional<std::string> validatePasswordPolicy(std::string_view password, std::string_view email) {
  if (password.size() < kMinPasswordLength) return "Password must be at least 12 characters long";
  if (password.size() > kMaxPasswordLength) return "Password must be at most 128 characters long";
  if (std::all_of(password.begin(), password.end(), [](unsigned char c) { return std::isspace(c); })) return "Password must not be only whitespace";
  const std::string p = lowered(password);
  const std::string e = lowered(email);
  if (!e.empty() && (p == e || p.find(e.substr(0, e.find('@'))) != std::string::npos) && e.find('@') != std::string::npos && e.find('@') >= 4)
    return "Password must not contain your email name";
  static const std::array<std::string_view, 8> kCommon = {"password1234", "123456789012", "qwertyuiop12", "iloveyou1234",
                                                          "administrator", "letmein12345", "welcome12345", "changeme1234"};
  for (const auto c : kCommon)
    if (p.find(c) != std::string::npos) return "Password is too common";
  if (std::all_of(p.begin(), p.end(), [&](char c) { return c == p.front(); })) return "Password must not be a single repeated character";
  return std::nullopt;
}

}  // namespace carbon::auth
