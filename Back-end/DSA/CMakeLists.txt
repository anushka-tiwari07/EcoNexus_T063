# C11 data-structures-and-algorithms library (static).
add_library(cdsa STATIC
  src/common.c src/dynarray.c src/list.c src/hashtable.c src/heap.c
  src/queue.c src/sort.c src/search.c src/graph.c)
target_include_directories(cdsa PUBLIC  include)
target_include_directories(cdsa PRIVATE src)
target_link_libraries(cdsa PRIVATE carbon_options)
set_target_properties(cdsa PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

if(CARBON_BUILD_TESTS)
  foreach(t containers algorithms graph alloc_failure)
    add_executable(test_${t} tests/test_${t}.c)
    target_link_libraries(test_${t} PRIVATE cdsa carbon_options)
    add_test(NAME dsa.${t} COMMAND test_${t})
  endforeach()
  # Proves the public C header is consumable from C++20 (the real consumer).
  add_executable(test_cpp_interop tests/test_cpp_interop.cpp)
  target_link_libraries(test_cpp_interop PRIVATE cdsa carbon_options)
  add_test(NAME dsa.cpp_interop COMMAND test_cpp_interop)
endif()
