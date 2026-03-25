#include "xalloc.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
xalloc_die(void)
{
  fputs("sed: memory exhausted\n", stderr);
  exit(4);
}

static size_t
xchecked_mul(size_t a, size_t b)
{
  if (a != 0 && b > (SIZE_MAX / a))
    xalloc_die();
  return a * b;
}

void *
xmalloc(size_t s)
{
  void *p = malloc(s ? s : 1);
  if (!p)
    xalloc_die();
  return p;
}

void *
ximalloc(idx_t s)
{
  if (s < 0)
    xalloc_die();
  return xmalloc((size_t) s);
}

void *
xzalloc(size_t s)
{
  void *p = calloc(1, s ? s : 1);
  if (!p)
    xalloc_die();
  return p;
}

void *
xnmalloc(size_t n, size_t s)
{
  return xmalloc(xchecked_mul(n, s));
}

void *
xmemdup(const void *p, size_t s)
{
  void *dup = xmalloc(s);
  memcpy(dup, p, s);
  return dup;
}

char *
xstrdup(const char *str)
{
  size_t len;
  char *dup;

  if (!str)
    return NULL;
  len = strlen(str) + 1;
  dup = xmalloc(len);
  memcpy(dup, str, len);
  return dup;
}

void *
xpalloc(void *pa, idx_t *pn, idx_t n_incr_min, ptrdiff_t n_max, idx_t s)
{
  idx_t current;
  idx_t wanted;
  idx_t grown;
  size_t bytes;
  void *p;

  if (!pn || s <= 0 || n_incr_min < 0)
    xalloc_die();

  current = *pn;
  if (current < 0)
    xalloc_die();

  wanted = current + n_incr_min;
  if (wanted < current)
    xalloc_die();

  grown = current > 0 ? current * 2 : 64;
  if (grown < wanted)
    grown = wanted;
  if (n_max >= 0 && grown > n_max)
    grown = wanted > n_max ? n_max : wanted;
  if (grown < wanted)
    xalloc_die();

  bytes = xchecked_mul((size_t) grown, (size_t) s);
  p = realloc(pa, bytes ? bytes : 1);
  if (!p)
    xalloc_die();

  *pn = grown;
  return p;
}
