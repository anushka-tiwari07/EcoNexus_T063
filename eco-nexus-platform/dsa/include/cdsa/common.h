/* cdsa/common.h - shared types, status codes and allocator hook.
 * Public C11 header, safe to include from C++ (extern "C" guards). */
#ifndef CDSA_COMMON_H
#define CDSA_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#define CDSA_BEGIN_DECLS extern "C" {
#define CDSA_END_DECLS }
#else
#define CDSA_BEGIN_DECLS
#define CDSA_END_DECLS
#endif

CDSA_BEGIN_DECLS

/* "Not found" sentinel for index-returning searches. */
#define CDSA_NPOS ((size_t)-1)

typedef enum {
  CDSA_OK = 0,
  CDSA_ERR_NOMEM,     /* allocation failed or size overflow */
  CDSA_ERR_INVALID,   /* NULL/zero argument that must not be */
  CDSA_ERR_EMPTY,     /* pop/peek on an empty container */
  CDSA_ERR_NOT_FOUND, /* key/element absent */
  CDSA_ERR_RANGE      /* index or vertex out of range */
} cdsa_status;

const char *cdsa_status_str(cdsa_status s);

/* Three-way comparator: <0, 0, >0 (same contract as qsort). */
typedef int (*cdsa_cmp_fn)(const void *a, const void *b);

/* Replaceable allocator. Used for tests (leak counting, failure injection).
 * Not thread-safe to swap while containers are in use. NULL restores libc. */
typedef struct cdsa_allocator {
  void *(*malloc_fn)(size_t);
  void *(*realloc_fn)(void *, size_t);
  void (*free_fn)(void *);
} cdsa_allocator;

cdsa_status cdsa_set_allocator(const cdsa_allocator *a);

void *cdsa_malloc(size_t n);
void *cdsa_calloc(size_t count, size_t size); /* overflow-checked */
void *cdsa_realloc(void *p, size_t n);
void cdsa_free(void *p);

CDSA_END_DECLS
#endif
