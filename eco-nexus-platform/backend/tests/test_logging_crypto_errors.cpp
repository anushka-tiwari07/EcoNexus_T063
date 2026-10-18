#include "carbon/crypto.hpp"
#include "carbon/errors.hpp"
#include "carbon/logging.hpp"

#include <gtest/gtest.h>

#include <set>
#include <sstream>
#include <thread>

using namespace carbon;

TEST(Logging, EmitsOneJsonLinePerEvent) {
  std::ostringstream out;
  Logger log(out, LogLevel::Info);
  log.info("hello", {{"request_id", "abc"}});
  const auto line = nlohmann::json::parse(out.str());
  EXPECT_EQ(line["msg"], "hello");
  EXPECT_EQ(line["level"], "info");
  EXPECT_EQ(line["fields"]["request_id"], "abc");
  EXPECT_TRUE(line["ts"].get<std::string>().ends_with("Z"));
  EXPECT_EQ(out.str().back(), '\n');
}

TEST(Logging, FiltersBelowMinimumLevel) {
  std::ostringstream out;
  Logger log(out, LogLevel::Warn);
  log.debug("d");
  log.info("i");
  log.warn("w");
  log.error("e");
  std::istringstream in(out.str());
  std::string line;
  int n = 0;
  while (std::getline(in, line)) ++n;
  EXPECT_EQ(n, 2);
}

TEST(Logging, RedactsSecretsRecursively) {
  std::ostringstream out;
  Logger log(out, LogLevel::Info);
  log.info("login", {{"email", "a@b.test"}, {"password", "hunter2"},
                     {"nested", {{"Authorization", "Bearer xyz"}, {"session_token", "t"}, {"ok", 1}}},
                     {"list", nlohmann::json::array({{{"api_key", "k"}}})}});
  const std::string text = out.str();
  for (const char* secret : {"hunter2", "Bearer xyz", "\"t\"", "\"k\""}) EXPECT_EQ(text.find(secret), std::string::npos) << secret;
  const auto f = nlohmann::json::parse(text)["fields"];
  EXPECT_EQ(f["password"], "[REDACTED]");
  EXPECT_EQ(f["nested"]["ok"], 1);
  EXPECT_EQ(f["email"], "a@b.test");
}

TEST(Logging, SurvivesInvalidUtf8) {
  std::ostringstream out;
  Logger log(out, LogLevel::Info);
  EXPECT_NO_THROW(log.info("bad", {{"v", std::string("\xff\xfe")}}));
  EXPECT_NO_THROW(nlohmann::json::parse(out.str()));
}

TEST(Logging, ConcurrentWritersNeverInterleaveLines) {
  std::ostringstream out;
  Logger log(out, LogLevel::Info);
  std::vector<std::thread> threads;
  for (int t = 0; t < 8; ++t)
    threads.emplace_back([&, t] { for (int i = 0; i < 200; ++i) log.info("msg", {{"t", t}, {"i", i}}); });
  for (auto& th : threads) th.join();
  std::istringstream in(out.str());
  std::string line;
  int n = 0;
  while (std::getline(in, line)) {
    ASSERT_TRUE(nlohmann::json::accept(line)) << line;
    ++n;
  }
  EXPECT_EQ(n, 1600);
}

TEST(Logging, ParsesLevels) {
  EXPECT_EQ(parseLogLevel("DEBUG"), LogLevel::Debug);
  EXPECT_EQ(parseLogLevel("warning"), LogLevel::Warn);
  EXPECT_FALSE(parseLogLevel("verbose").has_value());
}

TEST(Crypto, Sha256KnownVectors) {
  EXPECT_EQ(sha256Hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(sha256Hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Crypto, RandomHexHasRequestedLengthAndIsUnique) {
  std::set<std::string> seen;
  for (int i = 0; i < 100; ++i) {
    const std::string r = randomHex(16);
    ASSERT_EQ(r.size(), 32u);
    ASSERT_TRUE(seen.insert(r).second);
  }
  EXPECT_EQ(randomHex(0), "");
}

TEST(Errors, ErrorBodyShape) {
  const auto b = makeErrorBody("not_found", "nope", "req-1", {{"field", "x"}});
  EXPECT_EQ(b["error"]["code"], "not_found");
  EXPECT_EQ(b["error"]["request_id"], "req-1");
  EXPECT_EQ(b["error"]["details"]["field"], "x");
  const auto minimal = makeErrorBody("c", "m", "");
  EXPECT_FALSE(minimal["error"].contains("request_id"));
  EXPECT_FALSE(minimal["error"].contains("details"));
}

TEST(Errors, FactoriesCarryStatusAndCode) {
  EXPECT_EQ(notFound().status(), 404);
  EXPECT_EQ(unauthorized().code(), "unauthorized");
  EXPECT_EQ(unprocessable("bad", {{"a", 1}}).details()["a"], 1);
  EXPECT_EQ(internalError().status(), 500);
  EXPECT_EQ(std::string(internalError().what()), "An internal error occurred");
  EXPECT_EQ(defaultErrorCode(405), "method_not_allowed");
  EXPECT_EQ(defaultErrorCode(502), "internal_error");
}
