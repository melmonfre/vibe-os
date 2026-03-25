#include "vibe_bsdgame_shim.h"

#include <curses.h>
#include <stdio.h>
#include <string.h>

#include "compat/games/tetris/screen.h"
#include "compat/games/tetris/tetris.h"

char *SEstr = "";
char *SOstr = "";

static int g_tetris_screen_ready = 0;
static char g_tetris_message[128];

static void
tetris_offset_to_rc(int offset, int *row_out, int *col_out)
{
	int row = offset / B_COLS;
	int col = offset % B_COLS;

	if (col <= -(B_COLS / 2)) {
		col += B_COLS;
		row -= 1;
	} else if (col >= (B_COLS / 2)) {
		col -= B_COLS;
		row += 1;
	}

	*row_out = row;
	*col_out = col;
}

static void
tetris_draw_preview(int top, int left)
{
	int blocks[4] = {0, nextshape->off[0], nextshape->off[1], nextshape->off[2]};

	mvaddstr(top, left, "Next:");
	for (int y = 0; y < 4; ++y) {
		mvaddstr(top + 1 + y, left, "        ");
	}
	if (!showpreview || !nextshape) {
		return;
	}
	for (int i = 0; i < 4; ++i) {
		int row;
		int col;

		tetris_offset_to_rc(blocks[i], &row, &col);
		mvaddstr(top + 2 + row, left + 3 + (col * 2), "[]");
	}
}

static void
tetris_draw_board(void)
{
	int top = 1;
	int left = ((Cols - ((B_COLS - 2) * 2 + 12)) / 2);

	if (left < 2) {
		left = 2;
	}

	mvaddstr(0, left, "Tetris BSD");
	mvprintw(0, left + 18, "Score: %d", score);

	for (int row = 0; row <= 20; ++row) {
		mvaddch(top + row, left, '|');
		mvaddch(top + row, left + 21, '|');
	}
	mvaddstr(top + 21, left, "+--------------------+");

	for (int row = 1; row <= 20; ++row) {
		for (int col = 1; col <= 10; ++col) {
			int index = row * B_COLS + col;

			mvaddstr(top + row - 1, left + 1 + ((col - 1) * 2),
			    board[index] ? "[]" : "  ");
		}
	}

	tetris_draw_preview(top + 2, left + 25);

	if (g_tetris_message[0] != '\0') {
		mvaddstr(Rows - 2, 2, g_tetris_message);
		clrtoeol();
	} else {
		move(Rows - 2, 0);
		clrtoeol();
	}
}

int
put(int c)
{
	return putchar(c);
}

int
tputs(const char *str, int affcnt, int (*outc)(int))
{
	(void)affcnt;
	if (!str || !outc) {
		return 0;
	}
	while (*str != '\0') {
		(void)outc((unsigned char)*str++);
	}
	return 0;
}

char *
tgoto(const char *cap, int col, int row)
{
	static char empty[1] = {0};

	(void)cap;
	(void)col;
	(void)row;
	return empty;
}

void
scr_init(void)
{
	Rows = 25;
	Cols = 80;
	SOstr = "";
	SEstr = "";
	g_tetris_message[0] = '\0';
}

void
scr_set(void)
{
	if (!g_tetris_screen_ready) {
		(void)initscr();
		g_tetris_screen_ready = 1;
	}
	Rows = LINES;
	Cols = COLS;
	(void)keypad(stdscr, TRUE);
	(void)cbreak();
	(void)noecho();
	(void)curs_set(0);
	(void)clear();
	tetris_draw_board();
	(void)refresh();
}

void
scr_end(void)
{
	if (!g_tetris_screen_ready) {
		return;
	}
	(void)curs_set(1);
	(void)endwin();
	g_tetris_screen_ready = 0;
}

void
scr_clear(void)
{
	if (!g_tetris_screen_ready) {
		return;
	}
	(void)clear();
	tetris_draw_board();
	(void)refresh();
}

void
scr_update(void)
{
	if (!g_tetris_screen_ready) {
		return;
	}
	(void)clear();
	tetris_draw_board();
	(void)refresh();
}

void
scr_msg(char *s, int set)
{
	if (set && s) {
		strlcpy(g_tetris_message, s, sizeof(g_tetris_message));
	} else {
		g_tetris_message[0] = '\0';
	}
	if (g_tetris_screen_ready) {
		move(Rows - 2, 0);
		clrtoeol();
		if (g_tetris_message[0] != '\0') {
			mvaddstr(Rows - 2, 2, g_tetris_message);
		}
		(void)refresh();
	}
}

void
stop(char *why)
{
	if (g_tetris_screen_ready) {
		scr_end();
	}
	errx(1, "aborting: %s", why ? why : "unknown error");
}
