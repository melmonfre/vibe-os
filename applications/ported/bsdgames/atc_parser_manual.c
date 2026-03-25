#include "vibe_bsdgame_shim.h"

#include "compat/games/atc/def.h"
#include "compat/games/atc/extern.h"

typedef struct {
    int ival;
    char cval;
} atc_yystype;

enum atc_token {
    ATC_TOK_EOF = 0,
    HeightOp = 256,
    WidthOp,
    UpdateOp,
    NewplaneOp,
    DirOp,
    ConstOp,
    LineOp,
    AirportOp,
    BeaconOp,
    ExitOp
};

static atc_yystype yylval;
static int lookahead = ATC_TOK_EOF;
static int errors = 0;
int line = 1;
FILE *yyin;

static int atc_next_token(void);
static int atc_expect(int token, const char *message);
static int atc_parse_defs(void);
static int atc_parse_lines(void);
static int atc_parse_def(void);
static int atc_parse_line(void);
static int atc_parse_beacon_list(void);
static int atc_parse_exit_list(void);
static int atc_parse_airport_list(void);
static int atc_parse_segment_list(void);
static int atc_parse_beacon_point(void);
static int atc_parse_exit_point(int airport);
static int atc_parse_segment(void);

int
yylex(void)
{
    int ch;

    if (yyin == NULL) {
        return ATC_TOK_EOF;
    }

    for (;;) {
        ch = fgetc(yyin);
        if (ch == EOF) {
            return ATC_TOK_EOF;
        }
        if (ch == ' ' || ch == '\t' || ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            ++line;
            continue;
        }
        if (ch == '#') {
            do {
                ch = fgetc(yyin);
            } while (ch != EOF && ch != '\n');
            if (ch == '\n') {
                ++line;
            }
            continue;
        }
        if (isdigit((unsigned char)ch)) {
            int value = ch - '0';

            while ((ch = fgetc(yyin)) != EOF && isdigit((unsigned char)ch)) {
                value = value * 10 + (ch - '0');
            }
            if (ch != EOF) {
                ungetc(ch, yyin);
            }
            yylval.ival = value;
            return ConstOp;
        }
        if (strchr("wedcxzaq", ch) != NULL) {
            yylval.cval = (char)ch;
            return DirOp;
        }
        if (isalpha((unsigned char)ch)) {
            char word[32];
            size_t len = 0u;

            do {
                if (len + 1u < sizeof(word)) {
                    word[len++] = (char)ch;
                }
                ch = fgetc(yyin);
            } while (ch != EOF && isalpha((unsigned char)ch));
            if (ch != EOF) {
                ungetc(ch, yyin);
            }
            word[len] = '\0';

            if (strcmp(word, "height") == 0) {
                return HeightOp;
            }
            if (strcmp(word, "width") == 0) {
                return WidthOp;
            }
            if (strcmp(word, "newplane") == 0) {
                return NewplaneOp;
            }
            if (strcmp(word, "update") == 0) {
                return UpdateOp;
            }
            if (strcmp(word, "airport") == 0) {
                return AirportOp;
            }
            if (strcmp(word, "line") == 0) {
                return LineOp;
            }
            if (strcmp(word, "exit") == 0) {
                return ExitOp;
            }
            if (strcmp(word, "beacon") == 0) {
                return BeaconOp;
            }

            yyerror("Unknown keyword.");
            return ATC_TOK_EOF;
        }
        return ch;
    }
}

static int
atc_next_token(void)
{
    lookahead = yylex();
    return lookahead;
}

static int
atc_expect(int token, const char *message)
{
    if (lookahead != token) {
        yyerror(message);
        return -1;
    }
    atc_next_token();
    return 0;
}

int
yyparse(void)
{
    errors = 0;
    line = 1;
    atc_next_token();

    if (atc_parse_defs() < 0) {
        return errors;
    }
    if (checkdefs() < 0) {
        return errors;
    }
    if (atc_parse_lines() < 0) {
        return errors;
    }
    if (lookahead != ATC_TOK_EOF) {
        yyerror("Trailing input.");
    }
    if (sp->num_exits + sp->num_airports < 2) {
        yyerror("Need at least 2 airports and/or exits.");
    }
    return errors;
}

