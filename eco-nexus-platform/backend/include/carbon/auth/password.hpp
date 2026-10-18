// Password hashing (Argon2id via libsodium) behind an abstract interface, plus the password policy.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace carbon::auth {

class PasswordHasher {
 public:
  virtual ~PasswordHasher() = default;
  virtual std::string hash(std::string_view password) const = 0;  // PHC string "$argon2id$..."
  virtual bool verify(std::string_view phc, std::string_view password) const noexcept = 0;
  virtual bool needsRehash(std::string_view phc) const noexcept = 0;
};

class ArgonPasswordHasher final : public PasswordHasher {
 public:
  ArgonPasswordHasher(unsigned long long opsLimit, std::size_t memLimitBytes);
  static ArgonPasswordHasher interactive();  // libsodium INTERACTIVE: 2 passes, 64 MiB
  static ArgonPasswordHasher moderate();     // libsodium MODERATE: 3 passes, 256 MiB
  static ArgonPasswordHasher cheapForTests();  // minimum cost; NEVER for real accounts

  std::string hash(std::string_view password) const override;
  bool verify(std::string_view phc, std::string_view password) const noexcept override;
  bool needsRehash(std::string_view phc) const noexcept override;

 private:
  unsigned long long ops_;
  std::size_t mem_;
};

// Returns an error message, or nullopt if acceptable. NIST-style: length over composition rules.
std::optional<std::string> validatePasswordPolicy(std::string_view password, std::string_view email);

constexpr std::size_t kMinPasswordLength = 12;
constexpr std::size_t kMaxPasswordLength = 128;  // upper bound stops hashing-DoS with huge inputs

}  // namespace carbon::auth
