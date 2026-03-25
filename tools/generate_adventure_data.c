#include <stdio.h>
#include <stdlib.h>

static unsigned int g_state = 1u;

static void adventure_srandom(unsigned int seed) {
    g_state = seed != 0u ? seed : 1u;
}

static int adventure_random(void) {
    g_state = (g_state * 1664525u) + 1013904223u;
    return (int)(g_state & 0x7fffffffu);
}

int main(int argc, char **argv) {
    FILE *infile;
    int c;
    int line_start;
    int count;

    if (argc != 2) {
        fprintf(stderr, "usage: %s glorkz\n", argv[0]);
        return 1;
    }
    infile = fopen(argv[1], "rb");
    if (!infile) {
        perror(argv[1]);
        return 1;
    }

    puts("/* Auto-generated from compat/games/adventure/glorkz. */");
    puts("char data_file[] =");
    puts("{");

    adventure_srandom(1u);
    line_start = 1;
    count = 0;
    while ((c = fgetc(infile)) != EOF) {
        if (line_start && c == ' ') {
            c = '\t';
            do {
                c = fgetc(infile);
            } while (c == ' ');
            if (c == EOF) {
                break;
            }
            line_start = 0;
        }

        if ((count % 12) == 0) {
            printf("\t");
        }
        printf("0x%02x,", (unsigned int)(((unsigned char)c) ^ (unsigned char)adventure_random()));
        ++count;
        if ((count % 12) == 0) {
            putchar('\n');
        } else {
            putchar(' ');
        }

        if (c == '\n') {
            line_start = 1;
        } else if (c != '\t') {
            line_start = 0;
        }
    }

    if ((count % 12) != 0) {
        putchar('\n');
    }
    puts("\t0");
    puts("};");

    fclose(infile);
    return 0;
}