static int
atc_parse_defs(void)
{
    while (lookahead == UpdateOp || lookahead == NewplaneOp ||
           lookahead == WidthOp || lookahead == HeightOp) {
        if (atc_parse_def() < 0) {
            return -1;
        }
    }
    return 0;
}

static int
atc_parse_lines(void)
{
    while (lookahead == BeaconOp || lookahead == ExitOp ||
           lookahead == LineOp || lookahead == AirportOp) {
        if (atc_parse_line() < 0) {
            return -1;
        }
    }
    return 0;
}

static int
atc_parse_def(void)
{
    int value;

    switch (lookahead) {
        case UpdateOp:
            atc_next_token();
            if (atc_expect('=', "Expected '=' after update.") < 0 ||
                lookahead != ConstOp) {
                yyerror("Expected constant after update.");
                return -1;
            }
            value = yylval.ival;
            atc_next_token();
            if (atc_expect(';', "Expected ';' after update definition.") < 0) {
                return -1;
            }
            if (sp->update_secs != 0) {
                return yyerror("Redefinition of 'update'."), -1;
            }
            if (value < 1) {
                return yyerror("'update' is too small."), -1;
            }
            sp->update_secs = value;
            return 0;
        case NewplaneOp:
            atc_next_token();
            if (atc_expect('=', "Expected '=' after newplane.") < 0 ||
                lookahead != ConstOp) {
                yyerror("Expected constant after newplane.");
                return -1;
            }
            value = yylval.ival;
            atc_next_token();
            if (atc_expect(';', "Expected ';' after newplane definition.") < 0) {
                return -1;
            }
            if (sp->newplane_time != 0) {
                return yyerror("Redefinition of 'newplane'."), -1;
            }
            if (value < 1) {
                return yyerror("'newplane' is too small."), -1;
            }
            sp->newplane_time = value;
            return 0;
        case HeightOp:
            atc_next_token();
            if (atc_expect('=', "Expected '=' after height.") < 0 ||
                lookahead != ConstOp) {
                yyerror("Expected constant after height.");
                return -1;
            }
            value = yylval.ival;
            atc_next_token();
            if (atc_expect(';', "Expected ';' after height definition.") < 0) {
                return -1;
            }
            if (sp->height != 0) {
                return yyerror("Redefinition of 'height'."), -1;
            }
            if (value < 3) {
                return yyerror("'height' is too small."), -1;
            }
            sp->height = value;
            return 0;
        case WidthOp:
            atc_next_token();
            if (atc_expect('=', "Expected '=' after width.") < 0 ||
                lookahead != ConstOp) {
                yyerror("Expected constant after width.");
                return -1;
            }
            value = yylval.ival;
            atc_next_token();
            if (atc_expect(';', "Expected ';' after width definition.") < 0) {
                return -1;
            }
            if (sp->width != 0) {
                return yyerror("Redefinition of 'width'."), -1;
            }
            if (value < 3) {
                return yyerror("'width' is too small."), -1;
            }
            sp->width = value;
            return 0;
        default:
            yyerror("Expected a definition.");
            return -1;
    }
}

static int
atc_parse_line(void)
{
    int token = lookahead;

    atc_next_token();
    if (atc_expect(':', "Expected ':' after line type.") < 0) {
        return -1;
    }

    switch (token) {
        case BeaconOp:
            if (atc_parse_beacon_list() < 0) {
                return -1;
            }
            break;
        case ExitOp:
            if (atc_parse_exit_list() < 0) {
                return -1;
            }
            break;
        case AirportOp:
            if (atc_parse_airport_list() < 0) {
                return -1;
            }
            break;
        case LineOp:
            if (atc_parse_segment_list() < 0) {
                return -1;
            }
            break;
        default:
            yyerror("Unknown line type.");
            return -1;
    }

    return atc_expect(';', "Expected ';' after declaration.");
}

static int
atc_parse_beacon_list(void)
{
    if (lookahead != '(') {
        yyerror("Expected beacon point list.");
        return -1;
    }
    while (lookahead == '(') {
        if (atc_parse_beacon_point() < 0) {
            return -1;
        }
    }
    return 0;
}

