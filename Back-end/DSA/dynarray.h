/* cdsa/dynarray.h - growable contiguous array of fixed-size elements (copied in).
 * Complexity: push amortized O(1); pop O(1); get O(1); insert/remove_at O(n). */
#ifndef CDSA_DYNARRAY_H
#define CDSA_DYNARRAY_H
#include "cdsa/common.h"
CDSA_BEGIN_DECLS

typedef struct cdsa_dynarray cdsa_dynarray;

cdsa_status cdsa_dynarray_create(cdsa_dynarray **out, size_t elem_size, size_t initial_capacity);
void cdsa_dynarray_destroy(cdsa_dynarray *a); /* NULL-safe */
cdsa_status cdsa_dynarray_reserve(cdsa_dynarray *a, size_t min_capacity);
cdsa_status cdsa_dynarray_push(cdsa_dynarray *a, const void *elem);
cdsa_status cdsa_dynarray_pop(cdsa_dynarray *a, void *out /* may be NULL */);
cdsa_status cdsa_dynarray_insert(cdsa_dynarray *a, size_t index, const void *elem);
cdsa_status cdsa_dynarray_remove_at(cdsa_dynarray *a, size_t index, void *out /* may be NULL */);
void *cdsa_dynarray_get(const cdsa_dynarray *a, size_t index); /* NULL if out of range */
void *cdsa_dynarray_data(const cdsa_dynarray *a);              /* may be invalidated by push */
size_t cdsa_dynarray_size(const cdsa_dynarray *a);
size_t cdsa_dynarray_capacity(const cdsa_dynarray *a);
void cdsa_dynarray_clear(cdsa_dynarray *a);

CDSA_END_DECLS
#endif
