#ifndef VIBE_SED_XALLOC_H
#define VIBE_SED_XALLOC_H

#include <stddef.h>
#include <stdint.h>

#include "idx.h"

void xalloc_die(void) __attribute__((noreturn));
void *xmalloc(size_t s);
void *ximalloc(idx_t s);
void *xzalloc(size_t s);
void *xnmalloc(size_t n, size_t s);
void *xmemdup(const void *p, size_t s);
char *xstrdup(const char *str);
void *xpalloc(void *pa, idx_t *pn, idx_t n_incr_min, ptrdiff_t n_max, idx_t s);

#define XNMALLOC(n, t) ((t *) xnmalloc((size_t)(n), sizeof(t)))

#endif
