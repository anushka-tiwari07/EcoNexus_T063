#include "cdsa/queue.h"
#include "internal.h"

struct cdsa_queue {
  unsigned char *data;
  size_t head, size, cap, elem;
};

cdsa_status cdsa_queue_create(cdsa_queue **out, size_t elem_size) {
  if (!out || elem_size == 0) return CDSA_ERR_INVALID;
  *out = NULL;
  cdsa_queue *q = cdsa_calloc(1, sizeof *q);
  if (!q) return CDSA_ERR_NOMEM;
  q->elem = elem_size;
  *out = q;
  return CDSA_OK;
}

void cdsa_queue_destroy(cdsa_queue *q) {
  if (!q) return;
  cdsa_free(q->data);
  cdsa_free(q);
}

static cdsa_status grow(cdsa_queue *q) {
  size_t nc = q->cap ? q->cap * 2 : 8;
  if (q->cap > SIZE_MAX / 2 || nc > SIZE_MAX / q->elem) return CDSA_ERR_NOMEM;
  unsigned char *nd = cdsa_malloc(nc * q->elem);
  if (!nd) return CDSA_ERR_NOMEM;
  /* Unwrap the ring into the new buffer so head becomes 0. */
  for (size_t i = 0; i < q->size; i++)
    memcpy(nd + i * q->elem, q->data + ((q->head + i) % q->cap) * q->elem, q->elem);
  cdsa_free(q->data);
  q->data = nd;
  q->cap = nc;
  q->head = 0;
  return CDSA_OK;
}

cdsa_status cdsa_queue_enqueue(cdsa_queue *q, const void *elem) {
  if (!q || !elem) return CDSA_ERR_INVALID;
  if (q->size == q->cap) {
    cdsa_status s = grow(q);
    if (s != CDSA_OK) return s;
  }
  memcpy(q->data + ((q->head + q->size) % q->cap) * q->elem, elem, q->elem);
  q->size++;
  return CDSA_OK;
}

cdsa_status cdsa_queue_dequeue(cdsa_queue *q, void *out) {
  if (!q) return CDSA_ERR_INVALID;
  if (q->size == 0) return CDSA_ERR_EMPTY;
  if (out) memcpy(out, q->data + q->head * q->elem, q->elem);
  q->head = (q->head + 1) % q->cap;
  q->size--;
  return CDSA_OK;
}

const void *cdsa_queue_peek(const cdsa_queue *q) { return (q && q->size) ? q->data + q->head * q->elem : NULL; }
size_t cdsa_queue_size(const cdsa_queue *q) { return q ? q->size : 0; }
bool cdsa_queue_empty(const cdsa_queue *q) { return cdsa_queue_size(q) == 0; }
