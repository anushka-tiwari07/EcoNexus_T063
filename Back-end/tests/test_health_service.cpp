#include "carbon/health/health_service.hpp"

#include <gtest/gtest.h>

#include <sstream>

using namespace carbon;
using namespace carbon::health;

namespace {
// Test double demonstrating the polymorphic HealthProbe interface.
class FakeProbe final : public HealthProbe {
 public:
  FakeProbe(std::string n, bool ok, bool throws = false) : name_(std::move(n)), ok_(ok), throws_(throws) {}
  std::string name() const override { return name_; }
  ProbeResult check() override {
    if (throws_) throw std::runtime_error("connection string postgresql://u:SECRET@h/db refused");
    return {ok_, {{"detail", "x"}}};
  }

 private:
  std::string name_;
  bool ok_, throws_;
};
}  // namespace

TEST(HealthService, LiveNeedsNoDependencies) {
  std::ostringstream logs;
  Logger logger(logs, LogLevel::Info);
  HealthService svc("1.2.3", logger);
  svc.addProbe(std::make_unique<FakeProbe>("db", false));  // failing probe must not affect liveness
  const auto j = svc.live();
  EXPECT_EQ(j["status"], "ok");
  EXPECT_EQ(j["version"], "1.2.3");
  EXPECT_GE(j["uptime_seconds"].get<long long>(), 0);
}

TEST(HealthService, ReadyOnlyWhenAllProbesPass) {
  std::ostringstream logs;
  Logger logger(logs, LogLevel::Info);
  HealthService svc("v", logger);
  svc.addProbe(std::make_unique<FakeProbe>("a", true));
  svc.addProbe(std::make_unique<FakeProbe>("b", true));
  auto r = svc.ready();
  EXPECT_TRUE(r.ready);
  EXPECT_EQ(r.body["status"], "ready");
  EXPECT_EQ(r.body["checks"]["a"]["status"], "ok");

  svc.addProbe(std::make_unique<FakeProbe>("c", false));
  r = svc.ready();
  EXPECT_FALSE(r.ready);
  EXPECT_EQ(r.body["status"], "not_ready");
  EXPECT_EQ(r.body["checks"]["c"]["status"], "fail");
  EXPECT_EQ(r.body["checks"]["a"]["status"], "ok");
}

TEST(HealthService, ThrowingProbeIsContainedAndNotLeaked) {
  std::ostringstream logs;
  Logger logger(logs, LogLevel::Info);
  HealthService svc("v", logger);
  svc.addProbe(std::make_unique<FakeProbe>("db", true, true));
  const auto r = svc.ready();
  EXPECT_FALSE(r.ready);
  EXPECT_EQ(r.body["checks"]["db"]["error"], "check failed");
  EXPECT_EQ(r.body.dump().find("SECRET"), std::string::npos);  // nothing internal in the response
  EXPECT_NE(logs.str().find("health probe failed"), std::string::npos);
}

TEST(HealthService, NoProbesMeansReady) {
  std::ostringstream logs;
  Logger logger(logs, LogLevel::Info);
  HealthService svc("v", logger);
  EXPECT_TRUE(svc.ready().ready);
}
