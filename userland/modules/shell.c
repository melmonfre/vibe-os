#include <userland/modules/include/shell.h>
#include <kernel/drivers/input/input.h>   /* kernel_keyboard_read */
#include <kernel/scheduler.h>              /* yield */
#include <stdint.h>

#define VGA_MEM ((volatile uint16_t *)0xB8000)
#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_ATTR 0x0F
#define LINE_MAX 128
#define ARGV_MAX 8
#define DBG_ROW (VGA_ROWS - 1)
#define DBG_BASE (DBG_ROW * VGA_COLS)

/* history stubs for busybox compatibility */
void shell_history_add(const char *line) { (void)line; }
void shell_history_print(void) { /* no-op */ }

static int cur_x = 0, cur_y = 0;
static volatile uint32_t shell_state = 0;
static volatile uint32_t shell_last_len = 0;
static volatile uint32_t shell_last_cmd0 = 0;

static char hex_digit(uint32_t value) {
    value &= 0x0Fu;
    return (char)(value < 10u ? ('0' + value) : ('A' + (value - 10u)));
}

static void debug_put(int col, char c) {
    VGA_MEM[DBG_BASE + col] = (VGA_ATTR << 8) | (uint8_t)c;
}

static void debug_clear(void) {
    for (int col = 0; col < VGA_COLS; ++col) {
        debug_put(col, ' ');
    }
}

static void debug_dump_line(const char *line, int len) {
    for (int i = 0; i < 8; ++i) {
        char c = line[i];
        debug_put(i, c == '\0' ? '.' : c);
    }
    debug_put(8, '|');
    debug_put(9, hex_digit((uint32_t)len >> 4));
    debug_put(10, hex_digit((uint32_t)len));
    debug_put(11, '|');
    debug_put(25, line[len] == '\0' ? '0' : '!');
}

static void debug_dump_argc(int argc) {
    debug_put(12, hex_digit((uint32_t)argc));
    debug_put(13, '|');
}

static void debug_dump_argv0(const char *s) {
    for (int i = 0; i < 8; ++i) {
        char c = (s != 0) ? s[i] : '\0';
        debug_put(14 + i, c == '\0' ? '.' : c);
    }
    debug_put(22, '|');
}

static void vga_putc(char c) {
    if (c == 0) return;
    if (c == '\n') {
        cur_x = 0;
        cur_y++;
    } else if (c == '\r') {
        cur_x = 0;
    } else if (c == '\b') {
        if (cur_x > 0) cur_x--;
        VGA_MEM[cur_y * VGA_COLS + cur_x] = (VGA_ATTR << 8) | ' ';
    } else {
        VGA_MEM[cur_y * VGA_COLS + cur_x] = (VGA_ATTR << 8) | (uint8_t)c;
        cur_x++;
    }
    if (cur_x >= VGA_COLS) { cur_x = 0; cur_y++; }
    if (cur_y >= VGA_ROWS) {
        for (int row = 0; row < VGA_ROWS - 1; ++row)
            for (int col = 0; col < VGA_COLS; ++col)
                VGA_MEM[row * VGA_COLS + col] = VGA_MEM[(row + 1) * VGA_COLS + col];
        for (int col = 0; col < VGA_COLS; ++col)
            VGA_MEM[(VGA_ROWS - 1) * VGA_COLS + col] = (VGA_ATTR << 8) | ' ';
        cur_y = VGA_ROWS - 1;
    }
}

static void vga_write(const char *s) { while (*s) vga_putc(*s++); }

static void prompt_print(void) {
    vga_putc('u');
    vga_putc('s');
    vga_putc('e');
    vga_putc('r');
    vga_putc('@');
    vga_putc('v');
    vga_putc('i');
    vga_putc('b');
    vga_putc('e');
    vga_putc('-');
    vga_putc('o');
    vga_putc('s');
    vga_putc(':');
    vga_putc('/');
    vga_putc(' ');
    vga_putc('%');
    vga_putc(' ');
}

static void echo_backspace(void) {
    vga_putc('\b');
    vga_putc(' ');
    vga_putc('\b');
}

static char *skip_spaces(char *s) {
    while (*s == ' ') {
        ++s;
    }
    return s;
}

static int tokenize_line(char *line, char **argv, int max_args) {
    int argc = 0;
    char *cursor = line;

    while (argc < max_args) {
        cursor = skip_spaces(cursor);
        if (*cursor == '\0') {
            break;
        }

        argv[argc++] = cursor;

        while (*cursor != '\0' && *cursor != ' ') {
            ++cursor;
        }

        if (*cursor == '\0') {
            break;
        }

        *cursor = '\0';
        ++cursor;
    }

    return argc;
}

