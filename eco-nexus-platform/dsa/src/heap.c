#include "cdsa/heap.h"
#include "internal.h"

struct cdsa_heap {
  unsigned char *data;
  size_t size, cap, elem;
  cdsa_cmp_fn cmp;
};

cdsa_status cdsa_heap_create(cdsa_heap **out, size_t elem_size, cdsa_cmp_fn cmp) {
  if (!out || elem_size == 0 || !cmp) return CDSA_ERR_INVALID;
  *out = NULL;
  cdsa_heap *h = cdsa_calloc(1, sizeof *h);
  if (!h) return CDSA_ERR_NOMEM;
  h->elem = elem_size;
  h->cmp = cmp;
  *out = h;
  return CDSA_OK;
}

void cdsa_heap_destroy(cdsa_heap *h) {
  if (!h) return;
  cdsa_free(h->data);
  cdsa_free(h);
}

#define H_AT(h, i) CDSA_AT((h)->data, (i), (h)->elem)

static void sift_up(cdsa_heap *h, size_t i) {
  while (i > 0) {
    size_t parent = (i - 1) / 2;
    if (h->cmp(H_AT(h, i), H_AT(h, parent)) >= 0) break;
    cdsa_swap_bytes(H_AT(h, i), H_AT(h, parent), h->elem);
    i = parent;
  }
}

static void sift_down(cdsa_heap *h, size_t i) {
  for (;;) {
    size_t l = 2 * i + 1, r = l + 1, best = i;
    if (l < h->size && h->cmp(H_AT(h, l), H_AT(h, best)) < 0) best = l;
    if (r < h->size && h->cmp(H_AT(h, r), H_AT(h, best)) < 0) best = r;
    if (best == i) return;
    cdsa_swap_bytes(H_AT(h, i), H_AT(h, best), h->elem);
    i = best;
  }
}

cdsa_status cdsa_heap_push(cdsa_heap *h, const void *elem) {
  if (!h || !elem) return CDSA_ERR_INVALID;
  if (h->size == h->cap) {
    size_t nc = h->cap ? h->cap * 2 : 8;
    if (h->cap > SIZE_MAX / 2 || nc > SIZE_MAX / h->elem) return CDSA_ERR_NOMEM;
    void *p = cdsa_realloc(h->data, nc * h->elem);
    if (!p) return CDSA_ERR_NOMEM;
    h->data = p;
    h->cap = nc;
  }
  memcpy(H_AT(h, h->size), elem, h->elem);
  h->size++;
  sift_up(h, h->size - 1);
  return CDSA_OK;
}

cdsa_status cdsa_heap_pop(cdsa_heap *h, void *out) {
  if (!h) return CDSA_ERR_INVALID;
  if (h->size == 0) return CDSA_ERR_EMPTY;
  if (out) memcpy(out, H_AT(h, 0), h->elem);
  h->size--;
  if (h->size > 0) {
    memcpy(H_AT(h, 0), H_AT(h, h->size), h->elem);
    sift_down(h, 0);
  }
  return CDSA_OK;
}

const void *cdsa_heap_peek(const cdsa_heap *h) { return (h && h->size) ? h->data : NULL; }
size_t cdsa_heap_size(const cdsa_heap *h) { return h ? h->size : 0; }
