#include <string.h>
#include "cdsa/cdsa.h"
#include "test_alloc.h"
#include "test_harness.h"

static int cmp_int(const void *a, const void *b) { int x = *(const int *)a, y = *(const int *)b; return (x > y) - (x < y); }
static int cmp_int_desc(const void *a, const void *b) { return cmp_int(b, a); }

static void test_dynarray(void) {
  cdsa_dynarray *a = NULL;
  CHECK_STATUS(cdsa_dynarray_create(NULL, 4, 0), CDSA_ERR_INVALID);
  CHECK_STATUS(cdsa_dynarray_create(&a, 0, 0), CDSA_ERR_INVALID);
  CHECK_STATUS(cdsa_dynarray_create(&a, sizeof(int), 0), CDSA_OK);
  CHECK_EQ(cdsa_dynarray_size(a), 0);
  CHECK_STATUS(cdsa_dynarray_pop(a, NULL), CDSA_ERR_EMPTY);
  CHECK(cdsa_dynarray_get(a, 0) == NULL);
  for (int i = 0; i < 1000; i++) CHECK_STATUS(cdsa_dynarray_push(a, &i), CDSA_OK);
  CHECK_EQ(cdsa_dynarray_size(a), 1000);
  CHECK(cdsa_dynarray_capacity(a) >= 1000);
  CHECK_EQ(*(int *)cdsa_dynarray_get(a, 999), 999);
  CHECK(cdsa_dynarray_get(a, 1000) == NULL);
  int v = -1, out = 0;
  CHECK_STATUS(cdsa_dynarray_insert(a, 0, &v), CDSA_OK);          /* front */
  CHECK_STATUS(cdsa_dynarray_insert(a, 501, &v), CDSA_OK);        /* middle */
  CHECK_STATUS(cdsa_dynarray_insert(a, cdsa_dynarray_size(a), &v), CDSA_OK); /* end */
  CHECK_STATUS(cdsa_dynarray_insert(a, cdsa_dynarray_size(a) + 1, &v), CDSA_ERR_RANGE);
  CHECK_EQ(cdsa_dynarray_size(a), 1003);
  CHECK_EQ(*(int *)cdsa_dynarray_get(a, 1), 0);
  CHECK_STATUS(cdsa_dynarray_remove_at(a, 0, &out), CDSA_OK);
  CHECK_EQ(out, -1);
  CHECK_EQ(*(int *)cdsa_dynarray_get(a, 0), 0);
  CHECK_STATUS(cdsa_dynarray_remove_at(a, 5000, NULL), CDSA_ERR_RANGE);
  CHECK_STATUS(cdsa_dynarray_pop(a, &out), CDSA_OK);
  CHECK_EQ(out, -1);
  cdsa_dynarray_clear(a);
  CHECK_EQ(cdsa_dynarray_size(a), 0);
  CHECK_STATUS(cdsa_dynarray_reserve(a, SIZE_MAX), CDSA_ERR_NOMEM); /* overflow guarded */
  cdsa_dynarray_destroy(a);
  cdsa_dynarray_destroy(NULL);
}

static void test_list(void) {
  cdsa_list *l = NULL;
  CHECK_STATUS(cdsa_list_create(&l, sizeof(int)), CDSA_OK);
  int out = 0;
  CHECK_STATUS(cdsa_list_pop_front(l, &out), CDSA_ERR_EMPTY);
  CHECK_STATUS(cdsa_list_pop_back(l, &out), CDSA_ERR_EMPTY);
  CHECK(cdsa_list_front(l) == NULL && cdsa_list_back(l) == NULL);
  for (int i = 1; i <= 5; i++) cdsa_list_push_back(l, &i);   /* 1 2 3 4 5 */
  int z = 0; cdsa_list_push_front(l, &z);                    /* 0 1 2 3 4 5 */
  CHECK_EQ(cdsa_list_size(l), 6);
  CHECK_EQ(*(int *)cdsa_list_front(l), 0);
  CHECK_EQ(*(int *)cdsa_list_back(l), 5);
  for (int i = 0; i < 6; i++) CHECK_EQ(*(int *)cdsa_list_at(l, (size_t)i), i);
  CHECK(cdsa_list_at(l, 6) == NULL);
  int key = 3;
  CHECK(cdsa_list_find(l, &key, cmp_int) != NULL);
  key = 99;
  CHECK(cdsa_list_find(l, &key, cmp_int) == NULL);
  CHECK_STATUS(cdsa_list_remove_first(l, &key, cmp_int), CDSA_ERR_NOT_FOUND);
  key = 3;
  CHECK_STATUS(cdsa_list_remove_first(l, &key, cmp_int), CDSA_OK);
  CHECK_STATUS(cdsa_list_remove_at(l, 0, &out), CDSA_OK);  /* removes 0 */
  CHECK_EQ(out, 0);
  CHECK_STATUS(cdsa_list_remove_at(l, 10, NULL), CDSA_ERR_RANGE);
  cdsa_list_reverse(l);                                    /* 5 4 2 1 */
  CHECK_EQ(*(int *)cdsa_list_front(l), 5);
  CHECK_EQ(*(int *)cdsa_list_back(l), 1);
  CHECK_EQ(*(int *)cdsa_list_at(l, 2), 2);
  CHECK_STATUS(cdsa_list_pop_back(l, &out), CDSA_OK);
  CHECK_EQ(out, 1);
  CHECK_STATUS(cdsa_list_pop_front(l, &out), CDSA_OK);
  CHECK_EQ(out, 5);
  CHECK_EQ(cdsa_list_size(l), 2);
  cdsa_list_destroy(l);
  cdsa_list_destroy(NULL);
}

