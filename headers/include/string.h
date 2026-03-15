#ifndef STRING_H
#define STRING_H

#include <sys/_types.h>

size_t strlen(const char* str);
char* strcpy(char* dest, const char* src);
int strcmp(const char* s1, const char* s2);

#endif
