#include "carbon/http/app.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

// docs/openapi.yaml must describe exactly the implemented operations. kApiRoutes is itself verified
// against the running server (test_org_api.cpp: every declared route exists and is protected as expected).
TEST(OpenApi, OperationsMatchImplementedRoutes) {
  std::ifstream in(std::filesystem::path(CARBON_SOURCE_DIR) / "docs" / "openapi.yaml");
  ASSERT_TRUE(in.good());
  std::set<std::string> documented;
  std::string line, path;
  bool inPaths = false;
  while (std::getline(in, line)) {
    if (line == "paths:") { inPaths = true; continue; }
    if (line == "components:") break;
    if (!inPaths) continue;
    if (line.rfind("  /api/", 0) == 0) { path = line.substr(2, line.find(':') - 2); continue; }
    for (const char* m : {"get", "post", "patch", "put", "delete"})
      if (line == std::string("    ") + m + ":") {
        std::string upper = m;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        documented.insert(upper + " " + path);
      }
  }
  std::set<std::string> implemented;
  for (const auto& r : carbon::http::kApiRoutes) implemented.insert(std::string(r.method) + " " + std::string(r.path));
  EXPECT_EQ(documented, implemented);
  EXPECT_EQ(implemented.size(), carbon::http::kApiRoutes.size());  // no duplicates in the table
}
