#include <string.h>
#include "cdsa/cdsa.h"
#include "test_harness.h"

static int cmp_int(const void *a, const void *b) { int x = *(const int *)a, y = *(const int *)b; return (x > y) - (x < y); }

typedef cdsa_status (*sort_fn)(void *, size_t, size_t, cdsa_cmp_fn);
typedef struct { const char *name; sort_fn fn; bool stable; } algo;
static const algo ALGOS[] = {
  {"insertion", cdsa_sort_insertion, true}, {"merge", cdsa_sort_merge, true},
  {"quick", cdsa_sort_quick, false},        {"heap", cdsa_sort_heap, false},
};
#define NALGOS (sizeof ALGOS / sizeof ALGOS[0])

static void check_against_qsort(const algo *al, const int *src, size_t n, const char *label) {
  int *mine = malloc((n ? n : 1) * sizeof(int)), *ref = malloc((n ? n : 1) * sizeof(int));
  if (n) { memcpy(mine, src, n * sizeof(int)); memcpy(ref, src, n * sizeof(int)); }
  CHECK_STATUS(al->fn(mine, n, sizeof(int), cmp_int), CDSA_OK);
  qsort(ref, n, sizeof(int), cmp_int);
  if (n && memcmp(mine, ref, n * sizeof(int)) != 0) { g_failures++; fprintf(stderr, "  FAIL %s on %s (n=%zu)\n", al->name, label, n); }
  g_checks++;
  CHECK(cdsa_is_sorted(mine, n, sizeof(int), cmp_int));
  free(mine); free(ref);
}

static void test_sorts_correctness(void) {
  for (size_t a = 0; a < NALGOS; a++) {
    const algo *al = &ALGOS[a];
    int empty[1] = {0};
    check_against_qsort(al, empty, 0, "empty");
    int one[] = {42}; check_against_qsort(al, one, 1, "single");
    int two[] = {2, 1}; check_against_qsort(al, two, 2, "two");
    int neg[] = {5, -3, 0, -3, 9, 5, INT32_MIN, INT32_MAX}; check_against_qsort(al, neg, 8, "negatives+extremes");
    enum { N = 2000 };
    static int buf[N];
    for (int i = 0; i < N; i++) buf[i] = i;
    check_against_qsort(al, buf, N, "already sorted");
    for (int i = 0; i < N; i++) buf[i] = N - i;
    check_against_qsort(al, buf, N, "reverse");
    for (int i = 0; i < N; i++) buf[i] = 7;
    check_against_qsort(al, buf, N, "all equal");
    for (int i = 0; i < N; i++) buf[i] = (int)(rnd() % 10);
    check_against_qsort(al, buf, N, "many duplicates");
    for (int i = 0; i < N; i++) buf[i] = (int)rnd() - (1 << 30);
    check_against_qsort(al, buf, N, "random");
    for (int len = 0; len < 40; len++) { /* every size around the quicksort cutoff */
      for (int i = 0; i < len; i++) buf[i] = (int)(rnd() % 50);
      check_against_qsort(al, buf, (size_t)len, "small random");
    }
  }
}

typedef struct { int key; int seq; } rec;
static int cmp_rec_key(const void *a, const void *b) { return cmp_int(&((const rec *)a)->key, &((const rec *)b)->key); }

static void test_sort_stability_and_struct(void) {
  for (size_t a = 0; a < NALGOS; a++) {
    enum { N = 1500 };
    static rec r[N];
    for (int i = 0; i < N; i++) { r[i].key = (int)(rnd() % 20); r[i].seq = i; }
    CHECK_STATUS(ALGOS[a].fn(r, N, sizeof(rec), cmp_rec_key), CDSA_OK);
    CHECK(cdsa_is_sorted(r, N, sizeof(rec), cmp_rec_key));
    if (ALGOS[a].stable) {
      int violations = 0;
      for (int i = 1; i < N; i++) if (r[i - 1].key == r[i].key && r[i - 1].seq > r[i].seq) violations++;
      if (violations) fprintf(stderr, "  %s not stable\n", ALGOS[a].name);
      CHECK_EQ(violations, 0);
    }
  }
}

