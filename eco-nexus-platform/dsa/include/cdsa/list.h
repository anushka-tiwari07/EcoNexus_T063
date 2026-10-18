/* cdsa/list.h - doubly linked list; elements are copied into nodes.
 * Complexity: push/pop at either end O(1); at/remove_at/find O(n); reverse O(n). */
#ifndef CDSA_LIST_H
#define CDSA_LIST_H
#include "cdsa/common.h"
CDSA_BEGIN_DECLS

typedef struct cdsa_list cdsa_list;
typedef bool (*cdsa_list_visit_fn)(void *elem, void *user); /* return false to stop */

cdsa_status cdsa_list_create(cdsa_list **out, size_t elem_size);
void cdsa_list_destroy(cdsa_list *l); /* NULL-safe */
cdsa_status cdsa_list_push_front(cdsa_list *l, const void *elem);
cdsa_status cdsa_list_push_back(cdsa_list *l, const void *elem);
cdsa_status cdsa_list_pop_front(cdsa_list *l, void *out);
cdsa_status cdsa_list_pop_back(cdsa_list *l, void *out);
void *cdsa_list_front(const cdsa_list *l);
void *cdsa_list_back(const cdsa_list *l);
void *cdsa_list_at(const cdsa_list *l, size_t index); /* NULL if out of range */
cdsa_status cdsa_list_remove_at(cdsa_list *l, size_t index, void *out);
void *cdsa_list_find(const cdsa_list *l, const void *key, cdsa_cmp_fn cmp);
cdsa_status cdsa_list_remove_first(cdsa_list *l, const void *key, cdsa_cmp_fn cmp);
void cdsa_list_reverse(cdsa_list *l);
void cdsa_list_foreach(const cdsa_list *l, cdsa_list_visit_fn fn, void *user);
size_t cdsa_list_size(const cdsa_list *l);

CDSA_END_DECLS
#endif
