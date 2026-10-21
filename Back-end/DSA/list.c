#include "cdsa/list.h"
#include "internal.h"

typedef struct node {
  struct node *prev, *next;
  _Alignas(max_align_t) unsigned char data[]; /* elem_size bytes */
} node;

struct cdsa_list {
  node *head, *tail;
  size_t size, elem;
};

cdsa_status cdsa_list_create(cdsa_list **out, size_t elem_size) {
  if (!out || elem_size == 0) return CDSA_ERR_INVALID;
  *out = NULL;
  cdsa_list *l = cdsa_calloc(1, sizeof *l);
  if (!l) return CDSA_ERR_NOMEM;
  l->elem = elem_size;
  *out = l;
  return CDSA_OK;
}

void cdsa_list_destroy(cdsa_list *l) {
  if (!l) return;
  node *n = l->head;
  while (n) { node *next = n->next; cdsa_free(n); n = next; }
  cdsa_free(l);
}

static node *new_node(const cdsa_list *l, const void *elem) {
  if (l->elem > SIZE_MAX - sizeof(node)) return NULL;
  node *n = cdsa_malloc(sizeof(node) + l->elem);
  if (!n) return NULL;
  n->prev = n->next = NULL;
  memcpy(n->data, elem, l->elem);
  return n;
}

cdsa_status cdsa_list_push_front(cdsa_list *l, const void *elem) {
  if (!l || !elem) return CDSA_ERR_INVALID;
  node *n = new_node(l, elem);
  if (!n) return CDSA_ERR_NOMEM;
  n->next = l->head;
  if (l->head) l->head->prev = n; else l->tail = n;
  l->head = n;
  l->size++;
  return CDSA_OK;
}

cdsa_status cdsa_list_push_back(cdsa_list *l, const void *elem) {
  if (!l || !elem) return CDSA_ERR_INVALID;
  node *n = new_node(l, elem);
  if (!n) return CDSA_ERR_NOMEM;
  n->prev = l->tail;
  if (l->tail) l->tail->next = n; else l->head = n;
  l->tail = n;
  l->size++;
  return CDSA_OK;
}

static void unlink_node(cdsa_list *l, node *n, void *out) {
  if (out) memcpy(out, n->data, l->elem);
  if (n->prev) n->prev->next = n->next; else l->head = n->next;
  if (n->next) n->next->prev = n->prev; else l->tail = n->prev;
  l->size--;
  cdsa_free(n);
}

cdsa_status cdsa_list_pop_front(cdsa_list *l, void *out) {
  if (!l) return CDSA_ERR_INVALID;
  if (!l->head) return CDSA_ERR_EMPTY;
  unlink_node(l, l->head, out);
  return CDSA_OK;
}
cdsa_status cdsa_list_pop_back(cdsa_list *l, void *out) {
  if (!l) return CDSA_ERR_INVALID;
  if (!l->tail) return CDSA_ERR_EMPTY;
  unlink_node(l, l->tail, out);
  return CDSA_OK;
}

void *cdsa_list_front(const cdsa_list *l) { return (l && l->head) ? l->head->data : NULL; }
void *cdsa_list_back(const cdsa_list *l) { return (l && l->tail) ? l->tail->data : NULL; }

static node *node_at(const cdsa_list *l, size_t index) {
  if (!l || index >= l->size) return NULL;
  node *n;
  if (index < l->size / 2) { n = l->head; while (index--) n = n->next; }
  else { n = l->tail; for (size_t i = l->size - 1; i > index; i--) n = n->prev; }
  return n;
}

void *cdsa_list_at(const cdsa_list *l, size_t index) {
  node *n = node_at(l, index);
  return n ? n->data : NULL;
}

cdsa_status cdsa_list_remove_at(cdsa_list *l, size_t index, void *out) {
  if (!l) return CDSA_ERR_INVALID;
  node *n = node_at(l, index);
  if (!n) return CDSA_ERR_RANGE;
  unlink_node(l, n, out);
  return CDSA_OK;
}

static node *find_node(const cdsa_list *l, const void *key, cdsa_cmp_fn cmp) {
  if (!l || !key || !cmp) return NULL;
  for (node *n = l->head; n; n = n->next)
    if (cmp(n->data, key) == 0) return n;
  return NULL;
}

void *cdsa_list_find(const cdsa_list *l, const void *key, cdsa_cmp_fn cmp) {
  node *n = find_node(l, key, cmp);
  return n ? n->data : NULL;
}

cdsa_status cdsa_list_remove_first(cdsa_list *l, const void *key, cdsa_cmp_fn cmp) {
  if (!l || !key || !cmp) return CDSA_ERR_INVALID;
  node *n = find_node(l, key, cmp);
  if (!n) return CDSA_ERR_NOT_FOUND;
  unlink_node(l, n, NULL);
  return CDSA_OK;
}

void cdsa_list_reverse(cdsa_list *l) {
  if (!l) return;
  node *n = l->head;
  while (n) { node *next = n->next; n->next = n->prev; n->prev = next; n = next; }
  node *t = l->head; l->head = l->tail; l->tail = t;
}

void cdsa_list_foreach(const cdsa_list *l, cdsa_list_visit_fn fn, void *user) {
  if (!l || !fn) return;
  for (node *n = l->head; n; n = n->next)
    if (!fn(n->data, user)) return;
}

size_t cdsa_list_size(const cdsa_list *l) { return l ? l->size : 0; }
