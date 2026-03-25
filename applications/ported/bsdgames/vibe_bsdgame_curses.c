#include "vibe_bsdgame_shim.h"

#include <curses.h>
#include <lang/include/vibe_app_runtime.h>

#define BSDGAME_CURSES_MAX_LINES 25
#define BSDGAME_CURSES_MAX_COLS 80
#define BSDGAME_CURSES_UNGET_MAX 16

WINDOW *stdscr = 0;
WINDOW *curscr = 0;
WINDOW *newscr = 0;
int LINES = BSDGAME_CURSES_MAX_LINES;
int COLS = BSDGAME_CURSES_MAX_COLS;

static WINDOW g_root_window;
static int g_curses_ready = 0;
static int g_curses_echo = 1;
static int g_curses_ended = 1;
static char g_screen[BSDGAME_CURSES_MAX_LINES][BSDGAME_CURSES_MAX_COLS];
static int g_unget_queue[BSDGAME_CURSES_UNGET_MAX];
static int g_unget_count = 0;
static char g_unctrl_storage[5];

static void bsdgame_curses_syscall5(int num, int a, int b, int c, int d, int e) {
    int ret;

    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                     : "memory", "cc");
    (void)ret;
}

static void bsdgame_curses_clear_console(void) {
    bsdgame_curses_syscall5(13, 0, 0, 0, 0, 0);
}

static void bsdgame_curses_fill_region(int top, int left, int height, int width, char ch) {
    int y;

    if (top < 0) {
        height += top;
        top = 0;
    }
    if (left < 0) {
        width += left;
        left = 0;
    }
    if (top + height > LINES) {
        height = LINES - top;
    }
    if (left + width > COLS) {
        width = COLS - left;
    }
    if (height <= 0 || width <= 0) {
        return;
    }
    for (y = 0; y < height; ++y) {
        memset(&g_screen[top + y][left], ch, (size_t)width);
    }
}

static void bsdgame_curses_reset_window(WINDOW *win, int nlines, int ncols, int begy, int begx) {
    if (!win) {
        return;
    }
    win->_begy = begy;
    win->_begx = begx;
    win->_maxy = nlines;
    win->_maxx = ncols;
    win->_cury = 0;
    win->_curx = 0;
    win->_attrs = 0;
    win->_clear = 0;
    win->_leaveok = 0;
    win->_scrollok = 0;
    win->_keypad = 0;
    win->_delay_ms = -1;
    win->_immedok = 0;
}

static void bsdgame_curses_refresh_all(void) {
    int row;

    if (!g_curses_ready) {
        return;
    }
    bsdgame_curses_clear_console();
    for (row = 0; row < LINES; ++row) {
        char line[BSDGAME_CURSES_MAX_COLS + 1];

        memcpy(line, g_screen[row], (size_t)COLS);
        line[COLS] = '\0';
        vibe_app_console_write(line);
        if (row + 1 < LINES) {
            vibe_app_console_putc('\n');
        }
    }
}

static int bsdgame_curses_translate_key(int key) {
    return key == '\r' ? '\n' : key;
}

static int bsdgame_curses_poll_until(unsigned long long deadline_ms) {
    for (;;) {
        int key = vibe_app_poll_key();

        if (key != 0) {
            return bsdgame_curses_translate_key(key);
        }
        if (deadline_ms != (unsigned long long)-1 &&
            vibe_app_millis() >= deadline_ms) {
            return ERR;
        }
        vibe_app_yield();
    }
}

static int bsdgame_curses_read_key(WINDOW *win) {
    if (g_unget_count > 0) {
        return g_unget_queue[--g_unget_count];
    }
    if (!win) {
        win = stdscr;
    }
    if (!win) {
        return ERR;
    }
    if (win->_delay_ms == 0) {
        int key = vibe_app_poll_key();

        return key ? bsdgame_curses_translate_key(key) : ERR;
    }
    if (win->_delay_ms > 0) {
        return bsdgame_curses_poll_until(vibe_app_millis() + (unsigned long long)win->_delay_ms);
    }
    return bsdgame_curses_poll_until((unsigned long long)-1);
}

