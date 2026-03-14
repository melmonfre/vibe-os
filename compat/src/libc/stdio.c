/* stdio.c - minimal stubs */
#include "../include/compat/libc/stdio.h"
#include <stdarg.h>

FILE *stdout = NULL;
FILE *stdin = NULL;  
FILE *stderr = NULL;

/* These are provided by app_runtime.h */
