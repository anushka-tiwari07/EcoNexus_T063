// Proves the C11 public headers compile and link cleanly under C++20 (extern "C"),
// and that a C++ RAII wrapper can own C-library objects safely.
#include <cdsa/cdsa.h>

#include <cstdio>
#include <memory>
#include <vector>

namespace {
int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { ++failures; std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// RAII deleters: the pattern the backend will use around C objects.
struct HeapDeleter { void operator()(cdsa_heap* h) const noexcept { cdsa_heap_destroy(h); } };
struct HashDeleter { void operator()(cdsa_hashtable* h) const noexcept { cdsa_ht_destroy(h); } };
using HeapPtr = std::unique_ptr<cdsa_heap, HeapDeleter>;
using HashPtr = std::unique_ptr<cdsa_hashtable, HashDeleter>;

int cmpInt(const void* a, const void* b) {
  const int x = *static_cast<const int*>(a), y = *static_cast<const int*>(b);
  return (x > y) - (x < y);
}
}  // namespace

int main() {
  cdsa_heap* raw = nullptr;
  EXPECT(cdsa_heap_create(&raw, sizeof(int), cmpInt) == CDSA_OK);
  HeapPtr heap(raw);
  for (int v : {5, 1, 4, 2, 3}) EXPECT(cdsa_heap_push(heap.get(), &v) == CDSA_OK);
  std::vector<int> drained;
  int out = 0;
  while (cdsa_heap_pop(heap.get(), &out) == CDSA_OK) drained.push_back(out);
  EXPECT((drained == std::vector<int>{1, 2, 3, 4, 5}));

  cdsa_hashtable* rawh = nullptr;
  EXPECT(cdsa_ht_create(&rawh, sizeof(double)) == CDSA_OK);
  HashPtr table(rawh);
  const double ef = 2.68;  // illustrative number for a unit test only
  EXPECT(cdsa_ht_put(table.get(), "diesel", &ef) == CDSA_OK);
  const auto* p = static_cast<const double*>(cdsa_ht_get(table.get(), "diesel"));
  EXPECT(p != nullptr && *p == ef);

  std::vector<int> v{9, 3, 7, 1};
  EXPECT(cdsa_sort_merge(v.data(), v.size(), sizeof(int), cmpInt) == CDSA_OK);
  EXPECT((v == std::vector<int>{1, 3, 7, 9}));
  EXPECT(cdsa_binary_search(v.data(), v.size(), sizeof(int), &v[2], cmpInt) == 2);

  std::printf("cpp_interop: %d failures\n", failures);
  return failures ? 1 : 0;
}
