#include <userland/applications/include/games/pacman.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>

static const struct rect DEFAULT_WINDOW = {40, 20, 400, 300};
static const int PACMAN_STEP_TICKS = 7;
static const int PACMAN_TUNNEL_ROW = 6;
static const int PACMAN_PELLET_SCORE = 10;
static const int PACMAN_POWER_SCORE = 50;
static const int PACMAN_TILE_SIZE = 14;
static const int PACMAN_BOARD_PAD = 8;
static const int PACMAN_GHOST_RELEASE_STEP = 28;

static const char *g_maze[PACMAN_GRID_H] = {
    "###################",
    "#........#........#",
    "#.###.##.#.##.###.#",
    "#o###.##.#.##.###o#",
    "#.................#",
    "#.###.#.###.#.###.#",
    "....#..#...#..#....",
    "#.###.#.###.#.###.#",
    "#.....#.....#.....#",
    "#.###.##.#.##.###.#",
    "#.###.#.....#.###.#",
    "#........#........#",
    "###################"
};

static const int g_ghost_spawn_x[PACMAN_GHOSTS] = {8, 10, 8, 10};
static const int g_ghost_spawn_y[PACMAN_GHOSTS] = {5, 5, 7, 7};
static const uint8_t g_ghost_colors[PACMAN_GHOSTS] = {12u, 9u, 10u, 13u};
static const int g_ghost_corner_x[PACMAN_GHOSTS] = {17, 1, 17, 1};
static const int g_ghost_corner_y[PACMAN_GHOSTS] = {1, 1, 11, 11};

static uint32_t pacman_next_random(struct pacman_state *s) {
    s->seed = (s->seed * 1664525u) + 1013904223u;
    return s->seed;
}

static int pacman_abs(int value) {
    return value < 0 ? -value : value;
}

static int pacman_center_x(int tile_x) {
    return tile_x * PACMAN_TILE_SIZE;
}

static int pacman_center_y(int tile_y) {
    return tile_y * PACMAN_TILE_SIZE;
}

static int pacman_tile_open(const struct pacman_state *s, int x, int y) {
    if (y < 0 || y >= PACMAN_GRID_H) {
        return 0;
    }
    if (x < 0 || x >= PACMAN_GRID_W) {
        return 0;
    }
    return s->walls[y][x] == 0u;
}

static int pacman_wrap_x(const struct pacman_state *s, int x, int y) {
    if (y != PACMAN_TUNNEL_ROW) {
        return x;
    }
    if (x < 0 && s->walls[y][PACMAN_GRID_W - 1] == 0u) {
        return PACMAN_GRID_W - 1;
    }
    if (x >= PACMAN_GRID_W && s->walls[y][0] == 0u) {
        return 0;
    }
    return x;
}

static int pacman_try_step(const struct pacman_state *s,
                           int x,
                           int y,
                           int dx,
                           int dy,
                           int *out_x,
                           int *out_y) {
    int nx = x + dx;
    int ny = y + dy;

    nx = pacman_wrap_x(s, nx, ny);
    if (!pacman_tile_open(s, nx, ny)) {
        return 0;
    }

    *out_x = nx;
    *out_y = ny;
    return 1;
}

static void pacman_reset_actors(struct pacman_state *s) {
    s->player_x = 9;
    s->player_y = 9;
    s->dir_x = -1;
    s->dir_y = 0;
    s->next_dir_x = -1;
    s->next_dir_y = 0;

    for (int i = 0; i < PACMAN_GHOSTS; ++i) {
        s->ghost_x[i] = g_ghost_spawn_x[i];
        s->ghost_y[i] = g_ghost_spawn_y[i];
        s->ghost_dir_x[i] = (i & 1) ? -1 : 1;
        s->ghost_dir_y[i] = 0;
    }

    s->frightened_until = 0u;
    s->scatter_until = s->tick_count + 140u;
    s->round_start_tick = s->tick_count;
    s->ghost_combo = 1;

    for (int i = 0; i < PACMAN_GHOSTS; ++i) {
        s->ghost_release_tick[i] = s->tick_count + (uint32_t)(i * PACMAN_GHOST_RELEASE_STEP);
    }
    s->ghost_release_tick[0] = s->tick_count;
}

