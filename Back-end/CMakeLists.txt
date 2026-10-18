include(FetchContent)

# Crow is fetched at a pinned release tag (reproducible builds). SYSTEM: its warnings are not ours.
set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CROW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CROW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(Crow
  GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
  GIT_TAG v1.3.3
  GIT_SHALLOW TRUE
  SYSTEM)
FetchContent_MakeAvailable(Crow)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PQXX REQUIRED IMPORTED_TARGET libpqxx)
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
find_package(nlohmann_json 3.2.0 REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

add_library(carbon_core STATIC
  src/errors.cpp src/crypto.cpp src/logging.cpp src/config.cpp src/uuid.cpp src/request_info.cpp src/paging.cpp src/validation.cpp
  src/auth/principal.cpp src/auth/password.cpp src/auth/rate_limiter.cpp src/auth/session_service.cpp src/auth/auth_service.cpp
  src/audit/audit_service.cpp src/domain/organization_service.cpp src/domain/facility_service.cpp
  src/http/api_routes.cpp
  src/db/pool.cpp src/db/migrator.cpp
  src/health/health_service.cpp
  src/http/middleware.cpp src/http/app.cpp)
target_include_directories(carbon_core PUBLIC include)
target_compile_definitions(carbon_core PUBLIC CARBON_VERSION="${PROJECT_VERSION}")
target_link_libraries(carbon_core
  PUBLIC Crow::Crow PkgConfig::PQXX PkgConfig::SODIUM nlohmann_json::nlohmann_json OpenSSL::Crypto Threads::Threads
  PRIVATE carbon_options)

option(CARBON_PQXX_ABI_WORKAROUND "Hide __cpp_lib_source_location from libpqxx headers (distro libpqxx built as C++17)" ON)
if(CARBON_PQXX_ABI_WORKAROUND)
  target_compile_options(carbon_core PUBLIC "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/include/carbon/pqxx_abi_fix.hpp")
endif()

add_executable(carbon_server src/main.cpp)
target_link_libraries(carbon_server PRIVATE carbon_core carbon_options)

add_executable(carbon_migrate src/migrate_main.cpp)
target_link_libraries(carbon_migrate PRIVATE carbon_core carbon_options)

# Development-only account seeding (refuses to run when CARBON_ENV=production).
add_executable(carbon_seed src/seed_main.cpp)
target_link_libraries(carbon_seed PRIVATE carbon_core carbon_options)

if(CARBON_BUILD_TESTS)
  find_package(GTest REQUIRED)
  include(GoogleTest)
  file(GLOB CARBON_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
  add_executable(carbon_backend_tests ${CARBON_TEST_SOURCES})
  target_compile_definitions(carbon_backend_tests PRIVATE CARBON_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(carbon_backend_tests PRIVATE carbon_core carbon_options GTest::gtest GTest::gtest_main)
  gtest_discover_tests(carbon_backend_tests DISCOVERY_TIMEOUT 60 PROPERTIES TIMEOUT 120)
endif()
