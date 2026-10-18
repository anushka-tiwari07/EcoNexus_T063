/* cdsa/queue.h - FIFO queue on a growing ring buffer.
 * Complexity: enqueue amortized O(1); dequeue O(1); peek O(1). */
#ifndef CDSA_QUEUE_H
#define CDSA_QUEUE_H
#include "cdsa/common.h"
CDSA_BEGIN_DECLS

typedef struct cdsa_queue cdsa_queue;

cdsa_status cdsa_queue_create(cdsa_queue **out, size_t elem_size);
void cdsa_queue_destroy(cdsa_queue *q); /* NULL-safe */
cdsa_status cdsa_queue_enqueue(cdsa_queue *q, const void *elem);
cdsa_status cdsa_queue_dequeue(cdsa_queue *q, void *out /* may be NULL */);
const void *cdsa_queue_peek(const cdsa_queue *q); /* NULL if empty */
size_t cdsa_queue_size(const cdsa_queue *q);
bool cdsa_queue_empty(const cdsa_queue *q);

CDSA_END_DECLS
#endif