static void bsdgame_curses_scroll_window(WINDOW *win) {
    int y;

    if (!win || !win->_scrollok || win->_maxy <= 0 || win->_maxx <= 0) {
        return;
    }
    for (y = 1; y < win->_maxy; ++y) {
        memcpy(&g_screen[win->_begy + y - 1][win->_begx],
               &g_screen[win->_begy + y][win->_begx],
               (size_t)win->_maxx);
    }
    memset(&g_screen[win->_begy + win->_maxy - 1][win->_begx], ' ', (size_t)win->_maxx);
    win->_cury = win->_maxy - 1;
    win->_curx = 0;
}

static int bsdgame_curses_put_at(WINDOW *win, int y, int x, char ch) {
    int gy;
    int gx;

    if (!win || y < 0 || x < 0 || y >= win->_maxy || x >= win->_maxx) {
        return ERR;
    }
    gy = win->_begy + y;
    gx = win->_begx + x;
    if (gy < 0 || gx < 0 || gy >= LINES || gx >= COLS) {
        return ERR;
    }
    g_screen[gy][gx] = ch;
    return OK;
}

WINDOW *initscr(void) {
    int row;

    if (!g_curses_ready) {
        bsdgame_curses_reset_window(&g_root_window, LINES, COLS, 0, 0);
        stdscr = &g_root_window;
        curscr = &g_root_window;
        newscr = &g_root_window;
        g_curses_ready = 1;
    }
    for (row = 0; row < BSDGAME_CURSES_MAX_LINES; ++row) {
        memset(g_screen[row], ' ', (size_t)BSDGAME_CURSES_MAX_COLS);
    }
    g_curses_echo = 1;
    g_curses_ended = 0;
    bsdgame_curses_reset_window(&g_root_window, LINES, COLS, 0, 0);
    bsdgame_curses_refresh_all();
    return stdscr;
}

int endwin(void) {
    g_curses_ended = 1;
    return OK;
}

int isendwin(void) {
    return g_curses_ended;
}

WINDOW *newwin(int nlines, int ncols, int begy, int begx) {
    WINDOW *win;

    if (!g_curses_ready) {
        initscr();
    }
    if (nlines <= 0) {
        nlines = LINES - begy;
    }
    if (ncols <= 0) {
        ncols = COLS - begx;
    }
    if (begy < 0 || begx < 0 || begy >= LINES || begx >= COLS) {
        return 0;
    }
    if (begy + nlines > LINES) {
        nlines = LINES - begy;
    }
    if (begx + ncols > COLS) {
        ncols = COLS - begx;
    }
    if (nlines <= 0 || ncols <= 0) {
        return 0;
    }
    win = (WINDOW *)malloc(sizeof(*win));
    if (!win) {
        return 0;
    }
    bsdgame_curses_reset_window(win, nlines, ncols, begy, begx);
    return win;
}

WINDOW *subwin(WINDOW *orig, int nlines, int ncols, int begy, int begx) {
    (void)orig;
    return newwin(nlines, ncols, begy, begx);
}

WINDOW *derwin(WINDOW *orig, int nlines, int ncols, int begy, int begx) {
    if (!orig) {
        return 0;
    }
    return newwin(nlines, ncols, orig->_begy + begy, orig->_begx + begx);
}

int delwin(WINDOW *win) {
    if (!win || win == stdscr || win == curscr || win == newscr) {
        return OK;
    }
    free(win);
    return OK;
}

int move(int y, int x) {
    return wmove(stdscr, y, x);
}

int wmove(WINDOW *win, int y, int x) {
    if (!win || y < 0 || x < 0 || y >= win->_maxy || x >= win->_maxx) {
        return ERR;
    }
    win->_cury = y;
    win->_curx = x;
    return OK;
}

int waddch(WINDOW *win, chtype ch) {
    char out = (char)(ch & 0xFFu);

    if (!win) {
        return ERR;
    }
    if (out == '\n') {
        win->_curx = 0;
        win->_cury += 1;
        if (win->_cury >= win->_maxy) {
            bsdgame_curses_scroll_window(win);
        }
    } else if (out == '\r') {
        win->_curx = 0;
    } else if (out == '\b') {
        if (win->_curx > 0) {
            win->_curx -= 1;
        }
        (void)bsdgame_curses_put_at(win, win->_cury, win->_curx, ' ');
    } else {
        if (bsdgame_curses_put_at(win, win->_cury, win->_curx, out) != OK) {
            return ERR;
        }
        win->_curx += 1;
        if (win->_curx >= win->_maxx) {
            win->_curx = 0;
            win->_cury += 1;
            if (win->_cury >= win->_maxy) {
                bsdgame_curses_scroll_window(win);
            }
        }
    }
    if (win->_immedok) {
        bsdgame_curses_refresh_all();
    }
    return OK;
}

