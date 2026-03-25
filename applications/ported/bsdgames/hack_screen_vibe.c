#include "vibe_bsdgame_shim.h"

#include <lang/include/vibe_app_runtime.h>

#include "compat/games/hack/hack.h"

#define HACK_SCREEN_COLS 80
#define HACK_SCREEN_LINES 25

char *CD = "";
int CO = HACK_SCREEN_COLS;
int LI = HACK_SCREEN_LINES;

static char g_hack_screen[HACK_SCREEN_LINES][HACK_SCREEN_COLS];
static int g_hack_screen_ready = 0;

static void
hack_screen_syscall5(int num, int a, int b, int c, int d, int e)
{
    int ret;

    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                     : "memory", "cc");
    (void)ret;
}

static void
hack_screen_refresh(void)
{
    if (!g_hack_screen_ready) {
        return;
    }

    hack_screen_syscall5(13, 0, 0, 0, 0, 0);
    for (int row = 0; row < HACK_SCREEN_LINES; ++row) {
        char line[HACK_SCREEN_COLS + 1];

        memcpy(line, g_hack_screen[row], sizeof(g_hack_screen[row]));
        line[HACK_SCREEN_COLS] = '\0';
        vibe_app_console_write(line);
        if (row + 1 < HACK_SCREEN_LINES) {
            vibe_app_console_putc('\n');
        }
    }
}

static void
hack_screen_clear_buffer(void)
{
    for (int row = 0; row < HACK_SCREEN_LINES; ++row) {
        memset(g_hack_screen[row], ' ', HACK_SCREEN_COLS);
    }
}

static void
hack_screen_set_cursor(int x, int y)
{
    if (x < 1) {
        x = 1;
    }
    if (x > HACK_SCREEN_COLS) {
        x = HACK_SCREEN_COLS;
    }
    if (y < 1) {
        y = 1;
    }
    if (y > HACK_SCREEN_LINES) {
        y = HACK_SCREEN_LINES;
    }
    curx = (xchar)x;
    cury = (xchar)y;
}

static void
hack_screen_scroll_if_needed(void)
{
    if (cury <= HACK_SCREEN_LINES) {
        return;
    }

    memmove(g_hack_screen[0], g_hack_screen[1],
        (size_t)(HACK_SCREEN_LINES - 1) * HACK_SCREEN_COLS);
    memset(g_hack_screen[HACK_SCREEN_LINES - 1], ' ', HACK_SCREEN_COLS);
    cury = HACK_SCREEN_LINES;
}

static void
hack_screen_putc_internal(int ch)
{
    if (curx < 1) {
        curx = 1;
    }
    if (cury < 1) {
        cury = 1;
    }

    switch (ch) {
        case '\r':
            curx = 1;
            return;
        case '\n':
            curx = 1;
            ++cury;
            hack_screen_scroll_if_needed();
            return;
        case '\b':
            if (curx > 1) {
                --curx;
            }
            return;
        case '\t':
            do {
                hack_screen_putc_internal(' ');
            } while (((curx - 1) & 7) != 0);
            return;
        default:
            break;
    }

    if (curx > HACK_SCREEN_COLS) {
        curx = 1;
        ++cury;
        hack_screen_scroll_if_needed();
    }
    if (cury >= 1 && cury <= HACK_SCREEN_LINES &&
        curx >= 1 && curx <= HACK_SCREEN_COLS) {
        g_hack_screen[cury - 1][curx - 1] = (char)ch;
    }
    ++curx;
    if (curx > HACK_SCREEN_COLS + 1) {
        curx = HACK_SCREEN_COLS + 1;
    }
}

static int
hack_screen_write_text(const char *text)
{
    int count = 0;

    if (!text) {
        return 0;
    }
    while (*text != '\0') {
        hack_screen_putc_internal((unsigned char)*text++);
        ++count;
    }
    hack_screen_refresh();
    return count;
}

void
startup(void)
{
    g_hack_screen_ready = 1;
    hack_screen_clear_buffer();
    hack_screen_set_cursor(1, 1);
    hack_screen_refresh();
}

void
start_screen(void)
{
    g_hack_screen_ready = 1;
    hack_screen_refresh();
}

void
end_screen(void)
{
}

void
curs(int x, int y)
{
    hack_screen_set_cursor(x, y);
}

void
cl_end(void)
{
    int row = cury - 1;
    int col = curx - 1;

    if (row < 0 || row >= HACK_SCREEN_LINES) {
        return;
    }
    if (col < 0) {
        col = 0;
    }
    if (col >= HACK_SCREEN_COLS) {
        return;
    }
    memset(&g_hack_screen[row][col], ' ', (size_t)(HACK_SCREEN_COLS - col));
    hack_screen_refresh();
}

void
clr_screen(void)
{
    hack_screen_clear_buffer();
    hack_screen_set_cursor(1, 1);
    hack_screen_refresh();
}

void
home(void)
{
    hack_screen_set_cursor(1, 1);
}

void
standoutbeg(void)
{
}

void
standoutend(void)
{
}

void
backsp(void)
{
    if (curx > 1) {
        --curx;
    }
}

void
bell(void)
{
}

void
hackbell(void)
{
    bell();
}

void
cl_eos(void)
{
    int row = cury - 1;
    int col = curx - 1;

    if (row < 0) {
        row = 0;
    }
    if (row >= HACK_SCREEN_LINES) {
        return;
    }
    if (col < 0) {
        col = 0;
    }
    if (col >= HACK_SCREEN_COLS) {
        col = HACK_SCREEN_COLS - 1;
    }

    memset(&g_hack_screen[row][col], ' ', (size_t)(HACK_SCREEN_COLS - col));
    for (int y = row + 1; y < HACK_SCREEN_LINES; ++y) {
        memset(g_hack_screen[y], ' ', HACK_SCREEN_COLS);
    }
    hack_screen_refresh();
}

int
hack_screen_putchar(int ch)
{
    hack_screen_putc_internal(ch);
    hack_screen_refresh();
    return ch;
}

int
hack_screen_puts(const char *text)
{
    int count = hack_screen_write_text(text);

    hack_screen_putc_internal('\n');
    hack_screen_refresh();
    return count + 1;
}

int
hack_screen_fputs(const char *text, FILE *stream)
{
    if (stream == stdout || stream == stderr) {
        (void)hack_screen_write_text(text);
        return 0;
    }
    return fprintf(stream, "%s", text) < 0 ? EOF : 0;
}

int
hack_screen_vprintf(const char *fmt, va_list ap)
{
    char buffer[1024];
    int len = vsnprintf(buffer, sizeof(buffer), fmt, ap);

    if (len <= 0) {
        return len;
    }
    (void)hack_screen_write_text(buffer);
    return len;
}

int
hack_screen_printf(const char *fmt, ...)
{
    int len;
    va_list ap;

    va_start(ap, fmt);
    len = hack_screen_vprintf(fmt, ap);
    va_end(ap);
    return len;
}
