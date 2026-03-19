#include <userland/applications/include/games/space_invaders.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>

static const struct rect DEFAULT_WINDOW = {40, 20, 400, 300};
static const int SI_BOARD_W = 288;
static const int SI_BOARD_H = 214;
static const int SI_INVADER_W = 14;
static const int SI_INVADER_H = 10;
static const int SI_INVADER_SPACING_X = 24;
static const int SI_INVADER_SPACING_Y = 18;
static const int SI_PLAYER_W = 24;
static const int SI_PLAYER_H = 10;
static const int SI_PLAYER_Y = 192;
static const int SI_PLAYER_MOVE_STEP = 6;
static const int SI_PLAYER_SHOT_COOLDOWN_TICKS = 14;
static const int SI_ENEMY_SHOT_COOLDOWN_TICKS = 18;
static const int SI_ENEMY_BULLET_SPEED = 3;
static const int SI_PLAYER_BULLET_SPEED = 6;
static const int SI_BASE_STEP_TICKS = 16;
static const int SI_MIN_STEP_TICKS = 5;
static const int SI_STEP_ACCEL_PER_4_KILLS = 1;
static const int SI_INVADER_DROP_STEP = 10;
static const int SI_BARRIER_W = 24;
static const int SI_BARRIER_H = 15;
static const int SI_BARRIER_Y = 156;
static const int SI_BARRIER_CELL_W = 5;
static const int SI_BARRIER_CELL_H = 5;
static const int SI_BARRIER_CELL_HP = 2;
static const int SI_START_LIVES = 3;

static void space_invaders_draw_invader_sprite(int x, int y, int row, int phase, uint8_t color) {
    int eye_y = y + 2;

    sys_rect(x + 3, y + 1, 8, 2, color);
    sys_rect(x + 2, y + 3, 10, 3, color);
    if (row < 2) {
        sys_rect(x + 3, y + 6, 2, 2, color);
        sys_rect(x + 9, y + 6, 2, 2, color);
        if (phase) {
            sys_rect(x + 1, y + 8, 2, 2, color);
            sys_rect(x + 11, y + 8, 2, 2, color);
        } else {
            sys_rect(x + 3, y + 8, 2, 2, color);
            sys_rect(x + 9, y + 8, 2, 2, color);
        }
    } else if (row < 4) {
        sys_rect(x + 2, y + 6, 2, 2, color);
        sys_rect(x + 10, y + 6, 2, 2, color);
        if (phase) {
            sys_rect(x + 1, y + 8, 2, 2, color);
            sys_rect(x + 11, y + 8, 2, 2, color);
        } else {
            sys_rect(x + 4, y + 8, 2, 2, color);
            sys_rect(x + 8, y + 8, 2, 2, color);
        }
    } else {
        sys_rect(x + 2, y + 6, 10, 1, color);
        if (phase) {
            sys_rect(x + 1, y + 8, 2, 2, color);
            sys_rect(x + 11, y + 8, 2, 2, color);
        } else {
            sys_rect(x + 4, y + 8, 2, 2, color);
            sys_rect(x + 8, y + 8, 2, 2, color);
        }
    }

    sys_rect(x + 4, eye_y + 1, 1, 1, ui_color_canvas());
    sys_rect(x + 9, eye_y + 1, 1, 1, ui_color_canvas());
}

static void space_invaders_draw_player_sprite(int x, int y, uint8_t color) {
    sys_rect(x + 10, y, 4, 2, color);
    sys_rect(x + 7, y + 2, 10, 2, color);
    sys_rect(x + 3, y + 4, 18, 3, color);
    sys_rect(x, y + 7, 24, 3, color);
}

