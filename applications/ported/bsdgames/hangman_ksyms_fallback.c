#include "vibe_bsdgame_shim.h"

#include <errno.h>

#include "compat/games/hangman/hangman.h"

void
sym_getword(void)
{
    getword();
}

int
sym_setup(void)
{
    errno = ENOEXEC;
    return -1;
}
