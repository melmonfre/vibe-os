#ifndef MINESWEEPER_H
#define MINESWEEPER_H

#include <userland/modules/include/utils.h>
#include <stdint.h>

#define MINESWEEPER_ROWS 10
#define MINESWEEPER_COLS 10
#define MINESWEEPER_MINES 14
#define MINESWEEPER_CLICK_REVEAL 0
#define MINESWEEPER_CLICK_FLAG 1

struct minesweeper_state {
    struct rect window;
    uint8_t mines[MINESWEEPER_ROWS][MINESWEEPER_COLS];
    uint8_t adjacent[MINESWEEPER_ROWS][MINESWEEPER_COLS];
    uint8_t revealed[MINESWEEPER_ROWS][MINESWEEPER_COLS];
    uint8_t flagged[MINESWEEPER_ROWS][MINESWEEPER_COLS];
    uint32_t seed;
    int cursor_x;
    int cursor_y;
    int mines_placed;
    int revealed_safe;
    int exploded;
    int won;
};

void minesweeper_init_state(struct minesweeper_state *game);
int minesweeper_step(struct minesweeper_state *game, uint32_t ticks);
int minesweeper_handle_key(struct minesweeper_state *game, int key);
int minesweeper_handle_click(struct minesweeper_state *game, int x, int y, int button);
void minesweeper_draw_window(struct minesweeper_state *game, int active,
                             int min_hover, int max_hover, int close_hover);

#endif
