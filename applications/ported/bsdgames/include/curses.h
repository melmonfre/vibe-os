#ifndef VIBE_BSDGAME_CURSES_H
#define VIBE_BSDGAME_CURSES_H

#include <compat_defs.h>
#include <include/userland_api.h>
#include <stdbool.h>
#include <stdarg.h>

typedef unsigned int chtype;
typedef unsigned int mmask_t;

typedef struct {
    int id;
    int x;
    int y;
    int z;
    mmask_t bstate;
} MEVENT;

typedef struct vibe_bsdgame_window {
    int _begy;
    int _begx;
    int _maxy;
    int _maxx;
    int _cury;
    int _curx;
    int _attrs;
    int _clear;
    int _leaveok;
    int _scrollok;
    int _keypad;
    int _delay_ms;
    int _immedok;
} WINDOW;

extern WINDOW *stdscr;
extern WINDOW *curscr;
extern WINDOW *newscr;
extern int LINES;
extern int COLS;

#define OK 0
#define ERR (-1)

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define KEY_UP KEY_ARROW_UP
#define KEY_DOWN KEY_ARROW_DOWN
#define KEY_LEFT KEY_ARROW_LEFT
#define KEY_RIGHT KEY_ARROW_RIGHT
#define KEY_BACKSPACE 0x107f
#define KEY_MIN 0x1000
#define KEY_DC KEY_DELETE
#define KEY_DL KEY_DELETE
#define KEY_A1 0x1101
#define KEY_A3 0x1102
#define KEY_C1 0x1103
#define KEY_C3 0x1104
#define KEY_MOUSE 0x1105

#define BUTTON1_CLICKED 0x0001u

#define COLOR_BLACK 0
#define COLOR_RED 1
#define COLOR_GREEN 2
#define COLOR_YELLOW 3
#define COLOR_BLUE 4
#define COLOR_MAGENTA 5
#define COLOR_CYAN 6
#define COLOR_WHITE 7

#define A_NORMAL 0
#define A_BOLD 0x0200
#define A_STANDOUT 0x0100
#define A_REVERSE A_STANDOUT

#define COLOR_PAIR(n) ((n) << 8)

#define ACS_ULCORNER '+'
#define ACS_URCORNER '+'
#define ACS_LLCORNER '+'
#define ACS_LRCORNER '+'
#define ACS_HLINE '-'
#define ACS_VLINE '|'
#define ACS_PLUS '+'
#define ACS_BLOCK '#'

#define CTRL(c) ((c) & 037)

#define getyx(win, y, x) \
    do { \
        (y) = (win)->_cury; \
        (x) = (win)->_curx; \
    } while (0)

#define getmaxyx(win, y, x) \
    do { \
        (y) = (win)->_maxy; \
        (x) = (win)->_maxx; \
    } while (0)

#define getbegyx(win, y, x) \
    do { \
        (y) = (win)->_begy; \
        (x) = (win)->_begx; \
    } while (0)

#define addch(ch) waddch(stdscr, (ch))
#define mvaddch(y, x, ch) (move((y), (x)) == ERR ? ERR : addch((ch)))
#define mvwaddch(win, y, x, ch) (wmove((win), (y), (x)) == ERR ? ERR : waddch((win), (ch)))
#define addstr(str) waddstr(stdscr, (str))
#define mvaddstr(y, x, str) (move((y), (x)) == ERR ? ERR : addstr((str)))
#define mvwaddstr(win, y, x, str) (wmove((win), (y), (x)) == ERR ? ERR : waddstr((win), (str)))
#define addnstr(str, n) waddnstr(stdscr, (str), (n))
#define mvaddnstr(y, x, str, n) (move((y), (x)) == ERR ? ERR : addnstr((str), (n)))
#define mvwaddnstr(win, y, x, str, n) (wmove((win), (y), (x)) == ERR ? ERR : waddnstr((win), (str), (n)))
#define mvwprintw(win, y, x, fmt, ...) (wmove((win), (y), (x)) == ERR ? ERR : wprintw((win), (fmt), ##__VA_ARGS__))
#define getch() wgetch(stdscr)
#define mvgetch(y, x) (move((y), (x)) == ERR ? ERR : getch())
#define clear() wclear(stdscr)
#define erase() werase(stdscr)
#define clrtoeol() wclrtoeol(stdscr)
#define clrtobot() wclrtobot(stdscr)
#define hline(ch, n) whline(stdscr, (ch), (n))
#define vline(ch, n) wvline(stdscr, (ch), (n))
#define standout() wstandout(stdscr)
#define standend() wstandend(stdscr)
#define attrset(attrs) wattrset(stdscr, (attrs))
#define attron(attrs) wattron(stdscr, (attrs))
#define attroff(attrs) wattroff(stdscr, (attrs))
#define refresh() wrefresh(stdscr)
#define timeout(delay) wtimeout(stdscr, (delay))
#define nodelay(win, flag) wtimeout((win), (flag) ? 0 : -1)
#define inch() winch(stdscr)
#define winch(win) mvwinch((win), (win)->_cury, (win)->_curx)
#define mvwinch(win, y, x) (wmove((win), (y), (x)) == ERR ? (chtype)ERR : winch_at((win), (y), (x)))
#define mvprintw(y, x, fmt, ...) (move((y), (x)) == ERR ? ERR : printw((fmt), ##__VA_ARGS__))

