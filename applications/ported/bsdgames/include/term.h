#ifndef VIBE_BSDGAME_TERM_H
#define VIBE_BSDGAME_TERM_H

int tputs(const char *str, int affcnt, int (*outc)(int));
char *tgoto(const char *cap, int col, int row);
int tgetent(char *bp, const char *name);
char *tgetstr(const char *id, char **area);
int tgetflag(const char *id);
int tgetnum(const char *id);

#endif