static int
atc_parse_exit_list(void)
{
    if (lookahead != '(') {
        yyerror("Expected exit point list.");
        return -1;
    }
    while (lookahead == '(') {
        if (atc_parse_exit_point(0) < 0) {
            return -1;
        }
    }
    return 0;
}

static int
atc_parse_airport_list(void)
{
    if (lookahead != '(') {
        yyerror("Expected airport point list.");
        return -1;
    }
    while (lookahead == '(') {
        if (atc_parse_exit_point(1) < 0) {
            return -1;
        }
    }
    return 0;
}

static int
atc_parse_segment_list(void)
{
    if (lookahead != '[') {
        yyerror("Expected line segment list.");
        return -1;
    }
    while (lookahead == '[') {
        if (atc_parse_segment() < 0) {
            return -1;
        }
    }
    return 0;
}

static int
atc_parse_beacon_point(void)
{
    int x;
    int y;

    if (atc_expect('(', "Expected '(' before beacon point.") < 0 ||
        lookahead != ConstOp) {
        yyerror("Expected X coordinate for beacon.");
        return -1;
    }
    x = yylval.ival;
    atc_next_token();
    if (lookahead != ConstOp) {
        yyerror("Expected Y coordinate for beacon.");
        return -1;
    }
    y = yylval.ival;
    atc_next_token();
    if (atc_expect(')', "Expected ')' after beacon point.") < 0) {
        return -1;
    }

    if (sp->num_beacons % REALLOC == 0) {
        sp->beacon = reallocarray(sp->beacon, sp->num_beacons + REALLOC,
            sizeof(BEACON));
        if (sp->beacon == NULL) {
            yyerror("No memory available.");
            return -1;
        }
    }
    sp->beacon[sp->num_beacons].x = x;
    sp->beacon[sp->num_beacons].y = y;
    check_point(x, y);
    sp->num_beacons++;
    return 0;
}

static int
atc_parse_exit_point(int airport)
{
    int x;
    int y;
    int dir;

    if (atc_expect('(', "Expected '(' before point.") < 0 ||
        lookahead != ConstOp) {
        yyerror("Expected X coordinate.");
        return -1;
    }
    x = yylval.ival;
    atc_next_token();
    if (lookahead != ConstOp) {
        yyerror("Expected Y coordinate.");
        return -1;
    }
    y = yylval.ival;
    atc_next_token();
    if (lookahead != DirOp) {
        yyerror("Expected direction.");
        return -1;
    }
    dir = dir_no(yylval.cval);
    atc_next_token();
    if (atc_expect(')', "Expected ')' after point.") < 0) {
        return -1;
    }

    if (airport) {
        if (sp->num_airports % REALLOC == 0) {
            sp->airport = reallocarray(sp->airport, sp->num_airports + REALLOC,
                sizeof(AIRPORT));
            if (sp->airport == NULL) {
                yyerror("No memory available.");
                return -1;
            }
        }
        sp->airport[sp->num_airports].x = x;
        sp->airport[sp->num_airports].y = y;
        sp->airport[sp->num_airports].dir = dir;
        check_point(x, y);
        check_adir(x, y, dir);
        sp->num_airports++;
    } else {
        if (sp->num_exits % REALLOC == 0) {
            sp->exit = reallocarray(sp->exit, sp->num_exits + REALLOC,
                sizeof(EXIT));
            if (sp->exit == NULL) {
                yyerror("No memory available.");
                return -1;
            }
        }
        sp->exit[sp->num_exits].x = x;
        sp->exit[sp->num_exits].y = y;
        sp->exit[sp->num_exits].dir = dir;
        check_edge(x, y);
        check_edir(x, y, dir);
        sp->num_exits++;
    }
    return 0;
}

