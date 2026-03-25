#include <userland/applications/include/taskmgr.h>
#include <userland/modules/include/ui.h>
#include <userland/modules/include/syscalls.h>

static const struct rect DEFAULT_TASKMGR_WINDOW = {30, 30, 580, 360};
static const int TASKMGR_ROW_HEIGHT = 18;
static const int TASKMGR_REFRESH_TICKS = 25;

static void append_uint(char *buf, unsigned v, int max_len) {
    char tmp[12];
    int pos = 0;

    if (max_len <= 1) {
        return;
    }
    if (v == 0u) {
        tmp[pos++] = '0';
    } else {
        while (v > 0u && pos < (int)sizeof(tmp) - 1) {
            tmp[pos++] = (char)('0' + (v % 10u));
            v /= 10u;
        }
        for (int i = 0; i < pos / 2; ++i) {
            char c = tmp[i];
            tmp[i] = tmp[pos - 1 - i];
            tmp[pos - 1 - i] = c;
        }
    }
    tmp[pos] = '\0';
    str_append(buf, tmp, max_len);
}

static void append_int(char *buf, int v, int max_len) {
    if (v < 0) {
        str_append(buf, "-", max_len);
        append_uint(buf, (unsigned)(-v), max_len);
    } else {
        append_uint(buf, (unsigned)v, max_len);
    }
}

static const char *taskmgr_window_label(enum app_type type) {
    switch (type) {
    case APP_TERMINAL: return "Terminal";
    case APP_CLOCK: return "Relogio";
    case APP_FILEMANAGER: return "Arquivos";
    case APP_EDITOR: return "Editor";
    case APP_TASKMANAGER: return "Gerenciador";
    case APP_CALCULATOR: return "Calculadora";
    case APP_SKETCHPAD: return "Sketchpad";
    case APP_SNAKE: return "Snake";
    case APP_TETRIS: return "Tetris";
    case APP_PACMAN: return "Pacman";
    case APP_SPACE_INVADERS: return "Invaders";
    case APP_PONG: return "Pong";
    case APP_DONKEY_KONG: return "Donkey Kong";
    case APP_BRICK_RACE: return "Brick Race";
    case APP_FLAP_BIRB: return "Flap Birb";
    case APP_DOOM: return "DOOM";
    case APP_CRAFT: return "Craft";
    case APP_IMAGEVIEWER: return "Imagens";
    case APP_PERSONALIZE: return "Personalizar";
    case APP_TRASH: return "Lixeira";
    default: return "Aplicacao";
    }
}

static const char *taskmgr_kind_label(uint32_t kind) {
    switch (kind) {
    case 0u: return "Usuario";
    case 1u: return "Servico";
    case 2u: return "Kernel";
    default: return "Outro";
    }
}

static const char *taskmgr_state_label(uint32_t state) {
    switch (state) {
    case 0u: return "Pronto";
    case 1u: return "Executando";
    case 2u: return "Bloqueado";
    case 3u: return "Finalizado";
    default: return "Desconhecido";
    }
}

static void taskmgr_refresh(struct taskmgr_state *tm, uint32_t ticks) {
    if (tm == 0) {
        return;
    }
    if (tm->last_refresh_ticks != 0u && (ticks - tm->last_refresh_ticks) < (uint32_t)TASKMGR_REFRESH_TICKS) {
        return;
    }

    tm->task_count = sys_task_snapshot(&tm->summary, tm->tasks, TASK_SNAPSHOT_MAX);
    if (tm->task_count < 0) {
        tm->task_count = 0;
    }
    if (tm->selected_pid != 0u) {
        int found = 0;

        for (int i = 0; i < tm->task_count; ++i) {
            if (tm->tasks[i].pid == tm->selected_pid) {
                found = 1;
                break;
            }
        }
        if (!found) {
            tm->selected_pid = 0u;
        }
    }
    tm->last_refresh_ticks = ticks;
}

void taskmgr_init_state(struct taskmgr_state *tm) {
    tm->window = DEFAULT_TASKMGR_WINDOW;
    tm->selected_tab = TASKMGR_TAB_PROCESSES;
    tm->last_refresh_ticks = 0u;
    tm->selected_pid = 0u;
    tm->task_count = 0;
}

