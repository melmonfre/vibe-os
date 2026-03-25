#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "xalloc.h"

int vibe_app_read_file(const char *path, const char **data_out, int *size_out);
int vibe_app_write_file(const char *path, const void *data, int size);

void
unreachable(void)
{
  abort();
}

char *
strdup(const char *s)
{
  return xstrdup(s);
}

void *
mempcpy(void *dst, const void *src, size_t n)
{
  memcpy(dst, src, n);
  return (char *) dst + n;
}

int
strverscmp(const char *a, const char *b)
{
  return strcmp(a, b);
}

intmax_t
strtoimax(const char *nptr, char **endptr, int base)
{
  return (intmax_t) strtol(nptr, endptr, base);
}

FILE *
fdopen(int fd, const char *mode)
{
  (void) fd;
  (void) mode;
  errno = ENOSYS;
  return NULL;
}

int
fileno(FILE *stream)
{
  (void) stream;
  return -1;
}

ssize_t
getdelim(char **text, size_t *buflen, int delim, FILE *stream)
{
  size_t len = 0;
  int ch;

  if (!text || !buflen || !stream)
    {
      errno = EINVAL;
      return -1;
    }

  if (!*text || *buflen == 0)
    {
      *buflen = 128;
      *text = xmalloc(*buflen);
    }

  while ((ch = fgetc(stream)) != EOF)
    {
      if (len + 1 >= *buflen)
        {
          idx_t size = (idx_t) *buflen;
          *text = xpalloc(*text, &size, 1, -1, 1);
          *buflen = (size_t) size;
        }
      (*text)[len++] = (char) ch;
      if (ch == delim)
        break;
    }

  if (len == 0 && ch == EOF)
    return -1;

  (*text)[len] = '\0';
  return (ssize_t) len;
}

ssize_t
readlink(const char *path, char *buf, size_t bufsiz)
{
  (void) path;
  (void) buf;
  (void) bufsiz;
  errno = EINVAL;
  return -1;
}

int
unlink(const char *path)
{
  (void) path;
  errno = ENOSYS;
  return -1;
}

mode_t
umask(mode_t mask)
{
  (void) mask;
  return 0;
}

int
rename(const char *from, const char *to)
{
  const char *data = NULL;
  int size = 0;

  if (!from || !to)
    {
      errno = EINVAL;
      return -1;
    }

  if (vibe_app_read_file(from, &data, &size) != 0)
    {
      errno = ENOENT;
      return -1;
    }
  if (vibe_app_write_file(to, data, size) != 0)
    {
      errno = EIO;
      return -1;
    }
  return 0;
}
