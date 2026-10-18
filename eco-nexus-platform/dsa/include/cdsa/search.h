/* cdsa/search.h - searching in arrays of fixed-size elements.
 *   linear_search          O(n)       any array
 *   binary_search          O(log n)   array sorted ascending by cmp; any match
 *   lower_bound/upper_bound O(log n)  first index with elem >= key / elem > key
 * Index-returning functions return CDSA_NPOS when nothing matches. */
#ifndef CDSA_SEARCH_H
#define CDSA_SEARCH_H
#include "cdsa/common.h"
CDSA_BEGIN_DECLS

size_t cdsa_linear_search(const void *base, size_t n, size_t elem_size, const void *key, cdsa_cmp_fn cmp);
size_t cdsa_binary_search(const void *base, size_t n, size_t elem_size, const void *key, cdsa_cmp_fn cmp);
size_t cdsa_lower_bound(const void *base, size_t n, size_t elem_size, const void *key, cdsa_cmp_fn cmp); /* n if none */
size_t cdsa_upper_bound(const void *base, size_t n, size_t elem_size, const void *key, cdsa_cmp_fn cmp); /* n if none */

CDSA_END_DECLS
#endif