static struct rect taskmgr_sidebar_rect(const struct taskmgr_state *tm) {
    struct rect r = {tm->window.x + 10, tm->window.y + 60, 116, tm->window.h - 72};
    return r;
}

static struct rect taskmgr_content_rect(const struct taskmgr_state *tm) {
    struct rect side = taskmgr_sidebar_rect(tm);
    struct rect r = {side.x + side.w + 10, tm->window.y + 60,
                     tm->window.w - (side.w + 30), tm->window.h - 72};
    return r;
}

static struct rect taskmgr_sidebar_button_rect(const struct taskmgr_state *tm, int index) {
    struct rect side = taskmgr_sidebar_rect(tm);
    struct rect r = {side.x + 6, side.y + 8 + (index * 22), side.w - 12, 18};
    return r;
}

static struct rect taskmgr_apps_row_rect(const struct taskmgr_state *tm, int visible_index) {
    struct rect content = taskmgr_content_rect(tm);
    struct rect r = {content.x + 8, content.y + 42 + (visible_index * TASKMGR_ROW_HEIGHT),
                     content.w - 16, TASKMGR_ROW_HEIGHT - 2};
    return r;
}

static struct rect taskmgr_apps_close_rect(const struct taskmgr_state *tm, int visible_index) {
    struct rect row = taskmgr_apps_row_rect(tm, visible_index);
    struct rect r = {row.x + row.w - 72, row.y + 1, 66, row.h - 2};
    return r;
}

static struct rect taskmgr_details_row_rect(const struct taskmgr_state *tm, int visible_index) {
    struct rect content = taskmgr_content_rect(tm);
    struct rect r = {content.x + 8, content.y + 58 + (visible_index * TASKMGR_ROW_HEIGHT),
                     content.w - 16, TASKMGR_ROW_HEIGHT - 2};
    return r;
}

static struct rect taskmgr_details_terminate_rect(const struct taskmgr_state *tm, int visible_index) {
    struct rect row = taskmgr_details_row_rect(tm, visible_index);
    struct rect r = {row.x + row.w - 72, row.y + 1, 66, row.h - 2};
    return r;
}

static void taskmgr_draw_sidebar(const struct taskmgr_state *tm) {
    static const char *labels[3] = {"Processos", "Desempenho", "Detalhes"};
    struct rect side = taskmgr_sidebar_rect(tm);

    ui_draw_surface(&side, ui_color_panel());
    for (int i = 0; i < 3; ++i) {
        struct rect button = taskmgr_sidebar_button_rect(tm, i);
        enum ui_button_style style = (tm->selected_tab == i) ? UI_BUTTON_ACTIVE : UI_BUTTON_NORMAL;

        ui_draw_button(&button, labels[i], style, 0);
    }
}

static void taskmgr_draw_header(const struct rect *content,
                                const struct desktop_theme *theme,
                                const char *title,
                                const char *subtitle) {
    struct rect hero = {content->x, content->y, content->w, 34};

    ui_draw_surface(&hero, ui_color_panel());
    sys_text(hero.x + 8, hero.y + 7, theme->text, title);
    sys_text(hero.x + 8, hero.y + 19, ui_color_muted(), subtitle);
}

