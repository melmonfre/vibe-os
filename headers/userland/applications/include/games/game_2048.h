#ifndef GAME_2048_H
#define GAME_2048_H

#include <userland/modules/include/utils.h>
#include <stdint.h>

#define GAME_2048_SIZE 4

struct game_2048_state {
    struct rect window;
    uint16_t board[GAME_2048_SIZE][GAME_2048_SIZE];
    uint32_t seed;
    int score;
    int best_tile;
    int game_over;
    int won;
};

void game_2048_init_state(struct game_2048_state *game);
int game_2048_step(struct game_2048_state *game, uint32_t ticks);
int game_2048_handle_key(struct game_2048_state *game, int key);
void game_2048_draw_window(struct game_2048_state *game, int active,
                           int min_hover, int max_hover, int close_hover);

#endif