WINDOW *initscr(void);
int endwin(void);
int isendwin(void);
WINDOW *newwin(int nlines, int ncols, int begy, int begx);
WINDOW *subwin(WINDOW *orig, int nlines, int ncols, int begy, int begx);
WINDOW *derwin(WINDOW *orig, int nlines, int ncols, int begy, int begx);
int delwin(WINDOW *win);

int move(int y, int x);
int wmove(WINDOW *win, int y, int x);
int waddch(WINDOW *win, chtype ch);
int waddstr(WINDOW *win, const char *str);
int waddnstr(WINDOW *win, const char *str, int n);
int wclear(WINDOW *win);
int werase(WINDOW *win);
int wclrtoeol(WINDOW *win);
int wclrtobot(WINDOW *win);
int whline(WINDOW *win, chtype ch, int n);
int wvline(WINDOW *win, chtype ch, int n);
int wrefresh(WINDOW *win);
int wnoutrefresh(WINDOW *win);
int wstandout(WINDOW *win);
int wstandend(WINDOW *win);
int wattrset(WINDOW *win, int attrs);
int wattron(WINDOW *win, int attrs);
int wattroff(WINDOW *win, int attrs);
int box(WINDOW *win, chtype vert, chtype hor);
int wborder(WINDOW *win, chtype ls, chtype rs, chtype ts, chtype bs,
            chtype tl, chtype tr, chtype bl, chtype br);
int touchwin(WINDOW *win);
int leaveok(WINDOW *win, bool flag);
int scrollok(WINDOW *win, bool flag);
int clearok(WINDOW *win, bool flag);
int immedok(WINDOW *win, bool flag);
int flushok(WINDOW *win, bool flag);
int keypad(WINDOW *win, bool flag);
int cbreak(void);
int nocbreak(void);
int noecho(void);
int echo(void);
int nonl(void);
int nl(void);
int raw(void);
int noraw(void);
int crmode(void);
int nocrmode(void);
int saveterm(void);
int resetterm(void);
int intrflush(WINDOW *win, bool flag);
int has_colors(void);
int start_color(void);
int init_pair(short pair, short fg, short bg);
int curs_set(int visibility);
int flushinp(void);
int wgetch(WINDOW *win);
int ungetch(int ch);
char erasechar(void);
char killchar(void);
void wtimeout(WINDOW *win, int delay);
int beep(void);
int napms(int ms);
int resizeterm(int lines, int cols);
int mvcur(int oldy, int oldx, int newy, int newx);
int printw(const char *fmt, ...);
int wprintw(WINDOW *win, const char *fmt, ...);
int vw_printw(WINDOW *win, const char *fmt, va_list ap);
int vwprintw(WINDOW *win, const char *fmt, va_list ap);
int deleteln(void);
mmask_t mousemask(mmask_t newmask, mmask_t *oldmask);
int getmouse(MEVENT *event);
int overwrite(const WINDOW *src, WINDOW *dst);
int overlay(const WINDOW *src, WINDOW *dst);
chtype winch_at(WINDOW *win, int y, int x);
char *unctrl(chtype ch);

#endif
