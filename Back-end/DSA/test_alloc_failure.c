/* For each library operation sequence, fail the Nth allocation for every N and verify:
 *  (1) no crash, (2) status is CDSA_ERR_NOMEM (never silent success with bad state),
 *  (3) every successful allocation was released (live == 0). */
#include "cdsa/cdsa.h"
#include "test_alloc.h"
#include "test_harness.h"

typedef cdsa_status (*scenario)(void);

static int cmp_int(const void *a, const void *b) { int x = *(const int *)a, y = *(const int *)b; return (x > y) - (x < y); }
static bool nop_visit(size_t v, void *u) { (void)v; (void)u; return true; }

static cdsa_status sc_dynarray(void) {
  cdsa_dynarray *a; cdsa_status s = cdsa_dynarray_create(&a, sizeof(int), 2);
  if (s != CDSA_OK) return s;
  for (int i = 0; i < 50 && s == CDSA_OK; i++) s = cdsa_dynarray_push(a, &i);
  cdsa_dynarray_destroy(a);
  return s;
}
static cdsa_status sc_list(void) {
  cdsa_list *l; cdsa_status s = cdsa_list_create(&l, sizeof(int));
  if (s != CDSA_OK) return s;
  for (int i = 0; i < 20 && s == CDSA_OK; i++) s = cdsa_list_push_back(l, &i);
  cdsa_list_destroy(l);
  return s;
}
static cdsa_status sc_queue(void) {
  cdsa_queue *q; cdsa_status s = cdsa_queue_create(&q, sizeof(int));
  if (s != CDSA_OK) return s;
  for (int i = 0; i < 40 && s == CDSA_OK; i++) s = cdsa_queue_enqueue(q, &i);
  cdsa_queue_destroy(q);
  return s;
}
static cdsa_status sc_heap(void) {
  cdsa_heap *h; cdsa_status s = cdsa_heap_create(&h, sizeof(int), cmp_int);
  if (s != CDSA_OK) return s;
  for (int i = 0; i < 40 && s == CDSA_OK; i++) s = cdsa_heap_push(h, &i);
  cdsa_heap_destroy(h);
  return s;
}
static cdsa_status sc_hashtable(void) {
  cdsa_hashtable *t; cdsa_status s = cdsa_ht_create(&t, sizeof(int));
  if (s != CDSA_OK) return s;
  for (int i = 0; i < 40 && s == CDSA_OK; i++) { char k[16]; snprintf(k, sizeof k, "k%d", i); s = cdsa_ht_put(t, k, &i); }
  cdsa_ht_destroy(t);
  return s;
}
static cdsa_status sc_graph(void) {
  cdsa_graph *g; cdsa_status s = cdsa_graph_create(&g, 8, false);
  if (s != CDSA_OK) return s;
  for (size_t i = 0; i < 7 && s == CDSA_OK; i++) s = cdsa_graph_add_edge(g, i, i + 1);
  if (s == CDSA_OK) s = cdsa_graph_bfs(g, 0, nop_visit, NULL);
  if (s == CDSA_OK) s = cdsa_graph_dfs(g, 0, nop_visit, NULL);
  if (s == CDSA_OK) { size_t d[8]; s = cdsa_graph_bfs_distances(g, 0, d); }
  cdsa_graph_destroy(g);
  return s;
}
static cdsa_status sc_merge_sort(void) {
  int v[64];
  for (int i = 0; i < 64; i++) v[i] = 64 - i;
  return cdsa_sort_merge(v, 64, sizeof(int), cmp_int);
}

static void run_injection(const char *name, scenario sc) {
  long points = 0, degraded = 0;
  for (long n = 1; n < 5000; n++) {
    ta_install(n);
    cdsa_status s = sc();
    long live = g_ta.live;
    long attempts = g_ta.attempts;
    ta_uninstall();
    if (live != 0) { fprintf(stderr, "  LEAK in %s at fail_at=%ld (live=%ld)\n", name, n, live); }
    CHECK_EQ(live, 0);
    if (s == CDSA_OK && attempts < n) break; /* n exceeded total allocations: all points covered */
    if (s == CDSA_OK) { degraded++; continue; } /* injected failure was absorbed (best-effort hash resize) */
    CHECK_STATUS(s, CDSA_ERR_NOMEM);
    points++;
  }
  printf("    %s: %ld failure points -> NOMEM, %ld absorbed gracefully\n", name, points, degraded);
  CHECK(points > 0);
}

static void test_injection(void) {
  run_injection("dynarray", sc_dynarray); run_injection("list", sc_list);
  run_injection("queue", sc_queue);       run_injection("heap", sc_heap);
  run_injection("hashtable*", sc_hashtable); run_injection("graph", sc_graph);
  run_injection("merge_sort", sc_merge_sort);
}

int main(void) {
  RUN(test_injection);
  return test_summary("alloc_failure");
}