static void test_sort_invalid_args(void) {
  int x[2] = {2, 1};
  for (size_t a = 0; a < NALGOS; a++) {
    CHECK_STATUS(ALGOS[a].fn(NULL, 2, sizeof(int), cmp_int), CDSA_ERR_INVALID);
    CHECK_STATUS(ALGOS[a].fn(x, 2, 0, cmp_int), CDSA_ERR_INVALID);
    CHECK_STATUS(ALGOS[a].fn(x, 2, sizeof(int), NULL), CDSA_ERR_INVALID);
    CHECK_STATUS(ALGOS[a].fn(NULL, 0, sizeof(int), cmp_int), CDSA_OK);
  }
  CHECK(!cdsa_is_sorted(x, 2, sizeof(int), cmp_int));
}

static void test_quicksort_no_quadratic_on_equal(void) {
  /* 200k equal keys would take ~minutes with a naive 2-way Lomuto partition. */
  enum { N = 200000 };
  int *a = malloc(N * sizeof(int));
  for (int i = 0; i < N; i++) a[i] = 1;
  CHECK_STATUS(cdsa_sort_quick(a, N, sizeof(int), cmp_int), CDSA_OK);
  for (int i = 0; i < N; i++) a[i] = i;           /* sorted input */
  CHECK_STATUS(cdsa_sort_quick(a, N, sizeof(int), cmp_int), CDSA_OK);
  CHECK(cdsa_is_sorted(a, N, sizeof(int), cmp_int));
  free(a);
}

static void test_searching(void) {
  int a[] = {1, 3, 3, 3, 5, 8, 13};
  size_t n = sizeof a / sizeof a[0];
  int k;
  k = 8;  CHECK_EQ(cdsa_binary_search(a, n, sizeof(int), &k, cmp_int), 5);
  k = 1;  CHECK_EQ(cdsa_binary_search(a, n, sizeof(int), &k, cmp_int), 0);
  k = 13; CHECK_EQ(cdsa_binary_search(a, n, sizeof(int), &k, cmp_int), 6);
  k = 4;  CHECK(cdsa_binary_search(a, n, sizeof(int), &k, cmp_int) == CDSA_NPOS);
  k = 0;  CHECK(cdsa_binary_search(a, n, sizeof(int), &k, cmp_int) == CDSA_NPOS);
  k = 99; CHECK(cdsa_binary_search(a, n, sizeof(int), &k, cmp_int) == CDSA_NPOS);
  k = 3;
  CHECK_EQ(cdsa_lower_bound(a, n, sizeof(int), &k, cmp_int), 1);
  CHECK_EQ(cdsa_upper_bound(a, n, sizeof(int), &k, cmp_int), 4);
  k = 4;
  CHECK_EQ(cdsa_lower_bound(a, n, sizeof(int), &k, cmp_int), 4);
  CHECK_EQ(cdsa_upper_bound(a, n, sizeof(int), &k, cmp_int), 4);
  k = 100; CHECK_EQ(cdsa_lower_bound(a, n, sizeof(int), &k, cmp_int), n);
  k = 3;   CHECK_EQ(cdsa_linear_search(a, n, sizeof(int), &k, cmp_int), 1);
  k = 77;  CHECK(cdsa_linear_search(a, n, sizeof(int), &k, cmp_int) == CDSA_NPOS);
  /* empty / invalid */
  CHECK(cdsa_binary_search(NULL, 0, sizeof(int), &k, cmp_int) == CDSA_NPOS);
  CHECK(cdsa_linear_search(NULL, 0, sizeof(int), &k, cmp_int) == CDSA_NPOS);
  CHECK(cdsa_binary_search(a, n, sizeof(int), NULL, cmp_int) == CDSA_NPOS);
  /* binary search agrees with linear search on random sorted data */
  static int big[3000];
  for (int i = 0; i < 3000; i++) big[i] = (int)(rnd() % 4000);
  cdsa_sort_merge(big, 3000, sizeof(int), cmp_int);
  int disagreements = 0;
  for (int probe = 0; probe < 4100; probe++) {
    size_t b = cdsa_binary_search(big, 3000, sizeof(int), &probe, cmp_int);
    size_t l = cdsa_linear_search(big, 3000, sizeof(int), &probe, cmp_int);
    if ((b == CDSA_NPOS) != (l == CDSA_NPOS) || (b != CDSA_NPOS && big[b] != probe)) disagreements++;
  }
  CHECK_EQ(disagreements, 0);
}

int main(void) {
  RUN(test_sorts_correctness); RUN(test_sort_stability_and_struct); RUN(test_sort_invalid_args);
  RUN(test_quicksort_no_quadratic_on_equal); RUN(test_searching);
  return test_summary("algorithms");
}
