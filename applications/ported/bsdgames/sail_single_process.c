#include "vibe_bsdgame_shim.h"

#include "compat/games/sail/driver.h"
#include "compat/games/sail/extern.h"
#include "compat/games/sail/player.h"

int vibe_sail_single_process = 0;

void
vibe_sail_driver_step(void)
{
    if (!vibe_sail_single_process) {
        return;
    }
    if (next() < 0) {
        hasdriver = 0;
        return;
    }
    unfoul();
    checkup();
    prizecheck();
    moveall();
    thinkofgrapples();
    boardcomp();
    compcombat();
    resolve();
    reload();
    checksails();
}
