/*
 * tr string parser - adapted from compat/usr.bin/tr/str.c
 */

#include "compat/include/compat.h"
#include "extern.h"

static int tr_failed = 0;

static int tr_isblank(int ch) {
    return ch == ' ' || ch == '\t';
}

static int tr_iscntrl(int ch) {
    return (ch >= 0 && ch < 32) || ch == 127;
}

static int tr_isgraph(int ch) {
    return ch > 32 && ch < 127;
}

static int tr_ispunct(int ch) {
    return tr_isgraph(ch) && !isalnum(ch);
}

static int tr_isxdigit(int ch) {
    return isdigit(ch) ||
           (ch >= 'a' && ch <= 'f') ||
           (ch >= 'A' && ch <= 'F');
}

static char *tr_strpbrk(const char *text, const char *accept) {
    const char *cursor;

    if (text == 0 || accept == 0) {
        return 0;
    }
    for (; *text != '\0'; ++text) {
        for (cursor = accept; *cursor != '\0'; ++cursor) {
            if (*text == *cursor) {
                return (char *)text;
            }
        }
    }
    return 0;
}

static void tr_parser_error(const char *msg, const char *detail) {
    if (tr_failed) {
        return;
    }
    fprintf(stderr, "tr: %s", msg);
    if (detail != 0 && detail[0] != '\0') {
        fprintf(stderr, " %s", detail);
    }
    fprintf(stderr, "\n");
    tr_failed = 1;
}

static int backslash(STR *s);
static int bracket(STR *s);
static void genclass(STR *s);
static void genequiv(STR *s);
static int genrange(STR *s);
static void genseq(STR *s);

int next(STR *s) {
    int ch;

    if (tr_failed || s == 0) {
        return 0;
    }

    switch (s->state) {
    case EOS:
        return 0;
    case INFINITE:
        return 1;
    case NORMAL:
        ch = *s->str;
        switch (ch) {
        case '\0':
            s->state = EOS;
            return 0;
        case '\\':
            s->lastch = backslash(s);
            break;
        case '[':
            if (bracket(s)) {
                return next(s);
            }
            s->str += 1;
            s->lastch = ch;
            break;
        default:
            s->str += 1;
            s->lastch = ch;
            break;
        }
        if (s->str[0] == '-' && genrange(s)) {
            return next(s);
        }
        return tr_failed ? 0 : 1;
    case RANGE:
        if (s->cnt-- == 0) {
            s->state = NORMAL;
            return next(s);
        }
        s->lastch += 1;
        return 1;
    case SEQUENCE:
        if (s->cnt-- == 0) {
            s->state = NORMAL;
            return next(s);
        }
        return 1;
    case SET:
        s->lastch = s->set[s->cnt++];
        if (s->lastch == OOBCH) {
            s->state = NORMAL;
            return next(s);
        }
        return 1;
    default:
        return 0;
    }
}

typedef struct {
    char *name;
    int (*func)(int);
    int *set;
} CLASS;

static CLASS classes[] = {
    { "alnum",  isalnum, 0 },
    { "alpha",  isalpha, 0 },
    { "blank",  tr_isblank, 0 },
    { "cntrl",  tr_iscntrl, 0 },
    { "digit",  isdigit, 0 },
    { "graph",  tr_isgraph, 0 },
    { "lower",  islower, 0 },
    { "print",  isprint, 0 },
    { "punct",  tr_ispunct, 0 },
    { "space",  isspace, 0 },
    { "upper",  isupper, 0 },
    { "xdigit", tr_isxdigit, 0 },
};

static int bracket(STR *s) {
    char *p;

    switch (s->str[1]) {
    case ':':
        p = strstr((char *)s->str + 2, ":]");
        if (p == 0) {
            return 0;
        }
        *p = '\0';
        s->str += 2;
        genclass(s);
        s->str = (unsigned char *)p + 2;
        return !tr_failed;
    case '=':
        p = strstr((char *)s->str + 2, "=]");
        if (p == 0) {
            return 0;
        }
        s->str += 2;
        genequiv(s);
        return !tr_failed;
    default:
        p = tr_strpbrk((char *)s->str + 2, "*]");
        if (p == 0) {
            return 0;
        }
        if (p[0] != '*' || strchr(p, ']') == 0) {
            return 0;
        }
        s->str += 1;
        genseq(s);
        return !tr_failed;
    }
}

