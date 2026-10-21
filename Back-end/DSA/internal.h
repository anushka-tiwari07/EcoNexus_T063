/* Private helpers shared by cdsa translation units. Not installed. */
#ifndef CDSA_INTERNAL_H
#define CDSA_INTERNAL_H
#include <stddef.h>
#include <string.h>

#define CDSA_AT(base, i, sz) ((unsigned char *)(base) + (i) * (sz))

/* Swap two non-overlapping (or identical) byte ranges without heap use. */
static inline void cdsa_swap_bytes(void *a, void *b, size_t n) {
  unsigned char buf[64];
  unsigned char *p = (unsigned char *)a, *q = (unsigned char *)b;
  if (p == q) return;
  while (n) {
    size_t c = n < sizeof buf ? n : sizeof buf;
    memcpy(buf, p, c);
    memcpy(p, q, c);
    memcpy(q, buf, c);
    p += c; q += c; n -= c;
  }
}
#endif