static void pacman_reset_board(struct pacman_state *s) {
    s->pellets_left = 0;

    for (int y = 0; y < PACMAN_GRID_H; ++y) {
        for (int x = 0; x < PACMAN_GRID_W; ++x) {
            char tile = g_maze[y][x];

            if (tile == '#') {
                s->walls[y][x] = 1u;
                s->pellets[y][x] = 0u;
            } else if (tile == '.') {
                s->walls[y][x] = 0u;
                s->pellets[y][x] = 1u;
                s->pellets_left += 1;
            } else if (tile == 'o') {
                s->walls[y][x] = 0u;
                s->pellets[y][x] = 2u;
                s->pellets_left += 1;
            } else {
                s->walls[y][x] = 0u;
                s->pellets[y][x] = 0u;
            }
        }
    }
}

static void pacman_reset_match(struct pacman_state *s) {
    pacman_reset_board(s);
    pacman_reset_actors(s);
    s->tick_count = 0u;
    s->next_tick = 0u;
    s->score = 0;
    s->lives = 3;
    s->level = 1;
    s->game_over = 0;
    s->win = 0;
    if (s->seed == 0u) {
        s->seed = 1u;
    }
}

static void pacman_append_int(char *buf, int value, int max_len) {
    char tmp[12];
    int pos = 0;
    int len = str_len(buf);
    unsigned uvalue;

    if (len >= max_len - 1) {
        return;
    }

    if (value < 0) {
        if (len < max_len - 1) {
            buf[len++] = '-';
            buf[len] = '\0';
        }
        value = -value;
    }

    uvalue = (unsigned)value;
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

static void pacman_eat_pellet(struct pacman_state *s) {
    uint8_t pellet = s->pellets[s->player_y][s->player_x];

    if (pellet == 0u) {
        return;
    }

    s->pellets[s->player_y][s->player_x] = 0u;
    if (s->pellets_left > 0) {
        s->pellets_left -= 1;
    }

    if (pellet == 1u) {
        s->score += PACMAN_PELLET_SCORE;
    } else {
        s->score += PACMAN_POWER_SCORE;
        s->frightened_until = s->tick_count + 110u;
        s->ghost_combo = 1;
    }

    if (s->pellets_left == 0) {
        s->win = 1;
    }
}

static void pacman_advance_level(struct pacman_state *s) {
    s->level += 1;
    if (s->level > 9) {
        s->level = 9;
    }
    pacman_reset_board(s);
    pacman_reset_actors(s);
    s->win = 0;
    s->next_tick = s->tick_count + 28u;
}

static void pacman_handle_player_death(struct pacman_state *s) {
    s->lives -= 1;
    if (s->lives <= 0) {
        s->lives = 0;
        s->game_over = 1;
        return;
    }

    pacman_reset_actors(s);
    s->next_tick = s->tick_count + 24u;
}

static void pacman_collide_ghost(struct pacman_state *s, int index) {
    if (s->tick_count < s->frightened_until) {
        s->score += 200 * s->ghost_combo;
        if (s->ghost_combo < 8) {
            s->ghost_combo *= 2;
        }
        s->ghost_x[index] = g_ghost_spawn_x[index];
        s->ghost_y[index] = g_ghost_spawn_y[index];
        s->ghost_dir_x[index] = (index & 1) ? -1 : 1;
        s->ghost_dir_y[index] = 0;
        s->ghost_release_tick[index] = s->tick_count + 18u;
    } else {
        pacman_handle_player_death(s);
    }
}

static void pacman_check_collisions(struct pacman_state *s) {
    for (int i = 0; i < PACMAN_GHOSTS; ++i) {
        if (s->ghost_x[i] == s->player_x && s->ghost_y[i] == s->player_y) {
            pacman_collide_ghost(s, i);
            if (s->game_over) {
                return;
            }
        }
    }
}

static int pacman_choose_ghost_dir(struct pacman_state *s, int index) {
    static const int dir_x[4] = {1, -1, 0, 0};
    static const int dir_y[4] = {0, 0, 1, -1};
    int gx = s->ghost_x[index];
    int gy = s->ghost_y[index];
    int reverse_x = -s->ghost_dir_x[index];
    int reverse_y = -s->ghost_dir_y[index];
    int best_dir = -1;
    int best_score = 0x7fffffff;
    int frightened = s->tick_count < s->frightened_until;
    int target_x = s->player_x;
    int target_y = s->player_y;
    int scatter = (!frightened) && (((s->tick_count - s->round_start_tick) % 220u) < 54u);

    if (scatter) {
        target_x = g_ghost_corner_x[index];
        target_y = g_ghost_corner_y[index];
    } else if (index == 1) {
        target_x += (s->dir_x * 2);
        target_y += (s->dir_y * 2);
    } else if (index == 2) {
        target_x += (s->dir_x * 4);
        target_y += (s->dir_y * 4);
    } else if (index == 3) {
        target_x = (s->player_x < (PACMAN_GRID_W / 2)) ? (PACMAN_GRID_W - 2) : 1;
        target_y = (s->player_y < (PACMAN_GRID_H / 2)) ? (PACMAN_GRID_H - 2) : 1;
    }

    if (target_x < 0) target_x = 0;
    if (target_x >= PACMAN_GRID_W) target_x = PACMAN_GRID_W - 1;
    if (target_y < 0) target_y = 0;
    if (target_y >= PACMAN_GRID_H) target_y = PACMAN_GRID_H - 1;

    for (int d = 0; d < 4; ++d) {
        int nx;
        int ny;
        int score;

        if (dir_x[d] == reverse_x && dir_y[d] == reverse_y) {
            continue;
        }
        if (!pacman_try_step(s, gx, gy, dir_x[d], dir_y[d], &nx, &ny)) {
            continue;
        }

        score = pacman_abs(nx - target_x) + pacman_abs(ny - target_y);
        if (frightened) {
            score = 300 - score;
        }
        score += (int)(pacman_next_random(s) & 1u);

        if (score < best_score) {
            best_score = score;
            best_dir = d;
        }
    }

    if (best_dir < 0) {
        for (int d = 0; d < 4; ++d) {
            int nx;
            int ny;
            if (pacman_try_step(s, gx, gy, dir_x[d], dir_y[d], &nx, &ny)) {
                best_dir = d;
                break;
            }
        }
    }

    if (best_dir < 0) {
        return 0;
    }

    s->ghost_dir_x[index] = dir_x[best_dir];
    s->ghost_dir_y[index] = dir_y[best_dir];
    return 1;
}

static void pacman_step_ghosts(struct pacman_state *s) {
    for (int i = 0; i < PACMAN_GHOSTS; ++i) {
        int nx = s->ghost_x[i];
        int ny = s->ghost_y[i];
        int slow_frightened;

        if (s->tick_count < s->ghost_release_tick[i]) {
            continue;
        }

        slow_frightened = (s->tick_count < s->frightened_until) && (((s->tick_count + (uint32_t)i) & 1u) == 0u);
        if (slow_frightened) {
            continue;
        }

        (void)pacman_choose_ghost_dir(s, i);
        if (pacman_try_step(s,
                            s->ghost_x[i],
                            s->ghost_y[i],
                            s->ghost_dir_x[i],
                            s->ghost_dir_y[i],
                            &nx,
                            &ny)) {
            s->ghost_x[i] = nx;
            s->ghost_y[i] = ny;
        }
    }
}

void pacman_init_state(struct pacman_state *s) {
    s->window = DEFAULT_WINDOW;
    s->seed = 1u;
    pacman_reset_match(s);
}

int pacman_handle_key(struct pacman_state *s, int key) {
    if (s->game_over) {
        if (key == 'r' || key == 'R' || key == ' ' || key == '\n') {
            pacman_reset_match(s);
            return 1;
        }
        return 0;
    }

    if (key == KEY_ARROW_UP) {
        s->next_dir_x = 0;
        s->next_dir_y = -1;
        return 1;
    }
    if (key == KEY_ARROW_DOWN) {
        s->next_dir_x = 0;
        s->next_dir_y = 1;
        return 1;
    }
    if (key == KEY_ARROW_LEFT) {
        s->next_dir_x = -1;
        s->next_dir_y = 0;
        return 1;
    }
    if (key == KEY_ARROW_RIGHT) {
        s->next_dir_x = 1;
        s->next_dir_y = 0;
        return 1;
    }
    return 0;
}

int pacman_step(struct pacman_state *s, uint32_t ticks) {
    int nx;
    int ny;

    (void)ticks;

    if (s->game_over) {
        return 0;
    }

    s->tick_count += 1u;
    if (s->win) {
        pacman_advance_level(s);
        return 1;
    }

    if (s->next_tick == 0u) {
        s->next_tick = s->tick_count + PACMAN_STEP_TICKS;
        return 1;
    }
    if (s->tick_count < s->next_tick) {
        return 0;
    }
    s->next_tick = s->tick_count + PACMAN_STEP_TICKS;

    nx = s->player_x;
    ny = s->player_y;

    if (pacman_try_step(s,
                        s->player_x,
                        s->player_y,
                        s->next_dir_x,
                        s->next_dir_y,
                        &nx,
                        &ny)) {
        s->dir_x = s->next_dir_x;
        s->dir_y = s->next_dir_y;
        s->player_x = nx;
        s->player_y = ny;
    } else if (pacman_try_step(s,
                               s->player_x,
                               s->player_y,
                               s->dir_x,
                               s->dir_y,
                               &nx,
                               &ny)) {
        s->player_x = nx;
        s->player_y = ny;
    }

    pacman_eat_pellet(s);
    pacman_check_collisions(s);
    if (s->game_over || s->win) {
        return 1;
    }

    pacman_step_ghosts(s);
    pacman_check_collisions(s);
    return 1;
}

static void pacman_draw_actor(int x, int y, int size, uint8_t color) {
    sys_rect(x + 2, y + 1, size - 4, size - 2, color);
    sys_rect(x + 1, y + 2, size - 2, size - 4, color);
}

static void pacman_draw_backdrop(const struct rect *board, const struct desktop_theme *t) {
    for (int i = 0; i < 4; ++i) {
        int px = board->x + 10 + (i * 62);
        int py = board->y + 12 + ((i & 1) ? 8 : 0);
        sys_rect(px, py + 18, 20, 18, t->menu_button_inactive);
        sys_rect(px + 4, py + 8, 12, 8, t->window);
        sys_rect(px + 6, py + 22, 8, 2, t->window_bg);
    }
    sys_rect(board->x + 8, board->y + 18, board->w - 16, 1, t->taskbar);
}

static void pacman_draw_player_sprite(const struct rect *board, struct pacman_state *s, const struct desktop_theme *t) {
    int px = board->x + pacman_center_x(s->player_x);
    int py = board->y + pacman_center_y(s->player_y);
    int mouth_phase = (int)((s->tick_count / 4u) & 1u);

    pacman_draw_actor(px, py, PACMAN_TILE_SIZE - 1, t->menu_button);
    sys_rect(px + 5, py + 3, 1, 1, t->text);

    if (mouth_phase) {
        if (s->dir_x > 0) {
            sys_rect(px + 7, py + 3, 4, 2, ui_color_canvas());
            sys_rect(px + 7, py + 8, 4, 2, ui_color_canvas());
        } else if (s->dir_x < 0) {
            sys_rect(px + 1, py + 3, 4, 2, ui_color_canvas());
            sys_rect(px + 1, py + 8, 4, 2, ui_color_canvas());
        } else if (s->dir_y < 0) {
            sys_rect(px + 3, py + 1, 2, 4, ui_color_canvas());
            sys_rect(px + 8, py + 1, 2, 4, ui_color_canvas());
        } else {
            sys_rect(px + 3, py + 7, 2, 4, ui_color_canvas());
            sys_rect(px + 8, py + 7, 2, 4, ui_color_canvas());
        }
    }
}

static void pacman_draw_ghost_sprite(const struct rect *board, struct pacman_state *s, int index, const struct desktop_theme *t) {
    int px = board->x + pacman_center_x(s->ghost_x[index]);
    int py = board->y + pacman_center_y(s->ghost_y[index]);
    uint8_t color = g_ghost_colors[index];

    if (s->tick_count < s->frightened_until) {
        color = (((s->frightened_until - s->tick_count) < 24u) && ((s->tick_count & 4u) != 0u))
                    ? t->text
                    : t->menu_button_inactive;
    }

    sys_rect(px + 2, py + 1, 6, 2, color);
    sys_rect(px + 1, py + 3, 8, 4, color);
    sys_rect(px + 2, py + 7, 2, 2, color);
    sys_rect(px + 5, py + 7, 2, 2, color);
    sys_rect(px + 8, py + 7, 1, 2, color);

    sys_rect(px + 3, py + 3, 1, 1, t->text);
    sys_rect(px + 6, py + 3, 1, 1, t->text);
    sys_rect(px + 3, py + 4, 1, 1, t->window_bg);
    sys_rect(px + 6, py + 4, 1, 1, t->window_bg);
}

void pacman_draw_window(struct pacman_state *s, int active, int min_hover, int max_hover, int close_hover) {
    const struct desktop_theme *t = ui_theme_get();
    struct rect body = {s->window.x + 4, s->window.y + 18, s->window.w - 8, s->window.h - 22};
    struct rect topbar = {s->window.x + 8, s->window.y + 22, s->window.w - 16, 18};
    struct rect board = {
        s->window.x + PACMAN_BOARD_PAD + 8,
        s->window.y + 44,
        PACMAN_GRID_W * PACMAN_TILE_SIZE,
        PACMAN_GRID_H * PACMAN_TILE_SIZE
    };
    struct rect hud = {board.x + board.w + 8, board.y, s->window.w - (board.w + 24), board.h};
    struct rect stat1 = {hud.x + 6, hud.y + 12, hud.w - 12, 28};
    struct rect stat2 = {hud.x + 6, hud.y + 46, hud.w - 12, 28};
    struct rect stat3 = {hud.x + 6, hud.y + 80, hud.w - 12, 28};
    struct rect help = {hud.x + 6, hud.y + 118, hud.w - 12, 54};
    struct rect status = {hud.x + 6, hud.y + 180, hud.w - 12, 34};
    char line[48];

    draw_window_frame(&s->window, "PACPAC", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&body, ui_color_canvas());
    ui_draw_surface(&topbar, ui_color_panel());
    ui_draw_inset(&board, ui_color_canvas());
    ui_draw_inset(&hud, ui_color_canvas());
    ui_draw_inset(&stat1, ui_color_canvas());
    ui_draw_inset(&stat2, ui_color_canvas());
    ui_draw_inset(&stat3, ui_color_canvas());
    ui_draw_inset(&help, ui_color_canvas());
    ui_draw_inset(&status, ui_color_canvas());

    sys_text(topbar.x + 6, topbar.y + 5, ui_color_muted(), "Maze chase");
    sys_rect(board.x + 1, board.y + 1, board.w - 2, board.h - 2, t->background);
    pacman_draw_backdrop(&board, t);

    for (int y = 0; y < PACMAN_GRID_H; ++y) {
        for (int x = 0; x < PACMAN_GRID_W; ++x) {
            int tx = board.x + (x * PACMAN_TILE_SIZE);
            int ty = board.y + (y * PACMAN_TILE_SIZE);
            int center = PACMAN_TILE_SIZE / 2;

            if (s->walls[y][x]) {
                sys_rect(tx + 1, ty + 1, PACMAN_TILE_SIZE - 2, PACMAN_TILE_SIZE - 2, t->window);
                sys_rect(tx + 3, ty + 3, PACMAN_TILE_SIZE - 6, PACMAN_TILE_SIZE - 6, t->background);
                sys_rect(tx + 4, ty + 4, PACMAN_TILE_SIZE - 8, 1, t->menu_button_inactive);
                sys_rect(tx + 4, ty + PACMAN_TILE_SIZE - 5, PACMAN_TILE_SIZE - 8, 1, t->menu_button_inactive);
                continue;
            }

            if (s->pellets[y][x] == 1u) {
                sys_rect(tx + center - 1, ty + center - 1, 3, 3, t->text);
            } else if (s->pellets[y][x] == 2u) {
                int blink = ((int)(s->tick_count & 15u) < 8) ? 6 : 4;
                sys_rect(tx + center - (blink / 2), ty + center - (blink / 2), blink, blink, t->text);
            }
        }
    }

    pacman_draw_player_sprite(&board, s, t);

    for (int i = 0; i < PACMAN_GHOSTS; ++i) {
        pacman_draw_ghost_sprite(&board, s, i, t);
    }

    line[0] = '\0';
    str_copy_limited(line, "Score ", (int)sizeof(line));
    pacman_append_int(line, s->score, (int)sizeof(line));
    sys_text(stat1.x + 6, stat1.y + 7, t->text, line);

    line[0] = '\0';
    str_copy_limited(line, "Vidas ", (int)sizeof(line));
    pacman_append_int(line, s->lives, (int)sizeof(line));
    sys_text(stat2.x + 6, stat2.y + 7, t->text, line);

    line[0] = '\0';
    str_copy_limited(line, "Nivel ", (int)sizeof(line));
    pacman_append_int(line, s->level, (int)sizeof(line));
    sys_text(stat3.x + 6, stat3.y + 7, t->text, line);

    if (s->game_over) {
        sys_text(status.x + 6, status.y + 8, t->text, "GAME OVER");
        sys_text(status.x + 6, status.y + 20, t->text, "R reinicia");
    } else if (s->win) {
        sys_text(status.x + 6, status.y + 8, t->text, "LIMPOU");
        sys_text(status.x + 6, status.y + 20, t->text, "PROXIMO");
    } else {
        sys_text(help.x + 6, help.y + 6, t->text, "Setas movem");
        if (s->tick_count < s->frightened_until) {
            sys_text(status.x + 6, status.y + 8, t->text, "Fantasmas");
            sys_text(status.x + 6, status.y + 20, t->text, "assustados");
        } else {
            sys_text(help.x + 6, help.y + 20, t->text, "Coma tudo");
            sys_text(help.x + 6, help.y + 32, t->text, "do labirinto");
            sys_text(status.x + 6, status.y + 8, t->text, "Evite os");
            sys_text(status.x + 6, status.y + 20, t->text, "fantasmas");
        }
    }
}
