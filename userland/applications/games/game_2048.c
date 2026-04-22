#include <userland/applications/include/games/game_2048.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/syscalls.h>

static const struct rect DEFAULT_2048_WINDOW = {52, 24, 420, 360};

static uint32_t game_2048_next_random(struct game_2048_state *game) {
    game->seed = (game->seed * 1664525u) + 1013904223u;
    return game->seed;
}

static void game_2048_append_int(char *buf, int value, int max_len) {
    char tmp[16];
    int pos = 0;
    int len = str_len(buf);
    unsigned uvalue = value < 0 ? (unsigned)(-value) : (unsigned)value;

    if (len >= max_len - 1) {
        return;
    }
    if (value < 0 && len < max_len - 1) {
        buf[len++] = '-';
    }
    if (uvalue == 0u) {
        tmp[pos++] = '0';
    } else {
        while (uvalue > 0u && pos < (int)sizeof(tmp)) {
            tmp[pos++] = (char)('0' + (uvalue % 10u));
            uvalue /= 10u;
        }
    }
    while (pos > 0 && len < max_len - 1) {
        buf[len++] = tmp[--pos];
    }
    buf[len] = '\0';
}

static uint8_t game_2048_tile_color(uint16_t value, const struct desktop_theme *theme) {
    switch (value) {
    case 0: return theme->background;
    case 2: return theme->window;
    case 4: return theme->menu;
    case 8: return 14;
    case 16: return 12;
    case 32: return 9;
    case 64: return 10;
    case 128: return 11;
    case 256: return 3;
    case 512: return 5;
    case 1024: return 13;
    case 2048: return 6;
    default: return theme->menu_button_inactive;
    }
}

static int game_2048_empty_count(const struct game_2048_state *game) {
    int empty = 0;

    for (int y = 0; y < GAME_2048_SIZE; ++y) {
        for (int x = 0; x < GAME_2048_SIZE; ++x) {
            if (game->board[y][x] == 0u) {
                empty += 1;
            }
        }
    }
    return empty;
}

static void game_2048_refresh_flags(struct game_2048_state *game) {
    int can_move = 0;

    game->best_tile = 0;
    for (int y = 0; y < GAME_2048_SIZE; ++y) {
        for (int x = 0; x < GAME_2048_SIZE; ++x) {
            int value = game->board[y][x];

            if (value > game->best_tile) {
                game->best_tile = value;
            }
            if (value == 0u) {
                can_move = 1;
            }
            if (value >= 2048) {
                game->won = 1;
            }
            if (x + 1 < GAME_2048_SIZE && value != 0u && game->board[y][x + 1] == value) {
                can_move = 1;
            }
            if (y + 1 < GAME_2048_SIZE && value != 0u && game->board[y + 1][x] == value) {
                can_move = 1;
            }
        }
    }
    game->game_over = can_move ? 0 : 1;
}

static void game_2048_spawn_tile(struct game_2048_state *game) {
    int empty = game_2048_empty_count(game);
    int target;

    if (empty <= 0) {
        return;
    }

    target = (int)(game_2048_next_random(game) % (uint32_t)empty);
    for (int y = 0; y < GAME_2048_SIZE; ++y) {
        for (int x = 0; x < GAME_2048_SIZE; ++x) {
            if (game->board[y][x] != 0u) {
                continue;
            }
            if (target == 0) {
                game->board[y][x] = (game_2048_next_random(game) % 10u) == 0u ? 4u : 2u;
                return;
            }
            target -= 1;
        }
    }
}

static void game_2048_reset(struct game_2048_state *game) {
    for (int y = 0; y < GAME_2048_SIZE; ++y) {
        for (int x = 0; x < GAME_2048_SIZE; ++x) {
            game->board[y][x] = 0u;
        }
    }
    if (game->seed == 0u) {
        game->seed = 1u;
    }
    game->score = 0;
    game->best_tile = 0;
    game->game_over = 0;
    game->won = 0;
    game_2048_spawn_tile(game);
    game_2048_spawn_tile(game);
    game_2048_refresh_flags(game);
}

static int game_2048_apply_line(uint16_t *line, int *score_out) {
    uint16_t packed[GAME_2048_SIZE];
    uint16_t merged[GAME_2048_SIZE];
    int packed_count = 0;
    int merged_count = 0;
    int changed = 0;

    for (int i = 0; i < GAME_2048_SIZE; ++i) {
        if (line[i] != 0u) {
            packed[packed_count++] = line[i];
        }
    }

    for (int i = 0; i < packed_count; ++i) {
        uint16_t value = packed[i];

        if (i + 1 < packed_count && packed[i + 1] == value) {
            value = (uint16_t)(value * 2u);
            *score_out += value;
            i += 1;
        }
        merged[merged_count++] = value;
    }
    while (merged_count < GAME_2048_SIZE) {
        merged[merged_count++] = 0u;
    }

    for (int i = 0; i < GAME_2048_SIZE; ++i) {
        if (line[i] != merged[i]) {
            changed = 1;
        }
        line[i] = merged[i];
    }
    return changed;
}

