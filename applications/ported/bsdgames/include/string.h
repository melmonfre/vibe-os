#ifndef VIBE_BSDGAME_STRING_H
#define VIBE_BSDGAME_STRING_H

#include <lang/include/vibe_app_runtime.h>

char *strrchr(const char *text, int c);
char *strstr(const char *text, const char *needle);
char *strcasestr(const char *text, const char *needle);
int strcasecmp(const char *lhs, const char *rhs);
int strncasecmp(const char *lhs, const char *rhs, size_t n);
char *strtok(char *text, const char *delim);
char *strtok_r(char *text, const char *delim, char **saveptr);
size_t strspn(const char *text, const char *accept);
size_t strcspn(const char *text, const char *reject);
char *strdup(const char *text);
char *strndup(const char *text, size_t max_len);
size_t strlcpy(char *dst, const char *src, size_t size);
size_t strlcat(char *dst, const char *src, size_t size);
char *strsep(char **stringp, const char *delim);
char *strerror(int errnum);
char *strncpy(char *dst, const char *src, size_t n);

#endif
