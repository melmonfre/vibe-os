#include "version-etc.h"

#include <stdarg.h>
#include <stdio.h>

void
version_etc(FILE *stream, const char *command_name, const char *package, const char *version, ...)
{
  va_list ap;
  const char *author;
  int first = 1;

  fprintf(stream, "%s (%s) %s\n", command_name, package, version);
  fputs("Written by ", stream);

  va_start(ap, version);
  while ((author = va_arg(ap, const char *)) != NULL)
    {
      if (!first)
        fputs(", ", stream);
      fputs(author, stream);
      first = 0;
    }
  va_end(ap);

  fputc('\n', stream);
}
