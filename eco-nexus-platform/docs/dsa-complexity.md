# C11 DSA library (`dsa/`) - complexity and usage

| Module | Structure / algorithm | Time | Extra space | Notes |
|---|---|---|---|---|
| dynarray | growable array | push amortized O(1), get O(1), insert/remove_at O(n) | O(n) | doubling growth, overflow-checked |
| list | doubly linked list | push/pop ends O(1), at/find O(n) | O(n) | `at` walks from nearer end |
| hashtable | chained, FNV-1a 64 | avg O(1), worst O(n) | O(n) | resize at load 0.75; resize is best-effort (failure keeps a valid table) |
| heap | binary heap | push/pop O(log n), peek O(1) | O(n) | comparator decides min/max; not stable |
| queue | ring buffer | enqueue amortized O(1), dequeue O(1) | O(n) | |
| sort | insertion | O(n^2), O(n) if sorted | O(1) | stable |
| sort | merge | O(n log n) | O(n/2) | stable; can return NOMEM |
| sort | quick | O(n log n) avg, O(n^2) worst | O(log n) stack | median-of-3, 3-way partition, recurse on smaller side |
| sort | heap | O(n log n) worst | O(1) | |
| search | linear / binary / lower & upper bound | O(n) / O(log n) | O(1) | binary requires sorted input |
| graph | adjacency list, BFS, DFS, BFS distances | O(V+E) | O(V) | DFS is iterative (no stack overflow) |

## Memory safety
- All allocation goes through `cdsa_malloc/realloc/free`, replaceable via `cdsa_set_allocator`.
- Tests inject a failure at every allocation point (`test_alloc_failure`) and assert no leak and a `CDSA_ERR_NOMEM` result.
- Whole suite also runs under AddressSanitizer + UBSan (`./scripts/test.sh`).

## Where the library will (and will not) be used  *(PLANNED - not yet implemented)*
Planned: ranking/prioritisation of review queues (heap), emission-factor lookup caches (hashtable),
data-quality sorting/search, and the *isolated in-memory order-book simulation used by tests*.
Deliberately NOT used for authoritative financial state: balances, ownership and settlement stay in
PostgreSQL transactions with C++ strong types.
