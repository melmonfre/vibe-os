#include "localcharset.h"

#include <locale.h>
#include <string.h>

const char *
locale_charset(void)
{
  const char *locale = setlocale(LC_CTYPE, NULL);

  if (locale && strstr(locale, "UTF-8"))
    return "UTF-8";
  return "C";
}
