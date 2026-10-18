/* Minimal C test harness: CHECK macros, RUN, summary exit code. One TU per test binary. */
#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H
#include <stdio.h>
#include <stdlib.h>

static int g_checks = 0, g_failures = 0;

#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; \
  fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { g_checks++; long long _a = (long long)(a), _b = (long long)(b); \
  if (_a != _b) { g_failures++; fprintf(stderr, "  FAIL %s:%d: %s == %s (%lld vs %lld)\n", \
  __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
#define CHECK_STATUS(expr, want) CHECK_EQ((long long)(expr), (long long)(want))
#define RUN(fn) do { printf("[ RUN ] %s\n", #fn); fn(); } while (0)

static int test_summary(const char *suite) {
  printf("%s: %d checks, %d failures\n", suite, g_checks, g_failures);
  return g_failures ? 1 : 0;
}

/* Deterministic PRNG (LCG) so "random" fixtures are reproducible. */
static unsigned long long g_seed = 12345ULL;
__attribute__((unused)) static unsigned rnd(void) { g_seed = g_seed * 6364136223846793005ULL + 1442695040888963407ULL; return (unsigned)(g_seed >> 33); }
#endif
