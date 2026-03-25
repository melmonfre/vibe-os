#ifndef VIBE_SED_STRING_H
#define VIBE_SED_STRING_H

#include_next <string.h>

void *mempcpy(void *dst, const void *src, size_t n);
char *strdup(const char *s);
int strverscmp(const char *a, const char *b);

#endif
