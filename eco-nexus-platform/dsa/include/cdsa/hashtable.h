/* cdsa/hashtable.h - string-keyed hash table, separate chaining, FNV-1a 64-bit.
 * Keys and fixed-size values are copied in. Resizes at load factor 0.75.
 * Complexity: put/get/remove average O(1), worst case O(n); foreach O(n + buckets). */
#ifndef CDSA_HASHTABLE_H
#define CDSA_HASHTABLE_H
#include "cdsa/common.h"
CDSA_BEGIN_DECLS

typedef struct cdsa_hashtable cdsa_hashtable;
typedef bool (*cdsa_ht_visit_fn)(const char *key, void *value, void *user); /* false = stop */

cdsa_status cdsa_ht_create(cdsa_hashtable **out, size_t value_size);
void cdsa_ht_destroy(cdsa_hashtable *h); /* NULL-safe */
cdsa_status cdsa_ht_put(cdsa_hashtable *h, const char *key, const void *value); /* insert or overwrite */
void *cdsa_ht_get(const cdsa_hashtable *h, const char *key);                    /* NULL if absent */
bool cdsa_ht_contains(const cdsa_hashtable *h, const char *key);
cdsa_status cdsa_ht_remove(cdsa_hashtable *h, const char *key, void *out /* may be NULL */);
void cdsa_ht_foreach(const cdsa_hashtable *h, cdsa_ht_visit_fn fn, void *user);
size_t cdsa_ht_size(const cdsa_hashtable *h);

CDSA_END_DECLS
#endif
