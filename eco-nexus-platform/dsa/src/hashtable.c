#include "cdsa/hashtable.h"
#include "internal.h"

typedef struct entry {
  struct entry *next;
  char *key;
  uint64_t hash;
  _Alignas(max_align_t) unsigned char value[]; /* value_size bytes */
} entry;

struct cdsa_hashtable {
  entry **buckets;
  size_t nbuckets, size, vsize;
};

#define HT_INITIAL_BUCKETS 16

static uint64_t fnv1a(const char *s) {
  uint64_t h = 1469598103934665603ULL;
  for (; *s; s++) { h ^= (unsigned char)*s; h *= 1099511628211ULL; }
  return h;
}

cdsa_status cdsa_ht_create(cdsa_hashtable **out, size_t value_size) {
  if (!out || value_size == 0) return CDSA_ERR_INVALID;
  *out = NULL;
  cdsa_hashtable *h = cdsa_calloc(1, sizeof *h);
  if (!h) return CDSA_ERR_NOMEM;
  h->buckets = cdsa_calloc(HT_INITIAL_BUCKETS, sizeof(entry *));
  if (!h->buckets) { cdsa_free(h); return CDSA_ERR_NOMEM; }
  h->nbuckets = HT_INITIAL_BUCKETS;
  h->vsize = value_size;
  *out = h;
  return CDSA_OK;
}

static void free_entry(entry *e) { cdsa_free(e->key); cdsa_free(e); }

void cdsa_ht_destroy(cdsa_hashtable *h) {
  if (!h) return;
  for (size_t i = 0; i < h->nbuckets; i++) {
    entry *e = h->buckets[i];
    while (e) { entry *n = e->next; free_entry(e); e = n; }
  }
  cdsa_free(h->buckets);
  cdsa_free(h);
}

static entry *find(const cdsa_hashtable *h, const char *key, uint64_t hash) {
  for (entry *e = h->buckets[hash % h->nbuckets]; e; e = e->next)
    if (e->hash == hash && strcmp(e->key, key) == 0) return e;
  return NULL;
}

/* Best-effort: on failure the table simply stays at its current (valid) size. */
static void grow(cdsa_hashtable *h) {
  if (h->nbuckets > SIZE_MAX / 2 / sizeof(entry *)) return;
  size_t nb = h->nbuckets * 2;
  entry **nbk = cdsa_calloc(nb, sizeof(entry *));
  if (!nbk) return;
  for (size_t i = 0; i < h->nbuckets; i++) {
    entry *e = h->buckets[i];
    while (e) {
      entry *next = e->next;
      size_t idx = e->hash % nb;
      e->next = nbk[idx];
      nbk[idx] = e;
      e = next;
    }
  }
  cdsa_free(h->buckets);
  h->buckets = nbk;
  h->nbuckets = nb;
}

cdsa_status cdsa_ht_put(cdsa_hashtable *h, const char *key, const void *value) {
  if (!h || !key || !value) return CDSA_ERR_INVALID;
  uint64_t hash = fnv1a(key);
  entry *e = find(h, key, hash);
  if (e) { memcpy(e->value, value, h->vsize); return CDSA_OK; }

  size_t klen = strlen(key) + 1;
  if (h->vsize > SIZE_MAX - sizeof(entry)) return CDSA_ERR_NOMEM;
  e = cdsa_malloc(sizeof(entry) + h->vsize);
  if (!e) return CDSA_ERR_NOMEM;
  e->key = cdsa_malloc(klen);
  if (!e->key) { cdsa_free(e); return CDSA_ERR_NOMEM; }
  memcpy(e->key, key, klen);
  memcpy(e->value, value, h->vsize);
  e->hash = hash;
  size_t idx = hash % h->nbuckets;
  e->next = h->buckets[idx];
  h->buckets[idx] = e;
  h->size++;
  if (h->size * 4 > h->nbuckets * 3) grow(h);
  return CDSA_OK;
}

void *cdsa_ht_get(const cdsa_hashtable *h, const char *key) {
  if (!h || !key) return NULL;
  entry *e = find(h, key, fnv1a(key));
  return e ? e->value : NULL;
}

bool cdsa_ht_contains(const cdsa_hashtable *h, const char *key) { return cdsa_ht_get(h, key) != NULL; }

cdsa_status cdsa_ht_remove(cdsa_hashtable *h, const char *key, void *out) {
  if (!h || !key) return CDSA_ERR_INVALID;
  uint64_t hash = fnv1a(key);
  entry **link = &h->buckets[hash % h->nbuckets];
  for (entry *e = *link; e; link = &e->next, e = e->next) {
    if (e->hash == hash && strcmp(e->key, key) == 0) {
      if (out) memcpy(out, e->value, h->vsize);
      *link = e->next;
      free_entry(e);
      h->size--;
      return CDSA_OK;
    }
  }
  return CDSA_ERR_NOT_FOUND;
}

void cdsa_ht_foreach(const cdsa_hashtable *h, cdsa_ht_visit_fn fn, void *user) {
  if (!h || !fn) return;
  for (size_t i = 0; i < h->nbuckets; i++)
    for (entry *e = h->buckets[i]; e; e = e->next)
      if (!fn(e->key, e->value, user)) return;
}

size_t cdsa_ht_size(const cdsa_hashtable *h) { return h ? h->size : 0; }
