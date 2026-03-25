#include "progname.h"

#include <string.h>

const char *program_name = "sed";

void
set_program_name(const char *argv0)
{
  const char *slash;

  if (!argv0 || !*argv0)
    return;
  slash = strrchr(argv0, '/');
  program_name = slash ? slash + 1 : argv0;
}