static void taskmgr_draw_processes_tab(struct taskmgr_state *tm,
                                       struct window *wins,
                                       int win_count,
                                       uint32_t ticks) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect content = taskmgr_content_rect(tm);
    char subtitle[96] = "";
    int visible_index = 0;

    str_copy_limited(subtitle, "Aplicativos abertos no desktop", (int)sizeof(subtitle));
    taskmgr_draw_header(&content, theme, "Processos", subtitle);

    for (int i = 0; i < win_count; ++i) {
        struct rect row;
        struct rect close_button;
        char line[128] = "";
        unsigned uptime;

        if (!wins[i].active) {
            continue;
        }

        row = taskmgr_apps_row_rect(tm, visible_index);
        close_button = taskmgr_apps_close_rect(tm, visible_index);
        ui_draw_inset(&row, ui_color_window_bg());
        ui_draw_button(&close_button, "Encerrar", UI_BUTTON_DANGER, 0);

        str_append(line, taskmgr_window_label(wins[i].type), (int)sizeof(line));
        str_append(line, "  janela ", (int)sizeof(line));
        append_uint(line, (unsigned)i, (int)sizeof(line));
        str_append(line, "  atividade ", (int)sizeof(line));
        uptime = (ticks - wins[i].start_ticks) / 100u;
        append_uint(line, uptime, (int)sizeof(line));
        str_append(line, "s", (int)sizeof(line));
        sys_text(row.x + 6, row.y + 4, theme->text, line);
        visible_index += 1;
    }

    if (visible_index == 0) {
        sys_text(content.x + 12, content.y + 54, ui_color_muted(), "Nenhum app grafico ativo.");
    }
}

static void taskmgr_draw_performance_card(const struct rect *card,
                                          const char *title,
                                          const char *value,
                                          const char *detail) {
    const struct desktop_theme *theme = ui_theme_get();

    ui_draw_inset(card, ui_color_window_bg());
    sys_text(card->x + 8, card->y + 6, theme->text, title);
    sys_text(card->x + 8, card->y + 19, theme->text, value);
    sys_text(card->x + 8, card->y + 31, ui_color_muted(), detail);
}

static void taskmgr_draw_performance_tab(struct taskmgr_state *tm) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect content = taskmgr_content_rect(tm);
    struct rect cards[4];
    char value[64];
    char detail[96];

    taskmgr_draw_header(&content, theme, "Desempenho", "Visao geral do kernel e do escalonador");

    cards[0].x = content.x + 8;
    cards[0].y = content.y + 44;
    cards[0].w = (content.w - 20) / 2;
    cards[0].h = 48;
    cards[1].x = cards[0].x + cards[0].w + 4;
    cards[1].y = cards[0].y;
    cards[1].w = cards[0].w;
    cards[1].h = cards[0].h;
    cards[2].x = cards[0].x;
    cards[2].y = cards[0].y + 54;
    cards[2].w = cards[0].w;
    cards[2].h = cards[0].h;
    cards[3].x = cards[1].x;
    cards[3].y = cards[2].y;
    cards[3].w = cards[1].w;
    cards[3].h = cards[1].h;

    value[0] = '\0';
    append_uint(value, tm->summary.cpu_count, (int)sizeof(value));
    str_copy_limited(detail, "CPUs visiveis", (int)sizeof(detail));
    str_append(detail, "  ativos ", (int)sizeof(detail));
    append_uint(detail, tm->summary.started_cpu_count, (int)sizeof(detail));
    taskmgr_draw_performance_card(&cards[0], "CPU", value, detail);

    value[0] = '\0';
    append_uint(value, tm->summary.total_tasks, (int)sizeof(value));
    str_copy_limited(detail, "prontos ", (int)sizeof(detail));
    append_uint(detail, tm->summary.ready_tasks, (int)sizeof(detail));
    str_append(detail, "  exec ", (int)sizeof(detail));
    append_uint(detail, tm->summary.running_tasks, (int)sizeof(detail));
    taskmgr_draw_performance_card(&cards[1], "Tarefas", value, detail);

    value[0] = '\0';
    append_uint(value, tm->summary.physmem_free_kb / 1024u, (int)sizeof(value));
    str_append(value, " MiB", (int)sizeof(value));
    str_copy_limited(detail, "livre de ", (int)sizeof(detail));
    append_uint(detail, tm->summary.physmem_total_kb / 1024u, (int)sizeof(detail));
    str_append(detail, " MiB fisicos", (int)sizeof(detail));
    taskmgr_draw_performance_card(&cards[2], "Memoria Fisica Livre", value, detail);

    value[0] = '\0';
    append_uint(value, tm->summary.kernel_heap_used / 1024u, (int)sizeof(value));
    str_append(value, " KiB", (int)sizeof(value));
    str_copy_limited(detail, "heap livre ", (int)sizeof(detail));
    append_uint(detail, tm->summary.kernel_heap_free / 1024u, (int)sizeof(detail));
    str_append(detail, " KiB", (int)sizeof(detail));
    taskmgr_draw_performance_card(&cards[3], "Heap do Kernel", value, detail);

    value[0] = '\0';
    str_copy_limited(value, "Tempo ativo ", (int)sizeof(value));
    append_uint(value, tm->summary.uptime_ticks / 100u, (int)sizeof(value));
    str_append(value, "s", (int)sizeof(value));
    sys_text(content.x + 12, content.y + 160, theme->text, value);

    detail[0] = '\0';
    str_copy_limited(detail, "PID atual ", (int)sizeof(detail));
    append_uint(detail, tm->summary.current_pid, (int)sizeof(detail));
    str_append(detail, "  bloqueados ", (int)sizeof(detail));
    append_uint(detail, tm->summary.blocked_tasks, (int)sizeof(detail));
    sys_text(content.x + 12, content.y + 172, ui_color_muted(), detail);
}