int waddstr(WINDOW *win, const char *str) {
    if (!win || !str) {
        return ERR;
    }
    while (*str != '\0') {
        if (waddch(win, (unsigned char)*str++) == ERR) {
            return ERR;
        }
    }
    return OK;
}

int waddnstr(WINDOW *win, const char *str, int n) {
    int i;

    if (!win || !str) {
        return ERR;
    }
    for (i = 0; i < n && str[i] != '\0'; ++i) {
        if (waddch(win, (unsigned char)str[i]) == ERR) {
            return ERR;
        }
    }
    return OK;
}

int werase(WINDOW *win) {
    if (!win) {
        return ERR;
    }
    bsdgame_curses_fill_region(win->_begy, win->_begx, win->_maxy, win->_maxx, ' ');
    win->_cury = 0;
    win->_curx = 0;
    if (win->_immedok) {
        bsdgame_curses_refresh_all();
    }
    return OK;
}

int wclear(WINDOW *win) {
    if (!win) {
        return ERR;
    }
    win->_clear = 1;
    return werase(win);
}

int wclrtoeol(WINDOW *win) {
    if (!win) {
        return ERR;
    }
    bsdgame_curses_fill_region(win->_begy + win->_cury,
                               win->_begx + win->_curx,
                               1,
                               win->_maxx - win->_curx,
                               ' ');
    return OK;
}

int wclrtobot(WINDOW *win) {
    if (!win) {
        return ERR;
    }
    if (wclrtoeol(win) == ERR) {
        return ERR;
    }
    bsdgame_curses_fill_region(win->_begy + win->_cury + 1,
                               win->_begx,
                               win->_maxy - win->_cury - 1,
                               win->_maxx,
                               ' ');
    return OK;
}

int whline(WINDOW *win, chtype ch, int n) {
    int i;

    if (!win) {
        return ERR;
    }
    for (i = 0; i < n; ++i) {
        if (waddch(win, ch) == ERR) {
            return ERR;
        }
    }
    return OK;
}

int wvline(WINDOW *win, chtype ch, int n) {
    int i;
    int start_y;
    int start_x;

    if (!win) {
        return ERR;
    }
    start_y = win->_cury;
    start_x = win->_curx;
    for (i = 0; i < n; ++i) {
        if (bsdgame_curses_put_at(win, start_y + i, start_x, (char)(ch & 0xFFu)) != OK) {
            return ERR;
        }
    }
    return OK;
}

int wrefresh(WINDOW *win) {
    (void)win;
    bsdgame_curses_refresh_all();
    return OK;
}

int wnoutrefresh(WINDOW *win) {
    (void)win;
    return OK;
}

int wstandout(WINDOW *win) {
    if (!win) {
        return ERR;
    }
    win->_attrs |= A_STANDOUT;
    return win->_attrs;
}

int wstandend(WINDOW *win) {
    if (!win) {
        return ERR;
    }
    win->_attrs &= ~A_STANDOUT;
    return win->_attrs;
}

int wattrset(WINDOW *win, int attrs) {
    if (!win) {
        return ERR;
    }
    win->_attrs = attrs;
    return OK;
}

int wattron(WINDOW *win, int attrs) {
    if (!win) {
        return ERR;
    }
    win->_attrs |= attrs;
    return OK;
}

int wattroff(WINDOW *win, int attrs) {
    if (!win) {
        return ERR;
    }
    win->_attrs &= ~attrs;
    return OK;
}

int box(WINDOW *win, chtype vert, chtype hor) {
    return wborder(win, vert, vert, hor, hor, '+', '+', '+', '+');
}

