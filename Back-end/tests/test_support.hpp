// Shared test helpers: throw-away PostgreSQL databases and a tiny raw-socket HTTP client.
#pragma once

#include "carbon/crypto.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace carbon::tsup {

// DB-backed tests need CARBON_TEST_ADMIN_URL: a connection URL for a role with CREATEDB, e.g.
//   postgresql://carbon_dev:carbon_dev_pw@localhost:5432/postgres   (development credentials only)
inline std::optional<std::string> adminUrl() {
  const char* v = std::getenv("CARBON_TEST_ADMIN_URL");
  if (!v || !*v) return std::nullopt;
  return std::string(v);
}
#define REQUIRE_TEST_DB() \
  if (!::carbon::tsup::adminUrl()) GTEST_SKIP() << "CARBON_TEST_ADMIN_URL not set; skipping database test"

inline std::string urlWithDatabase(const std::string& url, const std::string& db) {
  const auto scheme = url.find("://");
  const auto pathStart = url.find('/', scheme + 3);
  const auto query = url.find('?');
  const std::string base = pathStart == std::string::npos ? url.substr(0, query) : url.substr(0, pathStart);
  return base + "/" + db + (query == std::string::npos ? "" : url.substr(query));
}

inline std::filesystem::path migrationsDir() { return std::filesystem::path(CARBON_SOURCE_DIR) / "db" / "migrations"; }

// Creates a uniquely named database and drops it on destruction.
class TempDatabase {
 public:
  TempDatabase() : name_("carbon_test_" + randomHex(6)) {
    pqxx::connection admin(*adminUrl());
    pqxx::nontransaction tx(admin);
    tx.exec("CREATE DATABASE " + name_);
    url_ = urlWithDatabase(*adminUrl(), name_);
  }
  ~TempDatabase() {
    try {
      pqxx::connection admin(*adminUrl());
      pqxx::nontransaction tx(admin);
      tx.exec("DROP DATABASE IF EXISTS " + name_ + " WITH (FORCE)");
    } catch (...) {
    }
  }
  TempDatabase(const TempDatabase&) = delete;
  TempDatabase& operator=(const TempDatabase&) = delete;
  const std::string& url() const { return url_; }

 private:
  std::string name_, url_;
};

// ---- minimal HTTP/1.1 client (Connection: close) ----
struct HttpResponse {
  int status = 0;
  std::vector<std::pair<std::string, std::string>> headers;  // names lower-cased
  std::string body;

  std::optional<std::string> header(std::string name) const {
    for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const auto& [k, v] : headers)
      if (k == name) return v;
    return std::nullopt;
  }
  nlohmann::json json() const { return nlohmann::json::parse(body); }
};

inline std::uint16_t freePort() {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
  socklen_t len = sizeof addr;
  ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
  const std::uint16_t port = ntohs(addr.sin_port);
  ::close(fd);
  return port;
}

inline HttpResponse httpRequest(std::uint16_t port, const std::string& method, const std::string& path,
                                const std::map<std::string, std::string>& headers = {}, const std::string& body = "") {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
    ::close(fd);
    throw std::runtime_error("connect failed");
  }
  timeval tv{5, 0};
  ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

  std::ostringstream req;
  req << method << ' ' << path << " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n";
  for (const auto& [k, v] : headers) req << k << ": " << v << "\r\n";
  if (!body.empty() || method == "POST") req << "Content-Length: " << body.size() << "\r\n";
  req << "\r\n" << body;
  const std::string out = req.str();
  std::size_t sent = 0;
  while (sent < out.size()) {
    const ssize_t n = ::send(fd, out.data() + sent, out.size() - sent, MSG_NOSIGNAL);
    if (n <= 0) break;  // server may close early (e.g. 413) - still try to read its answer
    sent += static_cast<std::size_t>(n);
  }

  std::string raw;
  char buf[4096];
  for (;;) {
    const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
    if (n <= 0) break;
    raw.append(buf, static_cast<std::size_t>(n));
  }
  ::close(fd);

  HttpResponse res;
  const auto headEnd = raw.find("\r\n\r\n");
  if (headEnd == std::string::npos) throw std::runtime_error("malformed HTTP response: '" + raw + "'");
  std::istringstream head(raw.substr(0, headEnd));
  std::string line;
  std::getline(head, line);
  res.status = std::stoi(line.substr(line.find(' ') + 1, 3));
  while (std::getline(head, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string name = line.substr(0, colon), value = line.substr(colon + 1);
    for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    value.erase(0, value.find_first_not_of(' '));
    res.headers.emplace_back(std::move(name), std::move(value));
  }
  res.body = raw.substr(headEnd + 4);
  return res;
}

}  // namespace carbon::tsup