static void taskmgr_draw_details_tab(struct taskmgr_state *tm) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect content = taskmgr_content_rect(tm);

    taskmgr_draw_header(&content, theme, "Detalhes", "PIDs reais, estado, nucleos e contadores");
    sys_text(content.x + 12, content.y + 44, ui_color_muted(), "Nome         PID  Estado       CPU  Ult  Stack  Runtime  Trocas");

    for (int i = 0; i < tm->task_count; ++i) {
        struct rect row = taskmgr_details_row_rect(tm, i);
        struct rect kill = taskmgr_details_terminate_rect(tm, i);
        char line[160] = "";
        uint8_t text_color = theme->text;

        ui_draw_inset(&row, ui_color_window_bg());
        if (tm->selected_pid == tm->tasks[i].pid) {
            ui_draw_surface(&row, ui_color_panel());
        }
        if (tm->tasks[i].pid == tm->summary.current_pid) {
            text_color = ui_color_muted();
        }

        if (tm->tasks[i].name[0] != '\0') {
            str_copy_limited(line, tm->tasks[i].name, (int)sizeof(line));
        } else {
            str_copy_limited(line, taskmgr_kind_label(tm->tasks[i].kind), (int)sizeof(line));
        }
        while (str_len(line) < 12) {
            str_append(line, " ", (int)sizeof(line));
        }
        append_uint(line, tm->tasks[i].pid, (int)sizeof(line));
        while (str_len(line) < 17) {
            str_append(line, " ", (int)sizeof(line));
        }
        str_append(line, taskmgr_state_label(tm->tasks[i].state), (int)sizeof(line));
        while (str_len(line) < 30) {
            str_append(line, " ", (int)sizeof(line));
        }
        append_int(line, tm->tasks[i].current_cpu, (int)sizeof(line));
        while (str_len(line) < 35) {
            str_append(line, " ", (int)sizeof(line));
        }
        append_int(line, tm->tasks[i].last_cpu, (int)sizeof(line));
        while (str_len(line) < 40) {
            str_append(line, " ", (int)sizeof(line));
        }
        append_uint(line, tm->tasks[i].stack_size / 1024u, (int)sizeof(line));
        str_append(line, "K ", (int)sizeof(line));
        append_uint(line, tm->tasks[i].runtime_ticks / 100u, (int)sizeof(line));
        str_append(line, "s ", (int)sizeof(line));
        append_uint(line, tm->tasks[i].context_switches, (int)sizeof(line));
        sys_text(row.x + 6, row.y + 4, text_color, line);

        if (tm->tasks[i].pid != tm->summary.current_pid &&
            (tm->tasks[i].flags & (1u << 1)) == 0u) {
            ui_draw_button(&kill, "PID", UI_BUTTON_DANGER, 0);
        } else {
            ui_draw_button(&kill, "Atual", UI_BUTTON_NORMAL, 0);
        }
    }

    if (tm->selected_pid != 0u) {
        char detail[128] = "";
        const struct task_snapshot_entry *selected = 0;

        for (int i = 0; i < tm->task_count; ++i) {
            if (tm->tasks[i].pid == tm->selected_pid) {
                selected = &tm->tasks[i];
                break;
            }
        }

        if (selected != 0) {
            str_copy_limited(detail, "Selecionado PID ", (int)sizeof(detail));
            append_uint(detail, selected->pid, (int)sizeof(detail));
            str_append(detail, "  tipo ", (int)sizeof(detail));
            str_append(detail, taskmgr_kind_label(selected->kind), (int)sizeof(detail));
            str_append(detail, "  afinidade pref ", (int)sizeof(detail));
            append_int(detail, selected->preferred_cpu, (int)sizeof(detail));
            sys_text(content.x + 12, content.y + content.h - 14, ui_color_muted(), detail);
        }
    }
}