int wborder(WINDOW *win, chtype ls, chtype rs, chtype ts, chtype bs,
            chtype tl, chtype tr, chtype bl, chtype br) {
    int x;
    int y;

    if (!win || win->_maxx <= 0 || win->_maxy <= 0) {
        return ERR;
    }
    for (x = 1; x + 1 < win->_maxx; ++x) {
        (void)bsdgame_curses_put_at(win, 0, x, (char)(ts & 0xFFu));
        (void)bsdgame_curses_put_at(win, win->_maxy - 1, x, (char)(bs & 0xFFu));
    }
    for (y = 1; y + 1 < win->_maxy; ++y) {
        (void)bsdgame_curses_put_at(win, y, 0, (char)(ls & 0xFFu));
        (void)bsdgame_curses_put_at(win, y, win->_maxx - 1, (char)(rs & 0xFFu));
    }
    (void)bsdgame_curses_put_at(win, 0, 0, (char)(tl & 0xFFu));
    (void)bsdgame_curses_put_at(win, 0, win->_maxx - 1, (char)(tr & 0xFFu));
    (void)bsdgame_curses_put_at(win, win->_maxy - 1, 0, (char)(bl & 0xFFu));
    (void)bsdgame_curses_put_at(win, win->_maxy - 1, win->_maxx - 1, (char)(br & 0xFFu));
    return OK;
}

int touchwin(WINDOW *win) {
    (void)win;
    return OK;
}

int leaveok(WINDOW *win, bool flag) {
    if (!win) {
        return ERR;
    }
    win->_leaveok = flag ? 1 : 0;
    return OK;
}

int scrollok(WINDOW *win, bool flag) {
    if (!win) {
        return ERR;
    }
    win->_scrollok = flag ? 1 : 0;
    return OK;
}

int clearok(WINDOW *win, bool flag) {
    if (!win) {
        return ERR;
    }
    win->_clear = flag ? 1 : 0;
    return OK;
}

int immedok(WINDOW *win, bool flag) {
    if (!win) {
        return ERR;
    }
    win->_immedok = flag ? 1 : 0;
    return OK;
}

int flushok(WINDOW *win, bool flag) {
    return immedok(win, flag);
}

int keypad(WINDOW *win, bool flag) {
    if (!win) {
        return ERR;
    }
    win->_keypad = flag ? 1 : 0;
    return OK;
}

int cbreak(void) {
    return OK;
}

int nocbreak(void) {
    return OK;
}

int noecho(void) {
    g_curses_echo = 0;
    return OK;
}

int echo(void) {
    g_curses_echo = 1;
    return OK;
}

int nonl(void) {
    return OK;
}

int nl(void) {
    return OK;
}

int raw(void) {
    return cbreak();
}

int noraw(void) {
    return nocbreak();
}

int crmode(void) {
    return cbreak();
}

int nocrmode(void) {
    return nocbreak();
}

int saveterm(void) {
    return OK;
}

int resetterm(void) {
    return OK;
}

int intrflush(WINDOW *win, bool flag) {
    (void)win;
    (void)flag;
    return OK;
}

int has_colors(void) {
    return 0;
}

int start_color(void) {
    return OK;
}

int init_pair(short pair, short fg, short bg) {
    (void)pair;
    (void)fg;
    (void)bg;
    return OK;
}

int curs_set(int visibility) {
    (void)visibility;
    return OK;
}

int flushinp(void) {
    while (vibe_app_poll_key() != 0) {
    }
    g_unget_count = 0;
    return OK;
}

int wgetch(WINDOW *win) {
    int key = bsdgame_curses_read_key(win);

    if (key == ERR) {
        return ERR;
    }
    if (g_curses_echo && key >= 32 && key < 127) {
        (void)waddch(win ? win : stdscr, key);
        bsdgame_curses_refresh_all();
    }
    return key;
}

int ungetch(int ch) {
    if (g_unget_count >= BSDGAME_CURSES_UNGET_MAX) {
        return ERR;
    }
    g_unget_queue[g_unget_count++] = ch;
    return OK;
}

void wtimeout(WINDOW *win, int delay) {
    if (!win) {
        return;
    }
    win->_delay_ms = delay;
}

int beep(void) {
    return OK;
}

int napms(int ms) {
    if (ms < 0) {
        ms = 0;
    }
    vibe_app_sleep_ms((unsigned int)ms);
    return OK;
}

int delay_output(int ms) {
    return napms(ms);
}

