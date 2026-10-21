#include "cdsa/sort.h"
#include "internal.h"

static cdsa_status check(const void *base, size_t n, size_t sz, cdsa_cmp_fn cmp) {
  if (sz == 0 || !cmp || (n > 0 && !base)) return CDSA_ERR_INVALID;
  return CDSA_OK;
}

#define A(i) CDSA_AT(base, (i), sz)

/* ---- insertion sort on [lo, hi): adjacent swaps, no allocation, stable ---- */
static void insertion_range(unsigned char *base, size_t lo, size_t hi, size_t sz, cdsa_cmp_fn cmp) {
  for (size_t i = lo + 1; i < hi; i++)
    for (size_t j = i; j > lo && cmp(A(j - 1), A(j)) > 0; j--)
      cdsa_swap_bytes(A(j - 1), A(j), sz);
}

cdsa_status cdsa_sort_insertion(void *b, size_t n, size_t sz, cdsa_cmp_fn cmp) {
  cdsa_status s = check(b, n, sz, cmp);
  if (s != CDSA_OK) return s;
  insertion_range(b, 0, n, sz, cmp);
  return CDSA_OK;
}

/* ---- quicksort: median-of-3, 3-way partition, recurse on smaller side ---- */
#define QS_CUTOFF 12

static void quick_range(unsigned char *base, size_t lo, size_t hi, size_t sz, cdsa_cmp_fn cmp) {
  while (hi - lo > QS_CUTOFF) {
    size_t mid = lo + (hi - lo) / 2, last = hi - 1, med;
    /* Index of the median of a[lo], a[mid], a[last]; move it to lo as the pivot. */
    if (cmp(A(lo), A(mid)) < 0)
      med = cmp(A(mid), A(last)) < 0 ? mid : (cmp(A(lo), A(last)) < 0 ? last : lo);
    else
      med = cmp(A(lo), A(last)) < 0 ? lo : (cmp(A(mid), A(last)) < 0 ? last : mid);
    cdsa_swap_bytes(A(lo), A(med), sz);

    /* Invariant: [lo,lt) < pivot, [lt,i) == pivot, [gt,hi) > pivot. a[lt] stays a pivot copy. */
    size_t lt = lo, i = lo + 1, gt = hi;
    while (i < gt) {
      int c = cmp(A(i), A(lt));
      if (c < 0) { cdsa_swap_bytes(A(lt), A(i), sz); lt++; i++; }
      else if (c > 0) { gt--; cdsa_swap_bytes(A(i), A(gt), sz); }
      else i++;
    }
    /* Recurse into the smaller side, loop on the larger: stack depth is O(log n). */
    if (lt - lo < hi - gt) { quick_range(base, lo, lt, sz, cmp); lo = gt; }
    else { quick_range(base, gt, hi, sz, cmp); hi = lt; }
  }
  insertion_range(base, lo, hi, sz, cmp);
}

cdsa_status cdsa_sort_quick(void *b, size_t n, size_t sz, cdsa_cmp_fn cmp) {
  cdsa_status s = check(b, n, sz, cmp);
  if (s != CDSA_OK) return s;
  if (n > 1) quick_range(b, 0, n, sz, cmp);
  return CDSA_OK;
}

/* ---- heapsort: in place, guaranteed O(n log n) ---- */
static void heap_sift_down(unsigned char *base, size_t i, size_t n, size_t sz, cdsa_cmp_fn cmp) {
  for (;;) {
    size_t l = 2 * i + 1, r = l + 1, big = i;
    if (l < n && cmp(A(l), A(big)) > 0) big = l;
    if (r < n && cmp(A(r), A(big)) > 0) big = r;
    if (big == i) return;
    cdsa_swap_bytes(A(i), A(big), sz);
    i = big;
  }
}

cdsa_status cdsa_sort_heap(void *b, size_t n, size_t sz, cdsa_cmp_fn cmp) {
  cdsa_status s = check(b, n, sz, cmp);
  if (s != CDSA_OK) return s;
  unsigned char *base = b;
  if (n < 2) return CDSA_OK;
  for (size_t i = n / 2; i-- > 0;) heap_sift_down(base, i, n, sz, cmp);
  for (size_t end = n - 1; end > 0; end--) {
    cdsa_swap_bytes(A(0), A(end), sz);
    heap_sift_down(base, 0, end, sz, cmp);
  }
  return CDSA_OK;
}

/* ---- merge sort: stable, O(n) scratch ---- */
static void merge_rec(unsigned char *base, unsigned char *tmp, size_t n, size_t sz, cdsa_cmp_fn cmp) {
  if (n < 2) return;
  size_t mid = n / 2;
  merge_rec(base, tmp, mid, sz, cmp);
  merge_rec(base + mid * sz, tmp, n - mid, sz, cmp);
  if (cmp(A(mid - 1), A(mid)) <= 0) return; /* already ordered */
  memcpy(tmp, base, mid * sz);              /* copy left run out */
  size_t i = 0, j = mid, k = 0;
  while (i < mid && j < n) {
    if (cmp(tmp + i * sz, A(j)) <= 0) memcpy(A(k++), tmp + (i++) * sz, sz); /* <= keeps stability */
    else memcpy(A(k++), A(j++), sz);
  }
  while (i < mid) memcpy(A(k++), tmp + (i++) * sz, sz);
}

cdsa_status cdsa_sort_merge(void *b, size_t n, size_t sz, cdsa_cmp_fn cmp) {
  cdsa_status s = check(b, n, sz, cmp);
  if (s != CDSA_OK) return s;
  if (n < 2) return CDSA_OK;
  if (n / 2 > SIZE_MAX / sz) return CDSA_ERR_NOMEM;
  unsigned char *tmp = cdsa_malloc((n / 2) * sz);
  if (!tmp) return CDSA_ERR_NOMEM;
  merge_rec(b, tmp, n, sz, cmp);
  cdsa_free(tmp);
  return CDSA_OK;
}

bool cdsa_is_sorted(const void *b, size_t n, size_t sz, cdsa_cmp_fn cmp) {
  if (check(b, n, sz, cmp) != CDSA_OK) return false;
  const unsigned char *base = b;
  for (size_t i = 1; i < n; i++)
    if (cmp(A(i - 1), A(i)) > 0) return false;
  return true;
}