static void genclass(STR *s) {
    CLASS key;
    CLASS *cp;
    int i;
    int len = 0;

    key.name = (char *)s->str;
    cp = 0;
    for (i = 0; i < (int)(sizeof(classes) / sizeof(classes[0])); ++i) {
        if (strcmp(key.name, classes[i].name) == 0) {
            cp = &classes[i];
            break;
        }
    }
    if (cp == 0) {
        tr_parser_error("unknown class", (char *)s->str);
        return;
    }

    if (cp->set == 0) {
        cp->set = malloc((NCHARS + 1) * (int)sizeof(*cp->set));
        if (cp->set == 0) {
            tr_parser_error("out of memory building class", cp->name);
            return;
        }
        for (i = 0; i < NCHARS; ++i) {
            if (cp->func(i)) {
                cp->set[len++] = i;
            }
        }
        cp->set[len] = OOBCH;
    }

    s->cnt = 0;
    s->state = SET;
    s->set = cp->set;
}

static void genequiv(STR *s) {
    if (*s->str == '\\') {
        s->equiv[0] = backslash(s);
        if (*s->str != '=') {
            tr_parser_error("misplaced equivalence equals sign", 0);
            return;
        }
    } else {
        s->equiv[0] = *s->str++;
    }
    if (s->str[1] != ']') {
        tr_parser_error("equivalence class must be one character", 0);
        return;
    }
    s->equiv[1] = OOBCH;
    s->str += 2;
    s->cnt = 0;
    s->state = SET;
    s->set = s->equiv;
}

static int genrange(STR *s) {
    int stop;

    if (s->str[1] == '\0') {
        return 0;
    }
    if (s->str[1] == '[' && bracket(s)) {
        return 0;
    }
    s->str += 1;
    stop = (*s->str == '\\') ? backslash(s) : *s->str++;
    if (stop < s->lastch) {
        tr_parser_error("range-end less than range-start", 0);
        return 0;
    }
    s->cnt = stop - s->lastch - 1;
    s->state = RANGE;
    return 1;
}

static void genseq(STR *s) {
    int ch;
    int count = 0;

    if (*s->str == '\\') {
        ch = backslash(s);
    } else {
        ch = *++s->str;
        s->str += 1;
    }
    if (*s->str != '*') {
        tr_parser_error("misplaced sequence asterisk", 0);
        return;
    }
    s->str += 1;
    while (*s->str >= '0' && *s->str <= '9') {
        count = (count * 10) + (*s->str - '0');
        s->str += 1;
    }
    if (*s->str != ']') {
        tr_parser_error("missing sequence terminator", 0);
        return;
    }
    s->str += 1;
    s->lastch = ch;
    s->cnt = count > 0 ? count - 1 : 0;
    s->state = count == 0 ? INFINITE : SEQUENCE;
}

static int backslash(STR *s) {
    int ch;
    int value = 0;
    int digits = 0;

    s->str += 1;
    switch (*s->str) {
    case 'a':
        s->str += 1;
        return '\a';
    case 'b':
        s->str += 1;
        return '\b';
    case 'f':
        s->str += 1;
        return '\f';
    case 'n':
        s->str += 1;
        return '\n';
    case 'r':
        s->str += 1;
        return '\r';
    case 't':
        s->str += 1;
        return '\t';
    case 'v':
        s->str += 1;
        return '\v';
    default:
        break;
    }

    while (digits < 3) {
        ch = *s->str;
        if (ch < '0' || ch > '7') {
            break;
        }
        value = (value * 8) + (ch - '0');
        digits += 1;
        s->str += 1;
    }
    if (digits > 0) {
        return value & 0xff;
    }
    if (*s->str == '\0') {
        tr_parser_error("trailing backslash", 0);
        return 0;
    }
    return *s->str++;
}
