// Small OpenSSL-backed helpers: SHA-256 (migration checksums, file hashes) and secure random hex.
#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace carbon {
std::string sha256Hex(std::string_view data);
std::string randomHex(std::size_t bytes);  // cryptographically secure; throws std::runtime_error on failure
}  // namespace carbon