static void test_queue(void) {
  cdsa_queue *q = NULL;
  CHECK_STATUS(cdsa_queue_create(&q, sizeof(int)), CDSA_OK);
  CHECK(cdsa_queue_empty(q));
  CHECK(cdsa_queue_peek(q) == NULL);
  int out = 0;
  CHECK_STATUS(cdsa_queue_dequeue(q, &out), CDSA_ERR_EMPTY);
  int next_in = 0, next_out = 0;
  for (int i = 0; i < 5; i++) cdsa_queue_enqueue(q, &next_in), next_in++;
  for (int i = 0; i < 3; i++) { cdsa_queue_dequeue(q, &out); CHECK_EQ(out, next_out++); }
  for (int i = 0; i < 40; i++) cdsa_queue_enqueue(q, &next_in), next_in++; /* wraps then grows */
  CHECK_EQ(cdsa_queue_size(q), 42);
  CHECK_EQ(*(const int *)cdsa_queue_peek(q), next_out);
  while (!cdsa_queue_empty(q)) { cdsa_queue_dequeue(q, &out); CHECK_EQ(out, next_out++); }
  CHECK_EQ(next_out, next_in);
  cdsa_queue_destroy(q);
}

typedef struct { int priority; int id; } task;
static int cmp_task_prio(const void *a, const void *b) { return cmp_int(&((const task *)a)->priority, &((const task *)b)->priority); }

static void test_heap(void) {
  cdsa_heap *h = NULL;
  CHECK_STATUS(cdsa_heap_create(&h, sizeof(int), NULL), CDSA_ERR_INVALID);
  CHECK_STATUS(cdsa_heap_create(&h, sizeof(int), cmp_int), CDSA_OK);
  int out;
  CHECK_STATUS(cdsa_heap_pop(h, &out), CDSA_ERR_EMPTY);
  CHECK(cdsa_heap_peek(h) == NULL);
  int vals[500];
  for (int i = 0; i < 500; i++) { vals[i] = (int)(rnd() % 1000); cdsa_heap_push(h, &vals[i]); }
  CHECK_EQ(cdsa_heap_size(h), 500);
  int prev = -1;
  for (int i = 0; i < 500; i++) {
    CHECK_EQ(*(const int *)cdsa_heap_peek(h), *(const int *)cdsa_heap_peek(h));
    CHECK_STATUS(cdsa_heap_pop(h, &out), CDSA_OK);
    CHECK(out >= prev);
    prev = out;
  }
  CHECK_EQ(cdsa_heap_size(h), 0);
  cdsa_heap_destroy(h);

  /* max-heap via reversed comparator */
  CHECK_STATUS(cdsa_heap_create(&h, sizeof(int), cmp_int_desc), CDSA_OK);
  for (int i = 0; i < 10; i++) cdsa_heap_push(h, &i);
  cdsa_heap_pop(h, &out);
  CHECK_EQ(out, 9);
  cdsa_heap_destroy(h);

  /* struct payload: lowest priority number first (scheduler-style) */
  cdsa_heap *th = NULL;
  CHECK_STATUS(cdsa_heap_create(&th, sizeof(task), cmp_task_prio), CDSA_OK);
  task t1 = {5, 1}, t2 = {1, 2}, t3 = {3, 3}, got;
  cdsa_heap_push(th, &t1); cdsa_heap_push(th, &t2); cdsa_heap_push(th, &t3);
  cdsa_heap_pop(th, &got); CHECK_EQ(got.id, 2);
  cdsa_heap_pop(th, &got); CHECK_EQ(got.id, 3);
  cdsa_heap_pop(th, &got); CHECK_EQ(got.id, 1);
  cdsa_heap_destroy(th);
}

