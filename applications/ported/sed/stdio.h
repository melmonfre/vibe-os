#ifndef VIBE_SED_STDIO_H
#define VIBE_SED_STDIO_H

#include_next <stdio.h>
#include <sys/types.h>

FILE *fdopen(int fd, const char *mode);
int fileno(FILE *stream);
ssize_t getdelim(char **text, size_t *buflen, int delim, FILE *stream);
int snprintf(char *str, size_t size, const char *fmt, ...);

#endif
