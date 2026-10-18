cmake_minimum_required(VERSION 3.25)
project(CarbonPlatform VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CARBON_ENABLE_SANITIZERS "Build with AddressSanitizer + UBSan" OFF)
option(CARBON_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(CARBON_BUILD_TESTS "Build the test suites" ON)

# Project-wide warning/sanitizer flags, applied through an INTERFACE target.
add_library(carbon_options INTERFACE)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(carbon_options INTERFACE -Wall -Wextra -Wpedantic -Wshadow)
  if(CARBON_WARNINGS_AS_ERRORS)
    target_compile_options(carbon_options INTERFACE -Werror)
  endif()
  if(CARBON_ENABLE_SANITIZERS)
    target_compile_options(carbon_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    target_link_options(carbon_options INTERFACE -fsanitize=address,undefined)
  endif()
endif()

if(CARBON_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(dsa)
add_subdirectory(backend)
# Later phases add: frontend build, Docker, etc.
