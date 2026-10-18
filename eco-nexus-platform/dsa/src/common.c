#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "cdsa/common.h"

static cdsa_allocator g_alloc = {malloc, realloc, free};

const char *cdsa_status_str(cdsa_status s) {
  switch (s) {
    case CDSA_OK: return "ok";
    case CDSA_ERR_NOMEM: return "out of memory";
    case CDSA_ERR_INVALID: return "invalid argument";
    case CDSA_ERR_EMPTY: return "container empty";
    case CDSA_ERR_NOT_FOUND: return "not found";
    case CDSA_ERR_RANGE: return "index out of range";
  }
  return "unknown status";
}

cdsa_status cdsa_set_allocator(const cdsa_allocator *a) {
  if (!a) {
    g_alloc = (cdsa_allocator){malloc, realloc, free};
    return CDSA_OK;
  }
  if (!a->malloc_fn || !a->realloc_fn || !a->free_fn) return CDSA_ERR_INVALID;
  g_alloc = *a;
  return CDSA_OK;
}

void *cdsa_malloc(size_t n) { return g_alloc.malloc_fn(n ? n : 1); }

void *cdsa_calloc(size_t count, size_t size) {
  if (size != 0 && count > SIZE_MAX / size) return NULL;
  size_t total = count * size;
  void *p = cdsa_malloc(total);
  if (p) memset(p, 0, total ? total : 1);
  return p;
}

void *cdsa_realloc(void *p, size_t n) { return g_alloc.realloc_fn(p, n ? n : 1); }
void cdsa_free(void *p) { if (p) g_alloc.free_fn(p); }
