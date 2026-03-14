/*
 * Echo - VibeOS Ported Application
 * Minimal GNU-compatible echo implementation
 * 
 * Usage: echo [OPTION]... [STRING]...
 * 
 * Print STRING(s) to standard output.
 *
 * -n             do not output the trailing newline
 * -e             enable interpretation of backslash escapes
 * -E             disable interpretation of backslash escapes (default)
 */

#include "compat/include/compat.h"

static void print_escapes(const char *str) {
    while (*str) {
        if (*str == '\\' && *(str + 1)) {
            switch (*(str + 1)) {
                case 'n': printf("\n"); str += 2; break;
                case 't': printf("\t"); str += 2; break;
                case 'r': printf("\r"); str += 2; break;
                case 'b': printf("\b"); str += 2; break;
                case 'a': printf("\x07"); str += 2; break;
                case 'v': printf("\x0b"); str += 2; break;
                case 'f': printf("\x0c"); str += 2; break;
                case '\\': printf("\\"); str += 2; break;
                default:
                    printf("%c", *str);
                    str++;
            }
        } else {
            printf("%c", *str);
            str++;
        }
    }
}

int vibe_app_main(int argc, char **argv) {
    if (argc < 1) return 1;
    
    argc--;
    argv++;
    
    int no_newline = 0;
    int interpret = 0;
    
    /* Parse options */
    while (argc > 0 && argv[0][0] == '-' && argv[0][1] != '\0') {
        int idx = 1;
        int all_parsed = 0;
        
        while (argv[0][idx]) {
            char opt = argv[0][idx];
            
            if (opt == 'n') {
                no_newline = 1;
            } else if (opt == 'e') {
                interpret = 1;
            } else if (opt == 'E') {
                interpret = 0;
            } else if (opt == '-') {
                all_parsed = 1;
                idx++;
                break;
            }
            idx++;
        }
        
        if (all_parsed || argv[0][idx] == '\0') {
            argc--;
            argv++;
            if (all_parsed) break;
        } else {
            break;
        }
    }
    
    /* Print arguments */
    for (int i = 0; i < argc; i++) {
        if (i > 0) printf(" ");
        
        if (interpret) {
            print_escapes(argv[i]);
        } else {
            printf("%s", argv[i]);
        }
    }
    
    if (!no_newline) printf("\n");
    
    return 0;
}