static int is_help(const char *cmd) {
    return cmd[0] == 'h' && cmd[1] == 'e' && cmd[2] == 'l' && cmd[3] == 'p' && cmd[4] == '\0';
}

static int is_echo(const char *cmd) {
    return cmd[0] == 'e' && cmd[1] == 'c' && cmd[2] == 'h' && cmd[3] == 'o' && cmd[4] == '\0';
}

static int is_clear(const char *cmd) {
    return cmd[0] == 'c' && cmd[1] == 'l' && cmd[2] == 'e' && cmd[3] == 'a' && cmd[4] == 'r' && cmd[5] == '\0';
}

static int is_exit(const char *cmd) {
    return cmd[0] == 'e' && cmd[1] == 'x' && cmd[2] == 'i' && cmd[3] == 't' && cmd[4] == '\0';
}

static void print_help(void) {
    vga_putc('h');
    vga_putc('e');
    vga_putc('l');
    vga_putc('p');
    vga_putc(',');
    vga_putc(' ');
    vga_putc('e');
    vga_putc('c');
    vga_putc('h');
    vga_putc('o');
    vga_putc(',');
    vga_putc(' ');
    vga_putc('c');
    vga_putc('l');
    vga_putc('e');
    vga_putc('a');
    vga_putc('r');
    vga_putc(',');
    vga_putc(' ');
    vga_putc('e');
    vga_putc('x');
    vga_putc('i');
    vga_putc('t');
    vga_putc('\n');
}

static void print_unknown(void) {
    vga_putc('u');
    vga_putc('n');
    vga_putc('k');
    vga_putc('n');
    vga_putc('o');
    vga_putc('w');
    vga_putc('n');
    vga_putc('\n');
}

static void print_bye(void) {
    vga_putc('b');
    vga_putc('y');
    vga_putc('e');
    vga_putc('\n');
}

static int command_echo(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        vga_write(argv[i]);
        if (i + 1 < argc) {
            vga_putc(' ');
        }
    }
    vga_putc('\n');
    return 0;
}

static int dispatch_command(int argc, char **argv) {
    debug_put(23, 'D');

    if (argc == 0 || argv[0] == 0) {
        return 0;
    }

    if (is_help(argv[0])) {
        debug_put(24, 'H');
        shell_state = 5;
        print_help();
        return 0;
    }

    if (is_echo(argv[0])) {
        shell_state = 6;
        return command_echo(argc, argv);
    }

    if (is_clear(argv[0])) {
        shell_state = 7;
        for (int i = 0; i < VGA_COLS * VGA_ROWS; ++i)
            VGA_MEM[i] = (VGA_ATTR << 8) | ' ';
        cur_x = cur_y = 0;
        return 0;
    }

    if (is_exit(argv[0])) {
        shell_state = 8;
        return 1;
    }

    shell_state = 9;
    print_unknown();
    return 0;
}

static int read_line(char *buf, int maxlen) {
    int len = 0;
    
    for (;;) {
        int c;

        __asm__ volatile("cli" : : : "memory");
        c = kernel_keyboard_read();
        __asm__ volatile("sti" : : : "memory");

        if (c == 0) {
            yield();
            continue;
        }
        
        if (c == '\r') c = '\n';
        if (c == '\n') {
            vga_putc('\n');
            buf[len] = '\0';
            shell_state = 3;
            shell_last_len = (uint32_t)len;
            return len;
        }
        if ((c == '\b' || c == 127) && len > 0) {
            --len;
            echo_backspace();
            continue;
        }
        if (c >= 32 && c < 127 && len < maxlen - 1) {
            buf[len++] = (char)c;
            vga_putc((char)c);
        }
    }
}

void shell_start(void) {
    for (int i = 0; i < VGA_COLS * VGA_ROWS; ++i)
        VGA_MEM[i] = (VGA_ATTR << 8) | ' ';
    cur_x = cur_y = 0;
    shell_state = 1;

    char line[LINE_MAX];
    char *argv[ARGV_MAX];

    for (;;) {
        prompt_print();
        shell_state = 2;
        int len = read_line(line, LINE_MAX);
        shell_state = 4;
        if (len == 0) continue;

        debug_clear();
        debug_dump_line(line, len);

        int argc = tokenize_line(line, argv, ARGV_MAX);
        debug_dump_argc(argc);

        if (argc == 0) {
            shell_last_cmd0 = 0;
            continue;
        }

        shell_last_cmd0 = (uint32_t)(uint8_t)argv[0][0];
        debug_dump_argv0(argv[0]);

        if (dispatch_command(argc, argv) != 0) {
            break;
        }
    }
    shell_state = 10;
    print_bye();
}