static int game_2048_apply_move(struct game_2048_state *game, int dx, int dy) {
    int changed = 0;
    int gained = 0;

    if ((dx == 0 && dy == 0) || (dx != 0 && dy != 0)) {
        return 0;
    }

    for (int index = 0; index < GAME_2048_SIZE; ++index) {
        uint16_t line[GAME_2048_SIZE];

        for (int step = 0; step < GAME_2048_SIZE; ++step) {
            int x;
            int y;

            if (dx > 0) {
                x = (GAME_2048_SIZE - 1) - step;
                y = index;
            } else if (dx < 0) {
                x = step;
                y = index;
            } else if (dy > 0) {
                x = index;
                y = (GAME_2048_SIZE - 1) - step;
            } else {
                x = index;
                y = step;
            }
            line[step] = game->board[y][x];
        }

        if (game_2048_apply_line(line, &gained)) {
            changed = 1;
        }

        for (int step = 0; step < GAME_2048_SIZE; ++step) {
            int x;
            int y;

            if (dx > 0) {
                x = (GAME_2048_SIZE - 1) - step;
                y = index;
            } else if (dx < 0) {
                x = step;
                y = index;
            } else if (dy > 0) {
                x = index;
                y = (GAME_2048_SIZE - 1) - step;
            } else {
                x = index;
                y = step;
            }
            game->board[y][x] = line[step];
        }
    }

    if (!changed) {
        return 0;
    }

    game->score += gained;
    game_2048_spawn_tile(game);
    game_2048_refresh_flags(game);
    return 1;
}

void game_2048_init_state(struct game_2048_state *game) {
    game->window = DEFAULT_2048_WINDOW;
    game->seed = 1u;
    game_2048_reset(game);
}

int game_2048_step(struct game_2048_state *game, uint32_t ticks) {
    (void)game;
    (void)ticks;
    return 0;
}

int game_2048_handle_key(struct game_2048_state *game, int key) {
    if (key == 'r' || key == 'R' || key == 'n' || key == 'N') {
        game_2048_reset(game);
        return 1;
    }

    if (key == KEY_ARROW_LEFT) {
        return game_2048_apply_move(game, -1, 0);
    }
    if (key == KEY_ARROW_RIGHT) {
        return game_2048_apply_move(game, 1, 0);
    }
    if (key == KEY_ARROW_UP) {
        return game_2048_apply_move(game, 0, -1);
    }
    if (key == KEY_ARROW_DOWN) {
        return game_2048_apply_move(game, 0, 1);
    }

    return 0;
}

static void game_2048_draw_tile(int x, int y, int cell, uint16_t value,
                                const struct desktop_theme *theme) {
    int inset = cell / 10;
    int inner = cell - (inset * 2);
    char label[16];

    if (inset < 4) {
        inset = 4;
        inner = cell - (inset * 2);
    }

    sys_rect(x, y, cell - 2, cell - 2, theme->menu_button_inactive);
    sys_rect(x + 2, y + 2, cell - 6, cell - 6, game_2048_tile_color(value, theme));
    if (value == 0u) {
        return;
    }

    label[0] = '\0';
    game_2048_append_int(label, value, (int)sizeof(label));
    sys_text(x + inset + 4, y + (cell / 2) - 4, theme->text, label);
    sys_rect(x + inset, y + inset, inner > 0 ? inner : 1, 1, theme->text);
}

void game_2048_draw_window(struct game_2048_state *game, int active,
                           int min_hover, int max_hover, int close_hover) {
    struct rect board;
    struct rect panel;
    struct rect stats;
    struct rect legend;
    const struct desktop_theme *theme = ui_theme_get();
    int available_w = game->window.w - 40;
    int available_h = game->window.h - 128;
    int cell = available_w / GAME_2048_SIZE;
    char score[32];
    char best[32];

    if ((available_h / GAME_2048_SIZE) < cell) {
        cell = available_h / GAME_2048_SIZE;
    }
    if (cell < 40) {
        cell = 40;
    }

    board.w = cell * GAME_2048_SIZE;
    board.h = cell * GAME_2048_SIZE;
    board.x = game->window.x + ((game->window.w - board.w) / 2);
    board.y = game->window.y + 66;
    panel = (struct rect){game->window.x + 8, game->window.y + 20,
                          game->window.w - 16, game->window.h - 28};
    stats = (struct rect){game->window.x + 16, game->window.y + 28,
                          game->window.w - 32, 30};
    legend = (struct rect){game->window.x + 16, board.y + board.h + 10,
                           game->window.w - 32, 38};

    draw_window_frame(&game->window, "2048", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&panel, ui_color_window_bg());
    ui_draw_surface(&stats, theme->menu);
    ui_draw_inset(&board, ui_color_window_bg());
    ui_draw_surface(&legend, theme->window);

    score[0] = '\0';
    str_append(score, "Score ", (int)sizeof(score));
    game_2048_append_int(score, game->score, (int)sizeof(score));
    best[0] = '\0';
    str_append(best, "Maior ", (int)sizeof(best));
    game_2048_append_int(best, game->best_tile, (int)sizeof(best));

    sys_text(stats.x + 10, stats.y + 8, theme->text, score);
    sys_text(stats.x + 120, stats.y + 8, theme->text, best);
    sys_text(stats.x + 232, stats.y + 8, theme->text, "Setas movem  R reinicia");

    for (int y = 0; y < GAME_2048_SIZE; ++y) {
        for (int x = 0; x < GAME_2048_SIZE; ++x) {
            game_2048_draw_tile(board.x + (x * cell) + 6,
                                board.y + (y * cell) + 6,
                                cell - 8,
                                game->board[y][x],
                                theme);
        }
    }

    if (game->game_over) {
        sys_text(legend.x + 10, legend.y + 9, theme->text, "Sem jogadas. Aperte R para recomecar.");
    } else if (game->won) {
        sys_text(legend.x + 10, legend.y + 9, theme->text, "Voce fez 2048. Pode continuar jogando.");
    } else {
        sys_text(legend.x + 10, legend.y + 9, theme->text, "Junte blocos iguais ate chegar em 2048.");
    }
}
