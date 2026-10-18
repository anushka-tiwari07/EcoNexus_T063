/* cdsa/heap.h - binary heap / priority queue.
 * cmp(a,b) < 0 means a has HIGHER priority (comes out first): pass an ascending
 * comparator for a min-heap, a reversed one for a max-heap.
 * Complexity: push O(log n); pop O(log n); peek O(1). Not stable for equal priorities. */
#ifndef CDSA_HEAP_H
#define CDSA_HEAP_H
#include "cdsa/common.h"
CDSA_BEGIN_DECLS

typedef struct cdsa_heap cdsa_heap;

cdsa_status cdsa_heap_create(cdsa_heap **out, size_t elem_size, cdsa_cmp_fn cmp);
void cdsa_heap_destroy(cdsa_heap *h); /* NULL-safe */
cdsa_status cdsa_heap_push(cdsa_heap *h, const void *elem);
cdsa_status cdsa_heap_pop(cdsa_heap *h, void *out /* may be NULL */);
const void *cdsa_heap_peek(const cdsa_heap *h); /* NULL if empty */
size_t cdsa_heap_size(const cdsa_heap *h);

CDSA_END_DECLS
#endif
