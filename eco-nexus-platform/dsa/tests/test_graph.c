#include "cdsa/cdsa.h"
#include "test_harness.h"

typedef struct { size_t order[64]; size_t n, stop_after; } trace;
static bool record(size_t v, void *user) {
  trace *t = user;
  t->order[t->n++] = v;
  return t->stop_after == 0 || t->n < t->stop_after;
}

static cdsa_graph *sample_graph(bool directed) {
  /* 0-1, 0-2, 1-3, 2-3, 3-4; vertex 5 isolated */
  cdsa_graph *g = NULL;
  cdsa_graph_create(&g, 6, directed);
  const size_t e[][2] = {{0, 1}, {0, 2}, {1, 3}, {2, 3}, {3, 4}};
  for (size_t i = 0; i < 5; i++) cdsa_graph_add_edge(g, e[i][0], e[i][1]);
  return g;
}

static void test_traversals(void) {
  cdsa_graph *g = sample_graph(false);
  trace t = {{0}, 0, 0};
  CHECK_STATUS(cdsa_graph_bfs(g, 0, record, &t), CDSA_OK);
  const size_t bfs_expected[] = {0, 1, 2, 3, 4};
  CHECK_EQ(t.n, 5);
  for (size_t i = 0; i < 5 && i < t.n; i++) CHECK_EQ(t.order[i], bfs_expected[i]);

  t.n = 0;
  CHECK_STATUS(cdsa_graph_dfs(g, 0, record, &t), CDSA_OK);
  const size_t dfs_expected[] = {0, 1, 3, 2, 4};
  CHECK_EQ(t.n, 5);
  for (size_t i = 0; i < 5 && i < t.n; i++) CHECK_EQ(t.order[i], dfs_expected[i]);

  t.n = 0; t.stop_after = 2;   /* early stop */
  cdsa_graph_bfs(g, 0, record, &t); CHECK_EQ(t.n, 2);
  t.n = 0;
  cdsa_graph_dfs(g, 0, record, &t); CHECK_EQ(t.n, 2);

  t.n = 0; t.stop_after = 0;   /* isolated vertex */
  cdsa_graph_bfs(g, 5, record, &t); CHECK_EQ(t.n, 1);
  CHECK_STATUS(cdsa_graph_bfs(g, 6, record, &t), CDSA_ERR_RANGE);
  CHECK_STATUS(cdsa_graph_dfs(g, 6, record, &t), CDSA_ERR_RANGE);
  CHECK_STATUS(cdsa_graph_bfs(g, 0, NULL, &t), CDSA_ERR_INVALID);
  cdsa_graph_destroy(g);
}

static void test_distances_and_directed(void) {
  cdsa_graph *g = sample_graph(false);
  size_t d[6];
  CHECK_STATUS(cdsa_graph_bfs_distances(g, 0, d), CDSA_OK);
  const size_t want[] = {0, 1, 1, 2, 3, CDSA_NPOS};
  for (size_t i = 0; i < 6; i++) CHECK(d[i] == want[i]);
  CHECK_STATUS(cdsa_graph_bfs_distances(g, 9, d), CDSA_ERR_RANGE);
  cdsa_graph_destroy(g);

  cdsa_graph *dg = sample_graph(true);   /* edges only go "forward" */
  CHECK_STATUS(cdsa_graph_bfs_distances(dg, 4, d), CDSA_OK);
  CHECK_EQ(d[4], 0);
  CHECK(d[0] == CDSA_NPOS && d[3] == CDSA_NPOS);
  CHECK_STATUS(cdsa_graph_bfs_distances(dg, 0, d), CDSA_OK);
  CHECK_EQ(d[4], 3);
  CHECK_STATUS(cdsa_graph_add_edge(dg, 0, 99), CDSA_ERR_RANGE);
  CHECK_STATUS(cdsa_graph_add_edge(dg, 3, 3), CDSA_OK); /* self loop is harmless */
  CHECK_STATUS(cdsa_graph_bfs_distances(dg, 0, d), CDSA_OK);
  cdsa_graph_destroy(dg);
}

static bool count_only(size_t v, void *user) { (void)v; (*(size_t *)user)++; return true; }

static void test_edge_cases(void) {
  cdsa_graph *g = NULL;
  CHECK_STATUS(cdsa_graph_create(&g, 0, false), CDSA_OK);     /* empty graph */
  CHECK_EQ(cdsa_graph_vertex_count(g), 0);
  size_t cnt = 0;
  CHECK_STATUS(cdsa_graph_bfs(g, 0, count_only, &cnt), CDSA_ERR_RANGE);
  CHECK_STATUS(cdsa_graph_add_edge(g, 0, 0), CDSA_ERR_RANGE);
  cdsa_graph_destroy(g);
  cdsa_graph_destroy(NULL);

  /* Very deep path graph: iterative DFS must not overflow the C stack. */
  enum { N = 200000 };
  CHECK_STATUS(cdsa_graph_create(&g, N, false), CDSA_OK);
  for (size_t i = 0; i + 1 < N; i++) cdsa_graph_add_edge(g, i, i + 1);
  cnt = 0;
  CHECK_STATUS(cdsa_graph_dfs(g, 0, count_only, &cnt), CDSA_OK);
  CHECK_EQ(cnt, N);
  cnt = 0;
  CHECK_STATUS(cdsa_graph_bfs(g, 0, count_only, &cnt), CDSA_OK);
  CHECK_EQ(cnt, N);
  cdsa_graph_destroy(g);
}

int main(void) {
  RUN(test_traversals); RUN(test_distances_and_directed); RUN(test_edge_cases);
  return test_summary("graph");
}
