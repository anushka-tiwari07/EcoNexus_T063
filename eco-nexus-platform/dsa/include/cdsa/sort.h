/* cdsa/sort.h - in-place sorting of arrays of fixed-size elements.
 * All take (base, n, elem_size, cmp) and return CDSA_OK, or CDSA_ERR_INVALID for
 * NULL base (n>0) / zero elem_size / NULL cmp. n == 0 is always OK.
 *
 *   insertion  O(n^2) time (O(n) if sorted), O(1) extra, STABLE   - small inputs
 *   merge      O(n log n) time, O(n) extra, STABLE                - may return NOMEM
 *   quick      O(n log n) avg, O(n^2) worst (median-of-3 + 3-way partition
 *              makes duplicates and sorted input fine), O(log n) stack, not stable
 *   heap       O(n log n) worst case, O(1) extra, not stable                  */
#ifndef CDSA_SORT_H
#define CDSA_SORT_H
#include "cdsa/common.h"
CDSA_BEGIN_DECLS

cdsa_status cdsa_sort_insertion(void *base, size_t n, size_t elem_size, cdsa_cmp_fn cmp);
cdsa_status cdsa_sort_merge(void *base, size_t n, size_t elem_size, cdsa_cmp_fn cmp);
cdsa_status cdsa_sort_quick(void *base, size_t n, size_t elem_size, cdsa_cmp_fn cmp);
cdsa_status cdsa_sort_heap(void *base, size_t n, size_t elem_size, cdsa_cmp_fn cmp);
bool cdsa_is_sorted(const void *base, size_t n, size_t elem_size, cdsa_cmp_fn cmp);

CDSA_END_DECLS
#endif
