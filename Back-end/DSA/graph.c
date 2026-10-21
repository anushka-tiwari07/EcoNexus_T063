#include "cdsa/graph.h"
#include "cdsa/dynarray.h"
#include "cdsa/queue.h"
#include "internal.h"

struct cdsa_graph {
  size_t n;
  bool directed;
  cdsa_dynarray **adj; /* adj[v] holds size_t neighbour ids in insertion order */
};

cdsa_status cdsa_graph_create(cdsa_graph **out, size_t n, bool directed) {
  if (!out) return CDSA_ERR_INVALID;
  *out = NULL;
  cdsa_graph *g = cdsa_calloc(1, sizeof *g);
  if (!g) return CDSA_ERR_NOMEM;
  g->n = n;
  g->directed = directed;
  if (n > 0) {
    g->adj = cdsa_calloc(n, sizeof(cdsa_dynarray *));
    if (!g->adj) { cdsa_free(g); return CDSA_ERR_NOMEM; }
    for (size_t v = 0; v < n; v++) {
      cdsa_status s = cdsa_dynarray_create(&g->adj[v], sizeof(size_t), 0);
      if (s != CDSA_OK) { cdsa_graph_destroy(g); return s; }
    }
  }
  *out = g;
  return CDSA_OK;
}

void cdsa_graph_destroy(cdsa_graph *g) {
  if (!g) return;
  for (size_t v = 0; v < g->n; v++) cdsa_dynarray_destroy(g->adj ? g->adj[v] : NULL);
  cdsa_free(g->adj);
  cdsa_free(g);
}

size_t cdsa_graph_vertex_count(const cdsa_graph *g) { return g ? g->n : 0; }

cdsa_status cdsa_graph_add_edge(cdsa_graph *g, size_t from, size_t to) {
  if (!g) return CDSA_ERR_INVALID;
  if (from >= g->n || to >= g->n) return CDSA_ERR_RANGE;
  cdsa_status s = cdsa_dynarray_push(g->adj[from], &to);
  if (s != CDSA_OK) return s;
  if (!g->directed && from != to) {
    s = cdsa_dynarray_push(g->adj[to], &from);
    if (s != CDSA_OK) { cdsa_dynarray_pop(g->adj[from], NULL); return s; } /* roll back */
  }
  return CDSA_OK;
}

cdsa_status cdsa_graph_bfs(const cdsa_graph *g, size_t start, cdsa_visit_fn visit, void *user) {
  if (!g || !visit) return CDSA_ERR_INVALID;
  if (start >= g->n) return CDSA_ERR_RANGE;
  unsigned char *seen = cdsa_calloc(g->n, 1);
  cdsa_queue *q = NULL;
  if (!seen) return CDSA_ERR_NOMEM;
  cdsa_status s = cdsa_queue_create(&q, sizeof(size_t));
  if (s != CDSA_OK) { cdsa_free(seen); return s; }

  seen[start] = 1;
  s = cdsa_queue_enqueue(q, &start);
  while (s == CDSA_OK && !cdsa_queue_empty(q)) {
    size_t v;
    cdsa_queue_dequeue(q, &v);
    if (!visit(v, user)) break;
    size_t deg = cdsa_dynarray_size(g->adj[v]);
    for (size_t i = 0; i < deg && s == CDSA_OK; i++) {
      size_t w = *(size_t *)cdsa_dynarray_get(g->adj[v], i);
      if (!seen[w]) { seen[w] = 1; s = cdsa_queue_enqueue(q, &w); }
    }
  }
  cdsa_queue_destroy(q);
  cdsa_free(seen);
  return s;
}

typedef struct { size_t v, next; } dfs_frame;

cdsa_status cdsa_graph_dfs(const cdsa_graph *g, size_t start, cdsa_visit_fn visit, void *user) {
  if (!g || !visit) return CDSA_ERR_INVALID;
  if (start >= g->n) return CDSA_ERR_RANGE;
  unsigned char *seen = cdsa_calloc(g->n, 1);
  cdsa_dynarray *stack = NULL;
  if (!seen) return CDSA_ERR_NOMEM;
  cdsa_status s = cdsa_dynarray_create(&stack, sizeof(dfs_frame), 16);
  if (s != CDSA_OK) { cdsa_free(seen); return s; }

  seen[start] = 1;
  dfs_frame f = {start, 0};
  if (visit(start, user)) s = cdsa_dynarray_push(stack, &f);
  /* Each frame remembers which neighbour to try next => true recursive pre-order. */
  while (s == CDSA_OK && cdsa_dynarray_size(stack) > 0) {
    dfs_frame *top = cdsa_dynarray_get(stack, cdsa_dynarray_size(stack) - 1);
    if (top->next >= cdsa_dynarray_size(g->adj[top->v])) { cdsa_dynarray_pop(stack, NULL); continue; }
    size_t w = *(size_t *)cdsa_dynarray_get(g->adj[top->v], top->next++);
    if (seen[w]) continue;
    seen[w] = 1;
    if (!visit(w, user)) break;
    dfs_frame nf = {w, 0};
    s = cdsa_dynarray_push(stack, &nf); /* may realloc: 'top' not used after this */
  }
  cdsa_dynarray_destroy(stack);
  cdsa_free(seen);
  return s;
}


cdsa_status cdsa_graph_bfs_distances(const cdsa_graph *g, size_t start, size_t *dist) {
  if (!g || !dist) return CDSA_ERR_INVALID;
  if (start >= g->n) return CDSA_ERR_RANGE;
  for (size_t v = 0; v < g->n; v++) dist[v] = CDSA_NPOS;
  cdsa_queue *q = NULL;
  cdsa_status s = cdsa_queue_create(&q, sizeof(size_t));
  if (s != CDSA_OK) return s;
  dist[start] = 0;
  s = cdsa_queue_enqueue(q, &start);
  while (s == CDSA_OK && !cdsa_queue_empty(q)) {
    size_t v;
    cdsa_queue_dequeue(q, &v);
    size_t deg = cdsa_dynarray_size(g->adj[v]);
    for (size_t i = 0; i < deg && s == CDSA_OK; i++) {
      size_t w = *(size_t *)cdsa_dynarray_get(g->adj[v], i);
      if (dist[w] == CDSA_NPOS) { dist[w] = dist[v] + 1; s = cdsa_queue_enqueue(q, &w); }
    }
  }
  cdsa_queue_destroy(q);
  return s;
}
