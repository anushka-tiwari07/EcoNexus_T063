#include "carbon/crypto.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <memory>
#include <stdexcept>
#include <vector>

namespace carbon {
namespace {
std::string toHex(const unsigned char* data, std::size_t n) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string out(n * 2, '0');
  for (std::size_t i = 0; i < n; ++i) {
    out[2 * i] = kDigits[data[i] >> 4];
    out[2 * i + 1] = kDigits[data[i] & 0x0F];
  }
  return out;
}
}  // namespace

std::string sha256Hex(std::string_view data) {
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int len = 0;
  if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1 ||
      EVP_DigestUpdate(ctx.get(), data.data(), data.size()) != 1 ||
      EVP_DigestFinal_ex(ctx.get(), digest.data(), &len) != 1) {
    throw std::runtime_error("SHA-256 computation failed");
  }
  return toHex(digest.data(), len);
}

std::string randomHex(std::size_t bytes) {
  std::vector<unsigned char> buf(bytes);
  if (bytes > 0 && RAND_bytes(buf.data(), static_cast<int>(bytes)) != 1) throw std::runtime_error("secure random generation failed");
  return toHex(buf.data(), buf.size());
}
}  // namespace carbon
