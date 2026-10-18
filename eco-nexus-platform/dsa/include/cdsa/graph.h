/* cdsa/graph.h - adjacency-list graph over vertices 0..n-1 with BFS and DFS.
 * Neighbours are visited in edge-insertion order, so traversal order is deterministic.
 *   add_edge    amortized O(1)
 *   bfs / dfs   O(V + E) time, O(V) extra. DFS is iterative (no recursion), so deep
 *               graphs cannot overflow the call stack.
 *   bfs_distances: hop count from start; CDSA_NPOS for unreachable vertices. */
#ifndef CDSA_GRAPH_H
#define CDSA_GRAPH_H
#include "cdsa/common.h"
CDSA_BEGIN_DECLS

typedef struct cdsa_graph cdsa_graph;
typedef bool (*cdsa_visit_fn)(size_t vertex, void *user); /* return false to stop traversal */

cdsa_status cdsa_graph_create(cdsa_graph **out, size_t vertex_count, bool directed);
void cdsa_graph_destroy(cdsa_graph *g); /* NULL-safe */
cdsa_status cdsa_graph_add_edge(cdsa_graph *g, size_t from, size_t to);
size_t cdsa_graph_vertex_count(const cdsa_graph *g);
cdsa_status cdsa_graph_bfs(const cdsa_graph *g, size_t start, cdsa_visit_fn visit, void *user);
cdsa_status cdsa_graph_dfs(const cdsa_graph *g, size_t start, cdsa_visit_fn visit, void *user);
cdsa_status cdsa_graph_bfs_distances(const cdsa_graph *g, size_t start, size_t *dist_out /* len = vertex_count */);

CDSA_END_DECLS
#endif