void taskmgr_draw_window(struct taskmgr_state *tm,
                         struct window *wins,
                         int win_count,
                         uint32_t ticks,
                         int active,
                         int min_hover,
                         int max_hover,
                         int close_hover) {
    const struct desktop_theme *theme = ui_theme_get();
    struct rect body = {tm->window.x + 4, tm->window.y + 18, tm->window.w - 8, tm->window.h - 22};
    struct rect title = {tm->window.x + 10, tm->window.y + 24, tm->window.w - 20, 30};

    taskmgr_refresh(tm, ticks);
    draw_window_frame(&tm->window, "GERENCIADOR DE TAREFAS", active, min_hover, max_hover, close_hover);
    ui_draw_surface(&body, theme->window_bg);
    ui_draw_surface(&title, ui_color_panel());
    sys_text(title.x + 8, title.y + 6, theme->text, "Monitoramento e controle do sistema");
    sys_text(title.x + title.w - 120, title.y + 6, ui_color_muted(), "VibeOS Desktop");

    taskmgr_draw_sidebar(tm);
    switch (tm->selected_tab) {
    case TASKMGR_TAB_PROCESSES:
        taskmgr_draw_processes_tab(tm, wins, win_count, ticks);
        break;
    case TASKMGR_TAB_PERFORMANCE:
        taskmgr_draw_performance_tab(tm);
        break;
    case TASKMGR_TAB_DETAILS:
    default:
        taskmgr_draw_details_tab(tm);
        break;
    }
}

struct taskmgr_action taskmgr_handle_click(struct taskmgr_state *tm,
                                           const struct window *wins,
                                           int win_count,
                                           int x,
                                           int y,
                                           uint32_t ticks) {
    struct taskmgr_action action = {TASKMGR_ACTION_NONE, -1};

    taskmgr_refresh(tm, ticks);
    for (int i = 0; i < 3; ++i) {
        struct rect button = taskmgr_sidebar_button_rect(tm, i);

        if (point_in_rect(&button, x, y)) {
            tm->selected_tab = i;
            return action;
        }
    }

    if (tm->selected_tab == TASKMGR_TAB_PROCESSES) {
        int visible_index = 0;

        for (int i = 0; i < win_count; ++i) {
            struct rect row;
            struct rect close_button;

            if (!wins[i].active) {
                continue;
            }
            row = taskmgr_apps_row_rect(tm, visible_index);
            close_button = taskmgr_apps_close_rect(tm, visible_index);
            if (point_in_rect(&close_button, x, y)) {
                action.type = TASKMGR_ACTION_CLOSE_WINDOW;
                action.value = i;
                return action;
            }
            if (point_in_rect(&row, x, y)) {
                return action;
            }
            visible_index += 1;
        }
        return action;
    }

    if (tm->selected_tab == TASKMGR_TAB_DETAILS) {
        for (int i = 0; i < tm->task_count; ++i) {
            struct rect row = taskmgr_details_row_rect(tm, i);
            struct rect kill = taskmgr_details_terminate_rect(tm, i);

            if (point_in_rect(&row, x, y)) {
                tm->selected_pid = tm->tasks[i].pid;
            }
            if (tm->tasks[i].pid != tm->summary.current_pid &&
                (tm->tasks[i].flags & (1u << 1)) == 0u &&
                point_in_rect(&kill, x, y)) {
                action.type = TASKMGR_ACTION_TERMINATE_PID;
                action.value = (int)tm->tasks[i].pid;
                return action;
            }
        }
    }

    return action;
}