int resizeterm(int lines, int cols) {
    if (lines > 0 && lines <= BSDGAME_CURSES_MAX_LINES) {
        LINES = lines;
    }
    if (cols > 0 && cols <= BSDGAME_CURSES_MAX_COLS) {
        COLS = cols;
    }
    bsdgame_curses_reset_window(&g_root_window, LINES, COLS, 0, 0);
    return OK;
}

int mvcur(int oldy, int oldx, int newy, int newx) {
    (void)oldy;
    (void)oldx;
    (void)newy;
    (void)newx;
    return OK;
}

int printw(const char *fmt, ...) {
    va_list ap;
    char buffer[512];

    va_start(ap, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);
    return waddstr(stdscr, buffer);
}

int wprintw(WINDOW *win, const char *fmt, ...) {
    va_list ap;
    char buffer[512];

    va_start(ap, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);
    return waddstr(win, buffer);
}

int vw_printw(WINDOW *win, const char *fmt, va_list ap) {
    char buffer[512];

    vsnprintf(buffer, sizeof(buffer), fmt, ap);
    return waddstr(win ? win : stdscr, buffer);
}

int vwprintw(WINDOW *win, const char *fmt, va_list ap) {
    return vw_printw(win, fmt, ap);
}

int deleteln(void) {
    int row;

    if (!stdscr || stdscr->_cury < 0 || stdscr->_cury >= stdscr->_maxy) {
        return ERR;
    }
    row = stdscr->_begy + stdscr->_cury;
    for (int y = row; y + 1 < LINES; ++y) {
        memcpy(g_screen[y], g_screen[y + 1], (size_t)COLS);
    }
    memset(g_screen[LINES - 1], ' ', (size_t)COLS);
    return OK;
}

mmask_t mousemask(mmask_t newmask, mmask_t *oldmask) {
    if (oldmask) {
        *oldmask = 0u;
    }
    return newmask;
}

int getmouse(MEVENT *event) {
    if (event) {
        memset(event, 0, sizeof(*event));
    }
    return ERR;
}

int overwrite(const WINDOW *src, WINDOW *dst) {
    int height;
    int width;

    if (!src || !dst) {
        return ERR;
    }
    height = src->_maxy < dst->_maxy ? src->_maxy : dst->_maxy;
    width = src->_maxx < dst->_maxx ? src->_maxx : dst->_maxx;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            chtype ch = winch_at((WINDOW *)src, y, x);

            if (ch == (chtype)ERR) {
                continue;
            }
            (void)bsdgame_curses_put_at(dst, y, x, (char)(ch & 0xFFu));
        }
    }
    return OK;
}

int overlay(const WINDOW *src, WINDOW *dst) {
    int height;
    int width;

    if (!src || !dst) {
        return ERR;
    }
    height = src->_maxy < dst->_maxy ? src->_maxy : dst->_maxy;
    width = src->_maxx < dst->_maxx ? src->_maxx : dst->_maxx;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            chtype ch = winch_at((WINDOW *)src, y, x);

            if (ch == (chtype)ERR || (char)(ch & 0xFFu) == ' ') {
                continue;
            }
            (void)bsdgame_curses_put_at(dst, y, x, (char)(ch & 0xFFu));
        }
    }
    return OK;
}

chtype winch_at(WINDOW *win, int y, int x) {
    int gy;
    int gx;

    if (!win || y < 0 || x < 0 || y >= win->_maxy || x >= win->_maxx) {
        return (chtype)ERR;
    }
    gy = win->_begy + y;
    gx = win->_begx + x;
    if (gy < 0 || gx < 0 || gy >= LINES || gx >= COLS) {
        return (chtype)ERR;
    }
    return (chtype)(unsigned char)g_screen[gy][gx];
}

char *unctrl(chtype ch) {
    unsigned int value = ch & 0xFFu;

    if (value < 32u) {
        g_unctrl_storage[0] = '^';
        g_unctrl_storage[1] = (char)(value + '@');
        g_unctrl_storage[2] = '\0';
    } else if (value == 127u) {
        strcpy(g_unctrl_storage, "^?");
    } else {
        g_unctrl_storage[0] = (char)value;
        g_unctrl_storage[1] = '\0';
    }
    return g_unctrl_storage;
}
