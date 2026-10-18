#include "cdsa/search.h"
#include "internal.h"

#define A(i) CDSA_AT(base, (i), sz)

size_t cdsa_linear_search(const void *b, size_t n, size_t sz, const void *key, cdsa_cmp_fn cmp) {
  if (!cmp || !key || sz == 0 || (n > 0 && !b)) return CDSA_NPOS;
  const unsigned char *base = b;
  for (size_t i = 0; i < n; i++)
    if (cmp(A(i), key) == 0) return i;
  return CDSA_NPOS;
}

size_t cdsa_lower_bound(const void *b, size_t n, size_t sz, const void *key, cdsa_cmp_fn cmp) {
  if (!cmp || !key || sz == 0 || (n > 0 && !b)) return n;
  const unsigned char *base = b;
  size_t lo = 0, hi = n;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (cmp(A(mid), key) < 0) lo = mid + 1; else hi = mid;
  }
  return lo;
}

size_t cdsa_upper_bound(const void *b, size_t n, size_t sz, const void *key, cdsa_cmp_fn cmp) {
  if (!cmp || !key || sz == 0 || (n > 0 && !b)) return n;
  const unsigned char *base = b;
  size_t lo = 0, hi = n;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (cmp(A(mid), key) <= 0) lo = mid + 1; else hi = mid;
  }
  return lo;
}

size_t cdsa_binary_search(const void *b, size_t n, size_t sz, const void *key, cdsa_cmp_fn cmp) {
  size_t i = cdsa_lower_bound(b, n, sz, key, cmp);
  if (i < n && cmp(CDSA_AT(b, i, sz), key) == 0) return i;
  return CDSA_NPOS;
}