static bool count_cb(const char *k, void *v, void *user) { (void)k; (void)v; (*(int *)user)++; return true; }
static bool stop_cb(const char *k, void *v, void *user) { (void)k; (void)v; (*(int *)user)++; return false; }

static void test_hashtable(void) {
  cdsa_hashtable *h = NULL;
  CHECK_STATUS(cdsa_ht_create(&h, 0), CDSA_ERR_INVALID);
  CHECK_STATUS(cdsa_ht_create(&h, sizeof(int)), CDSA_OK);
  int v = 7, out = 0;
  CHECK(cdsa_ht_get(h, "missing") == NULL);
  CHECK_STATUS(cdsa_ht_remove(h, "missing", NULL), CDSA_ERR_NOT_FOUND);
  CHECK_STATUS(cdsa_ht_put(h, NULL, &v), CDSA_ERR_INVALID);
  CHECK_STATUS(cdsa_ht_put(h, "coal", &v), CDSA_OK);
  CHECK_EQ(*(int *)cdsa_ht_get(h, "coal"), 7);
  v = 9;
  CHECK_STATUS(cdsa_ht_put(h, "coal", &v), CDSA_OK);   /* overwrite, size unchanged */
  CHECK_EQ(cdsa_ht_size(h), 1);
  CHECK_EQ(*(int *)cdsa_ht_get(h, "coal"), 9);
  CHECK(cdsa_ht_contains(h, "coal"));
  CHECK_STATUS(cdsa_ht_remove(h, "coal", &out), CDSA_OK);
  CHECK_EQ(out, 9);
  CHECK(!cdsa_ht_contains(h, "coal"));
  CHECK_EQ(cdsa_ht_size(h), 0);

  char key[32];
  for (int i = 0; i < 5000; i++) { snprintf(key, sizeof key, "key-%d", i); cdsa_ht_put(h, key, &i); }
  CHECK_EQ(cdsa_ht_size(h), 5000);
  int bad = 0;
  for (int i = 0; i < 5000; i++) { snprintf(key, sizeof key, "key-%d", i); int *p = cdsa_ht_get(h, key); if (!p || *p != i) bad++; }
  CHECK_EQ(bad, 0);
  int n = 0; cdsa_ht_foreach(h, count_cb, &n); CHECK_EQ(n, 5000);
  n = 0; cdsa_ht_foreach(h, stop_cb, &n); CHECK_EQ(n, 1);
  for (int i = 0; i < 5000; i += 2) { snprintf(key, sizeof key, "key-%d", i); cdsa_ht_remove(h, key, NULL); }
  CHECK_EQ(cdsa_ht_size(h), 2500);
  CHECK(cdsa_ht_get(h, "key-1") != NULL);
  CHECK(cdsa_ht_get(h, "key-2") == NULL);
  cdsa_ht_destroy(h);
}

static void test_no_leaks_normal_use(void) {
  ta_install(0);
  cdsa_dynarray *a; cdsa_list *l; cdsa_queue *q; cdsa_heap *h; cdsa_hashtable *t;
  cdsa_dynarray_create(&a, sizeof(int), 0); cdsa_list_create(&l, sizeof(int));
  cdsa_queue_create(&q, sizeof(int)); cdsa_heap_create(&h, sizeof(int), cmp_int);
  cdsa_ht_create(&t, sizeof(int));
  for (int i = 0; i < 300; i++) {
    char k[16]; snprintf(k, sizeof k, "%d", i);
    cdsa_dynarray_push(a, &i); cdsa_list_push_back(l, &i); cdsa_queue_enqueue(q, &i);
    cdsa_heap_push(h, &i); cdsa_ht_put(t, k, &i);
  }
  cdsa_dynarray_destroy(a); cdsa_list_destroy(l); cdsa_queue_destroy(q);
  cdsa_heap_destroy(h); cdsa_ht_destroy(t);
  CHECK_EQ(g_ta.live, 0);
  ta_uninstall();
}

int main(void) {
  RUN(test_dynarray); RUN(test_list); RUN(test_queue); RUN(test_heap);
  RUN(test_hashtable); RUN(test_no_leaks_normal_use);
  return test_summary("containers");
}
