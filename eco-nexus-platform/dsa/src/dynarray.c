#include "cdsa/dynarray.h"
#include "internal.h"

struct cdsa_dynarray {
  unsigned char *data;
  size_t size, cap, elem;
};

cdsa_status cdsa_dynarray_create(cdsa_dynarray **out, size_t elem_size, size_t initial_capacity) {
  if (!out || elem_size == 0) return CDSA_ERR_INVALID;
  *out = NULL;
  cdsa_dynarray *a = cdsa_calloc(1, sizeof *a);
  if (!a) return CDSA_ERR_NOMEM;
  a->elem = elem_size;
  if (initial_capacity) {
    cdsa_status s = cdsa_dynarray_reserve(a, initial_capacity);
    if (s != CDSA_OK) { cdsa_free(a); return s; }
  }
  *out = a;
  return CDSA_OK;
}

void cdsa_dynarray_destroy(cdsa_dynarray *a) {
  if (!a) return;
  cdsa_free(a->data);
  cdsa_free(a);
}

cdsa_status cdsa_dynarray_reserve(cdsa_dynarray *a, size_t min_capacity) {
  if (!a) return CDSA_ERR_INVALID;
  if (min_capacity <= a->cap) return CDSA_OK;
  size_t nc = a->cap ? a->cap : 4;
  while (nc < min_capacity) {
    if (nc > SIZE_MAX / 2) { nc = min_capacity; break; }
    nc *= 2;
  }
  if (nc > SIZE_MAX / a->elem) return CDSA_ERR_NOMEM;
  void *p = cdsa_realloc(a->data, nc * a->elem);
  if (!p) return CDSA_ERR_NOMEM; /* old buffer untouched */
  a->data = p;
  a->cap = nc;
  return CDSA_OK;
}

cdsa_status cdsa_dynarray_push(cdsa_dynarray *a, const void *elem) {
  if (!a || !elem) return CDSA_ERR_INVALID;
  if (a->size == SIZE_MAX) return CDSA_ERR_NOMEM;
  cdsa_status s = cdsa_dynarray_reserve(a, a->size + 1);
  if (s != CDSA_OK) return s;
  memcpy(a->data + a->size * a->elem, elem, a->elem);
  a->size++;
  return CDSA_OK;
}

cdsa_status cdsa_dynarray_pop(cdsa_dynarray *a, void *out) {
  if (!a) return CDSA_ERR_INVALID;
  if (a->size == 0) return CDSA_ERR_EMPTY;
  a->size--;
  if (out) memcpy(out, a->data + a->size * a->elem, a->elem);
  return CDSA_OK;
}

cdsa_status cdsa_dynarray_insert(cdsa_dynarray *a, size_t index, const void *elem) {
  if (!a || !elem) return CDSA_ERR_INVALID;
  if (index > a->size) return CDSA_ERR_RANGE;
  if (a->size == SIZE_MAX) return CDSA_ERR_NOMEM;
  cdsa_status s = cdsa_dynarray_reserve(a, a->size + 1);
  if (s != CDSA_OK) return s;
  memmove(a->data + (index + 1) * a->elem, a->data + index * a->elem, (a->size - index) * a->elem);
  memcpy(a->data + index * a->elem, elem, a->elem);
  a->size++;
  return CDSA_OK;
}

cdsa_status cdsa_dynarray_remove_at(cdsa_dynarray *a, size_t index, void *out) {
  if (!a) return CDSA_ERR_INVALID;
  if (index >= a->size) return CDSA_ERR_RANGE;
  if (out) memcpy(out, a->data + index * a->elem, a->elem);
  memmove(a->data + index * a->elem, a->data + (index + 1) * a->elem, (a->size - index - 1) * a->elem);
  a->size--;
  return CDSA_OK;
}

void *cdsa_dynarray_get(const cdsa_dynarray *a, size_t index) {
  if (!a || index >= a->size) return NULL;
  return a->data + index * a->elem;
}
void *cdsa_dynarray_data(const cdsa_dynarray *a) { return a ? a->data : NULL; }
size_t cdsa_dynarray_size(const cdsa_dynarray *a) { return a ? a->size : 0; }
size_t cdsa_dynarray_capacity(const cdsa_dynarray *a) { return a ? a->cap : 0; }
void cdsa_dynarray_clear(cdsa_dynarray *a) { if (a) a->size = 0; }
