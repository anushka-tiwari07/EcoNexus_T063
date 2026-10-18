// Force-included (see CMakeLists.txt) before any libpqxx header.
//
// Why: Ubuntu's libpqxx 7.8 is compiled as C++17, but its headers pick exception constructors taking
// std::source_location whenever the *including* TU is C++20 (__cpp_lib_source_location defined).
// Those symbols do not exist in the shared library -> undefined-reference link errors.
// Hiding the feature macro makes the headers match the installed library's ABI while our code stays C++20.
// Remove this (CARBON_PQXX_ABI_WORKAROUND=OFF) if libpqxx is built as C++20 (e.g. 7.9+ or from source).
#pragma once
#include <version>
#ifdef __cpp_lib_source_location
#undef __cpp_lib_source_location
#endif