static void space_invaders_append_int(char *buf, int value, int max_len) {
    char tmp[12];
    int pos = 0;
    int len = str_len(buf);
    unsigned uvalue = (unsigned)value;

    if (len >= max_len - 1) {
        return;
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

static int space_invaders_total_alive(const struct space_invaders_state *s) {
    int alive = 0;

    for (int y = 0; y < SI_ROWS; ++y) {
        for (int x = 0; x < SI_COLS; ++x) {
            alive += s->alive[y][x];
        }
    }
    return alive;
}

static int space_invaders_invader_left(const struct space_invaders_state *s) {
    int min_x = SI_BOARD_W;

    for (int y = 0; y < SI_ROWS; ++y) {
        for (int x = 0; x < SI_COLS; ++x) {
            if (!s->alive[y][x]) {
                continue;
            }
            int inv_x = s->invader_origin_x + (x * SI_INVADER_SPACING_X);
            if (inv_x < min_x) {
                min_x = inv_x;
            }
        }
    }
    return min_x;
}

static int space_invaders_invader_right(const struct space_invaders_state *s) {
    int max_x = 0;

    for (int y = 0; y < SI_ROWS; ++y) {
        for (int x = 0; x < SI_COLS; ++x) {
            if (!s->alive[y][x]) {
                continue;
            }
            int inv_x = s->invader_origin_x + (x * SI_INVADER_SPACING_X);
            int right = inv_x + SI_INVADER_W;
            if (right > max_x) {
                max_x = right;
            }
        }
    }
    return max_x;
}

static int space_invaders_invader_bottom(const struct space_invaders_state *s) {
    int max_y = 0;

    for (int y = 0; y < SI_ROWS; ++y) {
        for (int x = 0; x < SI_COLS; ++x) {
            if (!s->alive[y][x]) {
                continue;
            }
            int inv_y = s->invader_origin_y + (y * SI_INVADER_SPACING_Y);
            int bottom = inv_y + SI_INVADER_H;
            if (bottom > max_y) {
                max_y = bottom;
            }
        }
    }
    return max_y;
}

static int space_invaders_barrier_x(int idx) {
    int gap = 18;

    return 12 + (idx * (SI_BARRIER_W + gap));
}

static int space_invaders_hit_barrier(struct space_invaders_state *s, int px, int py) {
    for (int b = 0; b < SI_BARRIER_COUNT; ++b) {
        int bx = space_invaders_barrier_x(b);
        int by = SI_BARRIER_Y;

        if (px < bx || px >= bx + SI_BARRIER_W ||
            py < by || py >= by + SI_BARRIER_H) {
            continue;
        }
        for (int row = 0; row < SI_BARRIER_ROWS; ++row) {
            for (int col = 0; col < SI_BARRIER_COLS; ++col) {
                int cx = bx + (col * SI_BARRIER_CELL_W);
                int cy = by + (row * SI_BARRIER_CELL_H);
                if (px < cx || px >= cx + SI_BARRIER_CELL_W ||
                    py < cy || py >= cy + SI_BARRIER_CELL_H) {
                    continue;
                }
                if (s->barrier_hp[b][row][col] > 0) {
                    s->barrier_hp[b][row][col] -= 1;
                    return 1;
                }
            }
        }
    }
    return 0;
}

static int space_invaders_pick_shooter_col(const struct space_invaders_state *s, int seed) {
    int preferred = seed % SI_COLS;

    for (int i = 0; i < SI_COLS; ++i) {
        int col = (preferred + i) % SI_COLS;
        for (int row = SI_ROWS - 1; row >= 0; --row) {
            if (s->alive[row][col]) {
                return col;
            }
        }
    }
    return -1;
}

static void space_invaders_spawn_enemy_bullet(struct space_invaders_state *s) {
    int slot = -1;
    int shooter_col;

    for (int i = 0; i < SI_MAX_BULLETS; ++i) {
        if (!s->bullet_active[i]) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return;
    }
    shooter_col = space_invaders_pick_shooter_col(s, (int)(s->tick_count + (uint32_t)s->score));
    if (shooter_col < 0) {
        return;
    }

    for (int row = SI_ROWS - 1; row >= 0; --row) {
        if (!s->alive[row][shooter_col]) {
            continue;
        }
        s->bullet_active[slot] = 1;
        s->bullet_x[slot] = s->invader_origin_x + (shooter_col * SI_INVADER_SPACING_X) + (SI_INVADER_W / 2);
        s->bullet_y[slot] = s->invader_origin_y + (row * SI_INVADER_SPACING_Y) + SI_INVADER_H;
        return;
    }
}

static void reset(struct space_invaders_state *s) {
    for (int y = 0; y < SI_ROWS; ++y) {
        for (int x = 0; x < SI_COLS; ++x) {
            s->alive[y][x] = 1;
        }
    }
    for (int i = 0; i < SI_MAX_BULLETS; ++i) {
        s->bullet_active[i] = 0;
        s->bullet_x[i] = 0;
        s->bullet_y[i] = 0;
    }
    for (int b = 0; b < SI_BARRIER_COUNT; ++b) {
        for (int row = 0; row < SI_BARRIER_ROWS; ++row) {
            for (int col = 0; col < SI_BARRIER_COLS; ++col) {
                s->barrier_hp[b][row][col] = SI_BARRIER_CELL_HP;
            }
        }
    }
    s->invader_origin_x = 10;
    s->invader_origin_y = 8;
    s->invader_dir = 1;
    s->player_x = (SI_BOARD_W - SI_PLAYER_W) / 2;
    s->player_bullet_active = 0;
    s->player_bullet_x = 0;
    s->player_bullet_y = 0;
    s->player_shot_cooldown = SI_PLAYER_SHOT_COOLDOWN_TICKS;
    s->next_player_shot_tick = 0;
    s->enemy_shot_cooldown = SI_ENEMY_SHOT_COOLDOWN_TICKS;
    s->next_enemy_shot_tick = 0;
    s->invader_step_ticks = SI_BASE_STEP_TICKS;
    s->lives = SI_START_LIVES;
    s->level = 1;
    s->score = 0;
    s->game_over = 0;
    s->win = 0;
    s->tick_count = 0u;
    s->next_tick = 0u;
}

void space_invaders_init_state(struct space_invaders_state *s) { s->window = DEFAULT_WINDOW; reset(s); }

static void space_invaders_next_wave(struct space_invaders_state *s) {
    int preserved_score = s->score;
    int preserved_lives = s->lives;
    int next_level = s->level + 1;

    reset(s);
    s->score = preserved_score;
    s->lives = preserved_lives;
    s->level = next_level;
    s->enemy_shot_cooldown = SI_ENEMY_SHOT_COOLDOWN_TICKS - (next_level > 5 ? 5 : next_level);
    if (s->enemy_shot_cooldown < 8) {
        s->enemy_shot_cooldown = 8;
    }
    s->invader_step_ticks = SI_BASE_STEP_TICKS - (next_level > 6 ? 6 : next_level);
    if (s->invader_step_ticks < SI_MIN_STEP_TICKS) {
        s->invader_step_ticks = SI_MIN_STEP_TICKS;
    }
}

int space_invaders_handle_key(struct space_invaders_state *s, int key) {
    if (s->game_over || s->win) {
        if (key == 'r' || key == 'R' || key == ' ' || key == '\n') {
            reset(s);
            return 1;
        }
        return 0;
    }

    if (key == KEY_ARROW_LEFT) {
        if (s->player_x > 0) {
            s->player_x -= SI_PLAYER_MOVE_STEP;
            if (s->player_x < 0) {
                s->player_x = 0;
            }
        }
        return 1;
    }
    if (key == KEY_ARROW_RIGHT) {
        if (s->player_x < SI_BOARD_W - SI_PLAYER_W) {
            s->player_x += SI_PLAYER_MOVE_STEP;
            if (s->player_x > SI_BOARD_W - SI_PLAYER_W) {
                s->player_x = SI_BOARD_W - SI_PLAYER_W;
            }
        }
        return 1;
    }
    if (key == ' ') {
        if (!s->player_bullet_active && (int)s->tick_count >= s->next_player_shot_tick) {
            s->player_bullet_active = 1;
            s->player_bullet_x = s->player_x + (SI_PLAYER_W / 2);
            s->player_bullet_y = SI_PLAYER_Y - 2;
            s->next_player_shot_tick = (int)s->tick_count + s->player_shot_cooldown;
            return 1;
        }
        return 0;
    }
    if (key == 'r' || key == 'R') {
        reset(s);
        return 1;
    }
    return 0;
}

int space_invaders_step(struct space_invaders_state *s, uint32_t ticks) {
    int alive_now;
    int killed;
    int left;
    int right;
    int bottom;
    (void)ticks;

    if (s->game_over || s->win) return 0;
    s->tick_count += 1u;

    if (s->player_bullet_active) {
        s->player_bullet_y -= SI_PLAYER_BULLET_SPEED;
        if (s->player_bullet_y < 0) {
            s->player_bullet_active = 0;
        } else if (space_invaders_hit_barrier(s, s->player_bullet_x, s->player_bullet_y)) {
            s->player_bullet_active = 0;
        } else {
            for (int y = 0; y < SI_ROWS && s->player_bullet_active; ++y) {
                for (int x = 0; x < SI_COLS; ++x) {
                    int ix;
                    int iy;

                    if (!s->alive[y][x]) {
                        continue;
                    }
                    ix = s->invader_origin_x + (x * SI_INVADER_SPACING_X);
                    iy = s->invader_origin_y + (y * SI_INVADER_SPACING_Y);
                    if (s->player_bullet_x >= ix && s->player_bullet_x <= ix + SI_INVADER_W &&
                        s->player_bullet_y >= iy && s->player_bullet_y <= iy + SI_INVADER_H) {
                        s->alive[y][x] = 0;
                        s->player_bullet_active = 0;
                        s->score += (y == 0) ? 30 : (y < 3 ? 20 : 10);
                        break;
                    }
                }
            }
        }
    }

    for (int i = 0; i < SI_MAX_BULLETS; ++i) {
        if (!s->bullet_active[i]) {
            continue;
        }
        s->bullet_y[i] += SI_ENEMY_BULLET_SPEED;
        if (s->bullet_y[i] >= SI_BOARD_H) {
            s->bullet_active[i] = 0;
            continue;
        }
        if (space_invaders_hit_barrier(s, s->bullet_x[i], s->bullet_y[i])) {
            s->bullet_active[i] = 0;
            continue;
        }
        if (s->bullet_y[i] >= SI_PLAYER_Y &&
            s->bullet_y[i] <= SI_PLAYER_Y + SI_PLAYER_H &&
            s->bullet_x[i] >= s->player_x &&
            s->bullet_x[i] <= s->player_x + SI_PLAYER_W) {
            s->bullet_active[i] = 0;
            s->lives -= 1;
            if (s->lives <= 0) {
                s->game_over = 1;
                return 1;
            }
        }
    }

    alive_now = space_invaders_total_alive(s);
    killed = (SI_ROWS * SI_COLS) - alive_now;
    s->invader_step_ticks = SI_BASE_STEP_TICKS - ((killed / 4) * SI_STEP_ACCEL_PER_4_KILLS);
    if (s->invader_step_ticks < SI_MIN_STEP_TICKS) {
        s->invader_step_ticks = SI_MIN_STEP_TICKS;
    }

    if (s->next_tick == 0u) {
        s->next_tick = s->tick_count + (uint32_t)s->invader_step_ticks;
    }
    if (s->tick_count >= s->next_tick) {
        left = space_invaders_invader_left(s);
        right = space_invaders_invader_right(s);

        if ((s->invader_dir < 0 && left <= 0) ||
            (s->invader_dir > 0 && right >= SI_BOARD_W - 1)) {
            s->invader_dir = -s->invader_dir;
            s->invader_origin_y += SI_INVADER_DROP_STEP;
        } else {
            s->invader_origin_x += s->invader_dir * 4;
        }
        s->next_tick = s->tick_count + (uint32_t)s->invader_step_ticks;
    }

    if (alive_now > 0 && (int)s->tick_count >= s->next_enemy_shot_tick) {
        space_invaders_spawn_enemy_bullet(s);
        s->next_enemy_shot_tick = (int)s->tick_count + s->enemy_shot_cooldown;
    }

    alive_now = space_invaders_total_alive(s);
    if (alive_now == 0) {
        space_invaders_next_wave(s);
        return 1;
    }
    bottom = space_invaders_invader_bottom(s);
    if (bottom >= SI_PLAYER_Y) {
        s->game_over = 1;
        return 1;
    }

    return 1;
}

void space_invaders_draw_window(struct space_invaders_state *s, int active, int min_hover, int max_hover, int close_hover) {
    const struct desktop_theme *t = ui_theme_get();
    struct rect body = {s->window.x + 4, s->window.y + 18, s->window.w - 8, s->window.h - 22};
    struct rect topbar = {s->window.x + 8, s->window.y + 22, s->window.w - 16, 18};
    struct rect board = {s->window.x + 10, s->window.y + 44, SI_BOARD_W, SI_BOARD_H};
    struct rect hud = {board.x + board.w + 8, board.y, s->window.w - (board.w + 22), board.h};
    char score_text[24];
    char lives_text[24];
    char level_text[24];
    struct rect stat1 = {hud.x + 4, hud.y + 10, hud.w - 8, 22};
    struct rect stat2 = {hud.x + 4, hud.y + 36, hud.w - 8, 22};
    struct rect stat3 = {hud.x + 4, hud.y + 62, hud.w - 8, 22};
    struct rect help = {hud.x + 4, hud.y + 96, hud.w - 8, 44};
    struct rect status = {hud.x + 4, hud.y + 148, hud.w - 8, 34};
    int phase = (int)((s->tick_count / 12u) & 1u);

    draw_window_frame(&s->window, "ALIENS", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&body, ui_color_canvas());
    ui_draw_surface(&topbar, ui_color_panel());
    ui_draw_inset(&board, ui_color_canvas());
    ui_draw_inset(&hud, ui_color_canvas());
    ui_draw_inset(&stat1, ui_color_canvas());
    ui_draw_inset(&stat2, ui_color_canvas());
    ui_draw_inset(&stat3, ui_color_canvas());
    ui_draw_inset(&help, ui_color_canvas());
    ui_draw_inset(&status, ui_color_canvas());
    sys_text(topbar.x + 6, topbar.y + 5, ui_color_muted(), "Arcade assault");
    sys_rect(board.x + 1, board.y + 1, board.w - 2, board.h - 2, t->background);

    for (int b = 0; b < SI_BARRIER_COUNT; ++b) {
        int bx = space_invaders_barrier_x(b);
        for (int row = 0; row < SI_BARRIER_ROWS; ++row) {
            for (int col = 0; col < SI_BARRIER_COLS; ++col) {
                uint8_t color;
                int hp = s->barrier_hp[b][row][col];
                if (hp <= 0) {
                    continue;
                }
                color = (hp == 1) ? t->menu_button_inactive : t->menu_button;
                sys_rect(board.x + bx + (col * SI_BARRIER_CELL_W),
                         board.y + SI_BARRIER_Y + (row * SI_BARRIER_CELL_H),
                         SI_BARRIER_CELL_W - 1,
                         SI_BARRIER_CELL_H - 1,
                         color);
            }
        }
    }

    for (int y = 0; y < SI_ROWS; ++y) {
        for (int x = 0; x < SI_COLS; ++x) {
            if (!s->alive[y][x]) {
                continue;
            }
            space_invaders_draw_invader_sprite(board.x + s->invader_origin_x + (x * SI_INVADER_SPACING_X),
                                               board.y + s->invader_origin_y + (y * SI_INVADER_SPACING_Y),
                                               y,
                                               phase,
                                               t->menu_button);
        }
    }

    if (s->player_bullet_active) {
        sys_rect(board.x + s->player_bullet_x, board.y + s->player_bullet_y, 2, 4, t->text);
    }
    for (int i = 0; i < SI_MAX_BULLETS; ++i) {
        if (!s->bullet_active[i]) {
            continue;
        }
        sys_rect(board.x + s->bullet_x[i], board.y + s->bullet_y[i], 2, 4, t->window);
    }

    if (!s->game_over) {
        space_invaders_draw_player_sprite(board.x + s->player_x, board.y + SI_PLAYER_Y, t->window);
    }

    str_copy_limited(score_text, "Score ", (int)sizeof(score_text));
    space_invaders_append_int(score_text, s->score, (int)sizeof(score_text));
    str_copy_limited(lives_text, "Vidas ", (int)sizeof(lives_text));
    space_invaders_append_int(lives_text, s->lives, (int)sizeof(lives_text));
    str_copy_limited(level_text, "Nivel ", (int)sizeof(level_text));
    space_invaders_append_int(level_text, s->level, (int)sizeof(level_text));
    sys_text(stat1.x + 5, stat1.y + 7, t->text, score_text);
    sys_text(stat2.x + 5, stat2.y + 7, t->text, lives_text);
    sys_text(stat3.x + 5, stat3.y + 7, t->text, level_text);
    sys_text(help.x + 5, help.y + 7, t->text, "<- -> move");
    sys_text(help.x + 5, help.y + 21, t->text, "Space atira");
    if (s->game_over) {
        sys_text(status.x + 5, status.y + 8, t->text, "Game over");
        sys_text(status.x + 5, status.y + 20, t->text, "R reinicia");
    } else if (s->level > 1) {
        sys_text(status.x + 5, status.y + 8, t->text, "Horda");
        sys_text(status.x + 5, status.y + 20, t->text, "mais rapida");
    }
}
