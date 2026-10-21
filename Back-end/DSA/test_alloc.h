/* Counting + failure-injecting allocator for tests. */
#ifndef TEST_ALLOC_H
#define TEST_ALLOC_H
#include <stdlib.h>
#include "cdsa/common.h"

static struct { long live; long attempts; long fail_at; } g_ta; /* fail_at: 1-based attempt to fail, 0 = never */

static int ta_should_fail(void) { g_ta.attempts++; return g_ta.fail_at && g_ta.attempts == g_ta.fail_at; }
static void *ta_malloc(size_t n) { if (ta_should_fail()) return NULL; void *p = malloc(n); if (p) g_ta.live++; return p; }
static void *ta_realloc(void *p, size_t n) {
  if (ta_should_fail()) return NULL;
  void *q = realloc(p, n);
  if (q && !p) g_ta.live++;
  return q;
}
static void ta_free(void *p) { if (p) { g_ta.live--; free(p); } }

static void ta_install(long fail_at) {
  static const cdsa_allocator a = {ta_malloc, ta_realloc, ta_free};
  g_ta.live = 0; g_ta.attempts = 0; g_ta.fail_at = fail_at;
  cdsa_set_allocator(&a);
}
static void ta_uninstall(void) { cdsa_set_allocator(NULL); }
#endif
