/*
 * tr - VibeOS port based on compat/usr.bin/tr/tr.c
 */

#include "compat/include/compat.h"
#include "extern.h"

static int g_delete_table[NCHARS];
static int g_squeeze_table[NCHARS];
static int g_translate_table[NCHARS];
static STR g_s1 = { STRING1, NORMAL, 0, OOBCH, { 0, OOBCH }, 0, 0 };
static STR g_s2 = { STRING2, NORMAL, 0, OOBCH, { 0, OOBCH }, 0, 0 };

static void tr_reset_tables(void) {
    int i;

    for (i = 0; i < NCHARS; ++i) {
        g_delete_table[i] = 0;
        g_squeeze_table[i] = 0;
        g_translate_table[i] = i;
    }
    memset(&g_s1, 0, sizeof(g_s1));
    memset(&g_s2, 0, sizeof(g_s2));
    g_s1.which = STRING1;
    g_s1.state = NORMAL;
    g_s1.lastch = OOBCH;
    g_s1.equiv[1] = OOBCH;
    g_s2.which = STRING2;
    g_s2.state = NORMAL;
    g_s2.lastch = OOBCH;
    g_s2.equiv[1] = OOBCH;
}

static void usage(void) {
    fprintf(stderr,
            "usage: tr [-Ccs] string1 string2\n"
            "       tr [-Cc] -d string1\n"
            "       tr [-Cc] -s string1\n"
            "       tr [-Cc] -ds string1 string2\n");
}

static void setup(int *table, char *arg, STR *str, int cflag) {
    int i;

    str->str = (unsigned char *)arg;
    memset(table, 0, NCHARS * sizeof(*table));
    while (next(str)) {
        table[str->lastch] = 1;
    }
    if (cflag) {
        for (i = 0; i < NCHARS; ++i) {
            table[i] = !table[i];
        }
    }
}

int vibe_app_main(int argc, char **argv) {
    int argi;
    int cflag = 0;
    int dflag = 0;
    int sflag = 0;
    int remaining_argc;
    char **remaining_argv;
    int ch;
    int lastch;
    int cnt;

    tr_reset_tables();

    for (argi = 1; argi < argc; ++argi) {
        const char *arg = argv[argi];
        int opti;

        if (arg == 0 || arg[0] != '-' || arg[1] == '\0') {
            break;
        }
        for (opti = 1; arg[opti] != '\0'; ++opti) {
            switch (arg[opti]) {
            case 'C':
            case 'c':
                cflag = 1;
                break;
            case 'd':
                dflag = 1;
                break;
            case 's':
                sflag = 1;
                break;
            default:
                usage();
                return 1;
            }
        }
    }

    remaining_argc = argc - argi;
    remaining_argv = &argv[argi];
    if (remaining_argc < 1 || remaining_argc > 2) {
        usage();
        return 1;
    }

    if (dflag && sflag) {
        if (remaining_argc != 2) {
            usage();
            return 1;
        }
        setup(g_delete_table, remaining_argv[0], &g_s1, cflag);
        setup(g_squeeze_table, remaining_argv[1], &g_s2, 0);
        for (lastch = OOBCH; (ch = getchar()) != EOF;) {
            if (!g_delete_table[ch] && (!g_squeeze_table[ch] || lastch != ch)) {
                lastch = ch;
                putchar(ch);
            }
        }
        return 0;
    }

    if (dflag) {
        if (remaining_argc != 1) {
            usage();
            return 1;
        }
        setup(g_delete_table, remaining_argv[0], &g_s1, cflag);
        while ((ch = getchar()) != EOF) {
            if (!g_delete_table[ch]) {
                putchar(ch);
            }
        }
        return 0;
    }

    if (sflag && remaining_argc == 1) {
        setup(g_squeeze_table, remaining_argv[0], &g_s1, cflag);
        for (lastch = OOBCH; (ch = getchar()) != EOF;) {
            if (!g_squeeze_table[ch] || lastch != ch) {
                lastch = ch;
                putchar(ch);
            }
        }
        return 0;
    }

    if (remaining_argc != 2) {
        usage();
        return 1;
    }

    g_s1.str = (unsigned char *)remaining_argv[0];
    g_s2.str = (unsigned char *)remaining_argv[1];

    if (cflag) {
        for (cnt = 0; cnt < NCHARS; ++cnt) {
            g_translate_table[cnt] = OOBCH;
        }
    }

    if (!next(&g_s2)) {
        fprintf(stderr, "tr: empty string2\n");
        return 1;
    }

    ch = g_s2.lastch;
    if (sflag) {
        while (next(&g_s1)) {
            g_translate_table[g_s1.lastch] = ch = g_s2.lastch;
            g_squeeze_table[ch] = 1;
            (void)next(&g_s2);
        }
    } else {
        while (next(&g_s1)) {
            g_translate_table[g_s1.lastch] = ch = g_s2.lastch;
            (void)next(&g_s2);
        }
    }

    if (cflag) {
        for (cnt = 0; cnt < NCHARS; ++cnt) {
            g_translate_table[cnt] = g_translate_table[cnt] == OOBCH ? ch : cnt;
        }
    }

    if (sflag) {
        for (lastch = OOBCH; (ch = getchar()) != EOF;) {
            ch = g_translate_table[ch];
            if (!g_squeeze_table[ch] || lastch != ch) {
                lastch = ch;
                putchar(ch);
            }
        }
    } else {
        while ((ch = getchar()) != EOF) {
            putchar(g_translate_table[ch]);
        }
    }

    return 0;
}