static int
atc_parse_segment(void)
{
    int x1;
    int y1;
    int x2;
    int y2;

    if (atc_expect('[', "Expected '[' before line segment.") < 0 ||
        atc_expect('(', "Expected '(' before first line point.") < 0 ||
        lookahead != ConstOp) {
        yyerror("Expected first X coordinate.");
        return -1;
    }
    x1 = yylval.ival;
    atc_next_token();
    if (lookahead != ConstOp) {
        yyerror("Expected first Y coordinate.");
        return -1;
    }
    y1 = yylval.ival;
    atc_next_token();
    if (atc_expect(')', "Expected ')' after first line point.") < 0 ||
        atc_expect('(', "Expected '(' before second line point.") < 0 ||
        lookahead != ConstOp) {
        yyerror("Expected second X coordinate.");
        return -1;
    }
    x2 = yylval.ival;
    atc_next_token();
    if (lookahead != ConstOp) {
        yyerror("Expected second Y coordinate.");
        return -1;
    }
    y2 = yylval.ival;
    atc_next_token();
    if (atc_expect(')', "Expected ')' after second line point.") < 0 ||
        atc_expect(']', "Expected ']' after line segment.") < 0) {
        return -1;
    }

    if (sp->num_lines % REALLOC == 0) {
        sp->line = reallocarray(sp->line, sp->num_lines + REALLOC,
            sizeof(LINE));
        if (sp->line == NULL) {
            yyerror("No memory available.");
            return -1;
        }
    }
    sp->line[sp->num_lines].p1.x = x1;
    sp->line[sp->num_lines].p1.y = y1;
    sp->line[sp->num_lines].p2.x = x2;
    sp->line[sp->num_lines].p2.y = y2;
    check_line(x1, y1, x2, y2);
    sp->num_lines++;
    return 0;
}

void
check_edge(int x, int y)
{
    if (!(x == 0) && !(x == sp->width - 1) &&
        !(y == 0) && !(y == sp->height - 1)) {
        yyerror("edge value not on edge.");
    }
}

void
check_point(int x, int y)
{
    if (x < 1 || x >= sp->width - 1) {
        yyerror("X value out of range.");
    }
    if (y < 1 || y >= sp->height - 1) {
        yyerror("Y value out of range.");
    }
}

void
check_linepoint(int x, int y)
{
    if (x < 0 || x >= sp->width) {
        yyerror("X value out of range.");
    }
    if (y < 0 || y >= sp->height) {
        yyerror("Y value out of range.");
    }
}

void
check_line(int x1, int y1, int x2, int y2)
{
    int d1;
    int d2;

    check_linepoint(x1, y1);
    check_linepoint(x2, y2);

    d1 = ABS(x2 - x1);
    d2 = ABS(y2 - y1);

    if (!(d1 == d2) && !(d1 == 0) && !(d2 == 0)) {
        yyerror("Bad line endpoints.");
    }
}

int
yyerror(const char *text)
{
    fprintf(stderr, "\"%s\": line %d: %s\n", file, line, text);
    ++errors;
    return errors;
}

void
check_edir(int x, int y, int dir)
{
    int bad = 0;

    if (x == sp->width - 1) {
        x = 2;
    } else if (x != 0) {
        x = 1;
    }
    if (y == sp->height - 1) {
        y = 2;
    } else if (y != 0) {
        y = 1;
    }

    switch (x * 10 + y) {
        case 0:
            if (dir != 3) {
                bad = 1;
            }
            break;
        case 1:
            if (dir < 1 || dir > 3) {
                bad = 1;
            }
            break;
        case 2:
            if (dir != 1) {
                bad = 1;
            }
            break;
        case 10:
            if (dir < 3 || dir > 5) {
                bad = 1;
            }
            break;
        case 11:
            break;
        case 12:
            if (dir > 1 && dir < 7) {
                bad = 1;
            }
            break;
        case 20:
            if (dir != 5) {
                bad = 1;
            }
            break;
        case 21:
            if (dir < 5) {
                bad = 1;
            }
            break;
        case 22:
            if (dir != 7) {
                bad = 1;
            }
            break;
        default:
            yyerror("Unknown value in checkdir!  Get help!");
            break;
    }
    if (bad) {
        yyerror("Bad direction for entrance at exit.");
    }
}

void
check_adir(int x, int y, int dir)
{
    (void)x;
    (void)y;
    (void)dir;
}

int
checkdefs(void)
{
    int err = 0;

    if (sp->width == 0) {
        yyerror("'width' undefined.");
        ++err;
    }
    if (sp->height == 0) {
        yyerror("'height' undefined.");
        ++err;
    }
    if (sp->update_secs == 0) {
        yyerror("'update' undefined.");
        ++err;
    }
    if (sp->newplane_time == 0) {
        yyerror("'newplane' undefined.");
        ++err;
    }
    return err ? -1 : 0;
}
