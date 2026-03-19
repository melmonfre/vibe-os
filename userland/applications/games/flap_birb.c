#include <userland/applications/include/games/flap_birb.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/ui.h>

static const struct rect DEFAULT_WINDOW = {40, 20, 400, 300};
static const int FLAP_BIRB_STEP_TICKS = 4;
static const int FLAP_BIRB_BOARD_W = 188;
static const int FLAP_BIRB_BOARD_H = 144;
static const int FLAP_BIRB_GROUND_H = 16;
static const int FLAP_BIRB_PIPE_W = 18;
static const int FLAP_BIRB_BIRD_W = 12;
static const int FLAP_BIRB_BIRD_H = 10;
static const int FLAP_BIRB_BASE_SPEED = 2;
static const int FLAP_BIRB_MAX_SPEED = 5;
static const int FLAP_BIRB_BASE_GAP = 44;
static const int FLAP_BIRB_MIN_GAP = 28;

static uint32_t flap_birb_rand(struct flap_birb_state *s) {
    s->seed = s->seed * 1664525u + 1013904223u;
    return s->seed;
}

static void flap_birb_append_int(char *buf, int value, int max_len) {
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

static void flap_birb_update_difficulty(struct flap_birb_state *s) {
    int speed_step = s->score / 6;
    int gap_reduce = s->score / 4;

    s->scroll_speed = FLAP_BIRB_BASE_SPEED + speed_step;
    if (s->scroll_speed > FLAP_BIRB_MAX_SPEED) {
        s->scroll_speed = FLAP_BIRB_MAX_SPEED;
    }

    s->gap_size = FLAP_BIRB_BASE_GAP - gap_reduce;
    if (s->gap_size < FLAP_BIRB_MIN_GAP) {
        s->gap_size = FLAP_BIRB_MIN_GAP;
    }
}

static void reset(struct flap_birb_state *s) {
    s->bird_x = 42;
    s->bird_y = 52;
    s->bird_vy = 0;
    s->score = 0;
    s->game_over = 0;
    s->tick_count = 0;
    s->next_tick = 0;
    for (int i = 0; i < FLAP_MAX_PIPES; ++i) {
        s->pipes_active[i] = 0;
        s->pipes_x[i] = 0;
        s->pipes_gap_y[i] = 40;
        s->pipes_scored[i] = 0;
    }
    s->scroll_speed = FLAP_BIRB_BASE_SPEED;
    s->gap_size = FLAP_BIRB_BASE_GAP;
}

void flap_birb_init_state(struct flap_birb_state *s) {
    s->window = DEFAULT_WINDOW;
    s->best_score = 0;
    s->seed = 0xC0FFEE11u;
    reset(s);
}

int flap_birb_handle_click(struct flap_birb_state *s) {
    if (s->game_over) {
        reset(s);
    } else {
        s->bird_vy = -5;
    }
    return 1;
}

int flap_birb_handle_key(struct flap_birb_state *s, int key) {
    if (key == ' ' || key == '\n') {
        return flap_birb_handle_click(s);
    }
    if (s->game_over && (key == 'r' || key == 'R')) {
        reset(s);
        return 1;
    }
    return 0;
}

int flap_birb_step(struct flap_birb_state *s, uint32_t ticks) {
    int board_bottom = FLAP_BIRB_BOARD_H - FLAP_BIRB_GROUND_H;

    s->tick_count = ticks;
    if (s->tick_count < s->next_tick) {
        return 0;
    }
    s->next_tick = s->tick_count + FLAP_BIRB_STEP_TICKS;

    if (s->game_over) {
        return 0;
    }

    flap_birb_update_difficulty(s);
    s->bird_vy += 1;
    if (s->bird_vy > 5) {
        s->bird_vy = 5;
    }
    s->bird_y += s->bird_vy;
    if (s->bird_y < -2) {
        s->bird_y = -2;
        s->bird_vy = 0;
    }

    if ((s->tick_count % 42u) == 0u) {
        for (int i = 0; i < FLAP_MAX_PIPES; ++i) {
            if (!s->pipes_active[i]) {
                int gap_min = 18;
                int gap_max = board_bottom - s->gap_size - 18;
                s->pipes_active[i] = 1;
                s->pipes_x[i] = FLAP_BIRB_BOARD_W + 8;
                if (gap_max <= gap_min) {
                    s->pipes_gap_y[i] = gap_min;
                } else {
                    s->pipes_gap_y[i] = gap_min + (int)(flap_birb_rand(s) % (uint32_t)(gap_max - gap_min + 1));
                }
                s->pipes_scored[i] = 0;
                break;
            }
        }
    }

    for (int i = 0; i < FLAP_MAX_PIPES; ++i) {
        int bird_left;
        int bird_right;
        int bird_top;
        int bird_bottom;
        int pipe_left;
        int pipe_right;
        int gap_top;
        int gap_bottom;

        if (!s->pipes_active[i]) {
            continue;
        }
        s->pipes_x[i] -= s->scroll_speed;
        if (s->pipes_x[i] < -FLAP_BIRB_PIPE_W) {
            s->pipes_active[i] = 0;
            continue;
        }

        if (!s->pipes_scored[i] && s->bird_x > s->pipes_x[i] + FLAP_BIRB_PIPE_W) {
            s->pipes_scored[i] = 1;
            s->score += 1;
            if (s->score > s->best_score) {
                s->best_score = s->score;
            }
            flap_birb_update_difficulty(s);
        }

        bird_left = s->bird_x + 1;
        bird_right = s->bird_x + FLAP_BIRB_BIRD_W - 2;
        bird_top = s->bird_y + 1;
        bird_bottom = s->bird_y + FLAP_BIRB_BIRD_H - 2;
        pipe_left = s->pipes_x[i];
        pipe_right = s->pipes_x[i] + FLAP_BIRB_PIPE_W;
        gap_top = s->pipes_gap_y[i];
        gap_bottom = gap_top + s->gap_size;

        if (bird_right >= pipe_left && bird_left <= pipe_right) {
            if (bird_top < gap_top || bird_bottom > gap_bottom) {
                s->game_over = 1;
            }
        }
    }

    if (s->bird_y + FLAP_BIRB_BIRD_H >= board_bottom || s->bird_y < 0) {
        s->game_over = 1;
    }

    return 1;
}

void flap_birb_draw_window(struct flap_birb_state *s, int active,
                           int min_hover, int max_hover, int close_hover) {
    const struct desktop_theme *t = ui_theme_get();
    struct rect board = {s->window.x + 14, s->window.y + 42, FLAP_BIRB_BOARD_W, FLAP_BIRB_BOARD_H};
    struct rect hud_panel = {board.x + board.w + 8, board.y, s->window.w - (board.w + 20), board.h};
    int skyline_y = board.y + FLAP_BIRB_BOARD_H - FLAP_BIRB_GROUND_H;
    char score_text[64] = "Score ";
    char speed_text[32] = "Vel ";
    char best_text[32] = "Best ";

    draw_window_frame(&s->window, "FLAP BIRB", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&(struct rect){s->window.x + 4, s->window.y + 18, s->window.w - 8, s->window.h - 22}, ui_color_canvas());
    ui_draw_inset(&board, ui_color_canvas());
    ui_draw_inset(&hud_panel, ui_color_canvas());

    sys_rect(board.x + 1, board.y + 1, board.w - 2, board.h - 2, t->window);
    sys_rect(board.x + 1, board.y + 1, board.w - 2, board.h - FLAP_BIRB_GROUND_H - 2, t->background);
    sys_rect(board.x + 1, skyline_y, board.w - 2, FLAP_BIRB_GROUND_H - 1, t->menu_button_inactive);
    sys_rect(board.x + 1, skyline_y - 3, board.w - 2, 3, t->menu_button);

    for (int cloud = 0; cloud < 3; ++cloud) {
        int cx = board.x + 20 + ((int)(s->tick_count / (6u + (uint32_t)cloud * 2u)) + cloud * 46) % (board.w - 34);
        int cy = board.y + 12 + cloud * 14;
        sys_rect(cx, cy, 16, 5, t->text);
        sys_rect(cx + 3, cy - 3, 9, 3, t->text);
    }

    for (int i = 0; i < FLAP_MAX_PIPES; ++i) {
        int pipe_x;
        int gap_top;
        int gap_bottom;

        if (!s->pipes_active[i]) {
            continue;
        }

        pipe_x = board.x + s->pipes_x[i];
        gap_top = s->pipes_gap_y[i];
        gap_bottom = gap_top + s->gap_size;

        sys_rect(pipe_x, board.y, FLAP_BIRB_PIPE_W, gap_top, t->menu_button_inactive);
        sys_rect(pipe_x - 2, board.y + gap_top - 5, FLAP_BIRB_PIPE_W + 4, 5, t->menu_button);
        sys_rect(pipe_x, board.y + gap_bottom, FLAP_BIRB_PIPE_W, skyline_y - (board.y + gap_bottom), t->menu_button_inactive);
        sys_rect(pipe_x - 2, board.y + gap_bottom, FLAP_BIRB_PIPE_W + 4, 5, t->menu_button);
    }

    sys_rect(board.x + s->bird_x, board.y + s->bird_y + 2, FLAP_BIRB_BIRD_W - 1, FLAP_BIRB_BIRD_H - 2,
             s->game_over ? t->menu_button_inactive : t->menu_button);
    sys_rect(board.x + s->bird_x + 8, board.y + s->bird_y + 4, 5, 3, t->menu_button_inactive);
    sys_rect(board.x + s->bird_x + 2, board.y + s->bird_y + 3, 2, 2, t->text);
    sys_rect(board.x + s->bird_x + 9, board.y + s->bird_y + 3, 2, 2, t->text);
    sys_rect(board.x + s->bird_x - 2, board.y + s->bird_y + 4, 4, 2, t->window);

    flap_birb_append_int(score_text, s->score, (int)sizeof(score_text));
    flap_birb_append_int(speed_text, s->scroll_speed, (int)sizeof(speed_text));
    flap_birb_append_int(best_text, s->best_score, (int)sizeof(best_text));

    sys_text(hud_panel.x + 8, hud_panel.y + 10, t->text, score_text);
    sys_text(hud_panel.x + 8, hud_panel.y + 24, t->text, best_text);
    sys_text(hud_panel.x + 8, hud_panel.y + 38, t->text, speed_text);
    sys_text(hud_panel.x + 8, hud_panel.y + 58, t->text, "Espaco/Click");
    sys_text(hud_panel.x + 8, hud_panel.y + 72, t->text, "segura voo");
    if (s->game_over) {
        sys_text(hud_panel.x + 8, hud_panel.y + 94, t->text, "Bateu!");
        sys_text(hud_panel.x + 8, hud_panel.y + 108, t->text, "R reinicia");
    } else {
        sys_text(hud_panel.x + 8, hud_panel.y + 94, t->text, "Passe pelos");
        sys_text(hud_panel.x + 8, hud_panel.y + 108, t->text, "canos verdes");
    }
}
