#include "vibe_bsdgame_shim.h"

#include <compat/posix/errno.h>
#include <compat/posix/fcntl.h>
#include <compat/posix/stat.h>
#include <lang/include/vibe_app_runtime.h>
#include <pwd.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <termios.h>

extern int vibe_bsdgame_main(int argc, char **argv);

#define BSDGAME_MAX_FDS 32

struct bsdgame_fd_entry {
    int used;
    int host_fd;
};

static struct bsdgame_fd_entry g_bsdgame_fds[BSDGAME_MAX_FDS];
static int g_bsdgame_pending_key = 0;
static int g_bsdgame_pending_key_valid = 0;

enum bsdgame_length_mod {
    BSDGAME_LEN_DEFAULT = 0,
    BSDGAME_LEN_HH,
    BSDGAME_LEN_H,
    BSDGAME_LEN_L,
    BSDGAME_LEN_LL,
    BSDGAME_LEN_Z,
    BSDGAME_LEN_T
};

enum bsdgame_scan_length_mod {
    BSDGAME_SCAN_LEN_DEFAULT = 0,
    BSDGAME_SCAN_LEN_HH,
    BSDGAME_SCAN_LEN_H,
    BSDGAME_SCAN_LEN_L,
    BSDGAME_SCAN_LEN_LL
};

typedef int (*bsdgame_scan_getc_fn)(void *ctx);
typedef int (*bsdgame_scan_ungetc_fn)(int ch, void *ctx);

struct bsdgame_string_scan_ctx {
    const char *cursor;
};

static int bsdgame_alloc_fd(void) {
    for (int i = 3; i < BSDGAME_MAX_FDS; ++i) {
        if (!g_bsdgame_fds[i].used) {
            return i;
        }
    }
    return -1;
}

static int bsdgame_fd_valid(int fd) {
    return fd >= 0 && fd < BSDGAME_MAX_FDS && g_bsdgame_fds[fd].used;
}

static int bsdgame_is_console_fd(int fd) {
    return fd >= STDIN_FILENO && fd <= STDERR_FILENO;
}

static const char *bsdgame_path_basename(const char *path) {
    const char *base = path;

    if (!path) {
        return "";
    }
    for (const char *cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == '/') {
            base = cursor + 1;
        }
    }
    return base;
}

static int bsdgame_fill_pending_key(unsigned long long deadline_ms) {
    if (g_bsdgame_pending_key_valid) {
        return g_bsdgame_pending_key;
    }
    for (;;) {
        int key = vibe_app_poll_key();

        if (key != 0) {
            g_bsdgame_pending_key = key;
            g_bsdgame_pending_key_valid = 1;
            return key;
        }
        if (deadline_ms != (unsigned long long)-1 &&
            vibe_app_millis() >= deadline_ms) {
            return 0;
        }
        vibe_app_yield();
    }
}

static int bsdgame_take_pending_key(void) {
    int key;

    if (!g_bsdgame_pending_key_valid) {
        return 0;
    }
    key = g_bsdgame_pending_key;
    g_bsdgame_pending_key = 0;
    g_bsdgame_pending_key_valid = 0;
    return key;
}

static int bsdgame_string_getc(void *ctx) {
    struct bsdgame_string_scan_ctx *scan = (struct bsdgame_string_scan_ctx *)ctx;

    if (!scan || !scan->cursor || *scan->cursor == '\0') {
        return EOF;
    }
    return (unsigned char)*scan->cursor++;
}

static int bsdgame_string_ungetc(int ch, void *ctx) {
    struct bsdgame_string_scan_ctx *scan = (struct bsdgame_string_scan_ctx *)ctx;

    if (!scan || !scan->cursor || ch == EOF) {
        return EOF;
    }
    if (scan->cursor == 0) {
        return EOF;
    }
    --scan->cursor;
    return ch;
}

static int bsdgame_file_getc(void *ctx) {
    return fgetc((FILE *)ctx);
}

static int bsdgame_file_ungetc(int ch, void *ctx) {
    return ungetc(ch, (FILE *)ctx);
}

static int bsdgame_scanset_contains(const char *set, char ch) {
    const unsigned char *p = (const unsigned char *)set;
    int invert = 0;
    int matched = 0;

    if (!p) {
        return 0;
    }
    if (*p == '^') {
        invert = 1;
        ++p;
    }
    while (*p != '\0' && *p != ']') {
        if (p[1] == '-' && p[2] != '\0' && p[2] != ']') {
            unsigned char start = p[0];
            unsigned char end = p[2];

            if (start <= (unsigned char)ch && (unsigned char)ch <= end) {
                matched = 1;
            }
            p += 3;
            continue;
        }
        if (*p == (unsigned char)ch) {
            matched = 1;
        }
        ++p;
    }
    return invert ? !matched : matched;
}

static int bsdgame_scan_int(unsigned long long value, int negative,
                            enum bsdgame_scan_length_mod length,
                            int suppress, int spec, va_list *ap) {
    if (suppress) {
        return 1;
    }
    if (spec == 'd' || spec == 'i') {
        long long signed_value = negative ? -(long long)value : (long long)value;

        switch (length) {
            case BSDGAME_SCAN_LEN_HH:
                *va_arg(*ap, signed char *) = (signed char)signed_value;
                break;
            case BSDGAME_SCAN_LEN_H:
                *va_arg(*ap, short *) = (short)signed_value;
                break;
            case BSDGAME_SCAN_LEN_L:
                *va_arg(*ap, long *) = (long)signed_value;
                break;
            case BSDGAME_SCAN_LEN_LL:
                *va_arg(*ap, long long *) = signed_value;
                break;
            default:
                *va_arg(*ap, int *) = (int)signed_value;
                break;
        }
    } else {
        switch (length) {
            case BSDGAME_SCAN_LEN_HH:
                *va_arg(*ap, unsigned char *) = (unsigned char)value;
                break;
            case BSDGAME_SCAN_LEN_H:
                *va_arg(*ap, unsigned short *) = (unsigned short)value;
                break;
            case BSDGAME_SCAN_LEN_L:
                *va_arg(*ap, unsigned long *) = (unsigned long)value;
                break;
            case BSDGAME_SCAN_LEN_LL:
                *va_arg(*ap, unsigned long long *) = value;
                break;
            default:
                *va_arg(*ap, unsigned int *) = (unsigned int)value;
                break;
        }
    }
    return 1;
}

static int bsdgame_vxscanf(bsdgame_scan_getc_fn get_fn,
                           bsdgame_scan_ungetc_fn unget_fn,
                           void *ctx, const char *fmt, va_list ap) {
    int assigned = 0;
    int consumed_any = 0;

    if (!get_fn || !unget_fn || !fmt) {
        return EOF;
    }

    while (*fmt != '\0') {
        if (isspace((unsigned char)*fmt)) {
            int ch;

            while (isspace((unsigned char)*fmt)) {
                ++fmt;
            }
            do {
                ch = get_fn(ctx);
                if (ch == EOF) {
                    return consumed_any || assigned > 0 ? assigned : EOF;
                }
                if (!isspace((unsigned char)ch)) {
                    (void)unget_fn(ch, ctx);
                    break;
                }
                consumed_any = 1;
            } while (1);
            continue;
        }

        if (*fmt != '%') {
            int ch = get_fn(ctx);

            if (ch == EOF) {
                return consumed_any || assigned > 0 ? assigned : EOF;
            }
            consumed_any = 1;
            if (ch != (unsigned char)*fmt) {
                (void)unget_fn(ch, ctx);
                return assigned;
            }
            ++fmt;
            continue;
        }

        ++fmt;
        if (*fmt == '%') {
            int ch = get_fn(ctx);

            if (ch == EOF) {
                return consumed_any || assigned > 0 ? assigned : EOF;
            }
            consumed_any = 1;
            if (ch != '%') {
                (void)unget_fn(ch, ctx);
                return assigned;
            }
            ++fmt;
            continue;
        }

        {
            int suppress = 0;
            int width = 0;
            enum bsdgame_scan_length_mod length = BSDGAME_SCAN_LEN_DEFAULT;
            char spec;
            int ch;

            if (*fmt == '*') {
                suppress = 1;
                ++fmt;
            }
            while (*fmt >= '0' && *fmt <= '9') {
                width = (width * 10) + (*fmt - '0');
                ++fmt;
            }
            if (fmt[0] == 'h' && fmt[1] == 'h') {
                length = BSDGAME_SCAN_LEN_HH;
                fmt += 2;
            } else if (*fmt == 'h') {
                length = BSDGAME_SCAN_LEN_H;
                ++fmt;
            } else if (fmt[0] == 'l' && fmt[1] == 'l') {
                length = BSDGAME_SCAN_LEN_LL;
                fmt += 2;
            } else if (*fmt == 'l') {
                length = BSDGAME_SCAN_LEN_L;
                ++fmt;
            }

            spec = *fmt++;
            if (spec == '\0') {
                break;
            }

            if (spec == 'd' || spec == 'i' || spec == 'u' || spec == 'x' || spec == 'o') {
                char buffer[128];
                int pos = 0;
                int base = (spec == 'o') ? 8 : ((spec == 'x') ? 16 : ((spec == 'u') ? 10 : 0));
                int negative = 0;
                unsigned long long value;
                char *endptr;

                if (width == 0) {
                    width = (int)sizeof(buffer) - 1;
                }

                do {
                    ch = get_fn(ctx);
                    if (ch == EOF) {
                        return consumed_any || assigned > 0 ? assigned : EOF;
                    }
                    consumed_any = 1;
                } while (isspace((unsigned char)ch));

                if ((ch == '+' || ch == '-') && width > 0) {
                    buffer[pos++] = (char)ch;
                    negative = (ch == '-');
                    --width;
                    ch = get_fn(ctx);
                    if (ch == EOF) {
                        return assigned;
                    }
                    consumed_any = 1;
                }

                while (ch != EOF && width > 0) {
                    int keep = 0;

                    if (spec == 'i') {
                        keep = isdigit((unsigned char)ch) ||
                               (pos > 0 && buffer[0] == '0' &&
                                (ch == 'x' || ch == 'X'));
                    } else if (spec == 'x') {
                        keep = isdigit((unsigned char)ch) ||
                               (ch >= 'a' && ch <= 'f') ||
                               (ch >= 'A' && ch <= 'F') ||
                               (pos == 1 && buffer[0] == '0' &&
                                (ch == 'x' || ch == 'X'));
                    } else if (spec == 'o') {
                        keep = (ch >= '0' && ch <= '7');
                    } else {
                        keep = isdigit((unsigned char)ch);
                    }
                    if (!keep) {
                        break;
                    }
                    buffer[pos++] = (char)ch;
                    --width;
                    ch = get_fn(ctx);
                    if (ch != EOF) {
                        consumed_any = 1;
                    }
                }
                if (ch != EOF) {
                    (void)unget_fn(ch, ctx);
                }
                buffer[pos] = '\0';
                if (pos == 0 || (pos == 1 && (buffer[0] == '+' || buffer[0] == '-'))) {
                    return assigned;
                }
                value = strtoull(buffer, &endptr, base);
                if (endptr == buffer) {
                    return assigned;
                }
                if (bsdgame_scan_int(value, negative, length, suppress, spec, &ap) == 0) {
                    return assigned;
                }
                if (!suppress) {
                    ++assigned;
                }
                continue;
            }

            if (spec == 'f' || spec == 'g' || spec == 'e') {
                char buffer[128];
                int pos = 0;
                double value;
                char *endptr;

                if (width == 0) {
                    width = (int)sizeof(buffer) - 1;
                }
                do {
                    ch = get_fn(ctx);
                    if (ch == EOF) {
                        return consumed_any || assigned > 0 ? assigned : EOF;
                    }
                    consumed_any = 1;
                } while (isspace((unsigned char)ch));

                while (ch != EOF && width > 0 &&
                       !isspace((unsigned char)ch)) {
                    buffer[pos++] = (char)ch;
                    --width;
                    ch = get_fn(ctx);
                    if (ch != EOF) {
                        consumed_any = 1;
                    }
                }
                if (ch != EOF) {
                    (void)unget_fn(ch, ctx);
                }
                buffer[pos] = '\0';
                if (pos == 0) {
                    return assigned;
                }
                value = strtod(buffer, &endptr);
                if (endptr == buffer) {
                    return assigned;
                }
                if (!suppress) {
                    if (length == BSDGAME_SCAN_LEN_L) {
                        *va_arg(ap, double *) = value;
                    } else {
                        *va_arg(ap, float *) = (float)value;
                    }
                    ++assigned;
                }
                continue;
            }

            if (spec == 's') {
                char *dst = suppress ? 0 : va_arg(ap, char *);
                int copied = 0;

                if (width == 0) {
                    width = 0x7fffffff;
                }
                do {
                    ch = get_fn(ctx);
                    if (ch == EOF) {
                        return consumed_any || assigned > 0 ? assigned : EOF;
                    }
                    consumed_any = 1;
                } while (isspace((unsigned char)ch));

                while (ch != EOF && width > 0 && !isspace((unsigned char)ch)) {
                    if (!suppress) {
                        dst[copied] = (char)ch;
                    }
                    ++copied;
                    --width;
                    ch = get_fn(ctx);
                    if (ch != EOF) {
                        consumed_any = 1;
                    }
                }
                if (ch != EOF) {
                    (void)unget_fn(ch, ctx);
                }
                if (copied == 0) {
                    return assigned;
                }
                if (!suppress) {
                    dst[copied] = '\0';
                    ++assigned;
                }
                continue;
            }

            if (spec == 'c') {
                char *dst = suppress ? 0 : va_arg(ap, char *);
                int count = width > 0 ? width : 1;

                for (int i = 0; i < count; ++i) {
                    ch = get_fn(ctx);
                    if (ch == EOF) {
                        return consumed_any || assigned > 0 ? assigned : EOF;
                    }
                    consumed_any = 1;
                    if (!suppress) {
                        dst[i] = (char)ch;
                    }
                }
                if (!suppress) {
                    ++assigned;
                }
                continue;
            }

            if (spec == '[') {
                char scanset[128];
                int scan_pos = 0;
                char *dst = suppress ? 0 : va_arg(ap, char *);
                int copied = 0;

                if (*fmt == '^' && scan_pos < (int)sizeof(scanset) - 1) {
                    scanset[scan_pos++] = *fmt++;
                }
                if (*fmt == ']' && scan_pos < (int)sizeof(scanset) - 1) {
                    scanset[scan_pos++] = *fmt++;
                }
                while (*fmt != '\0' && *fmt != ']' &&
                       scan_pos < (int)sizeof(scanset) - 1) {
                    scanset[scan_pos++] = *fmt++;
                }
                scanset[scan_pos] = '\0';
                if (*fmt == ']') {
                    ++fmt;
                }
                if (width == 0) {
                    width = 0x7fffffff;
                }

                ch = get_fn(ctx);
                if (ch == EOF) {
                    return consumed_any || assigned > 0 ? assigned : EOF;
                }
                consumed_any = 1;
                while (ch != EOF && width > 0 &&
                       bsdgame_scanset_contains(scanset, (char)ch)) {
                    if (!suppress) {
                        dst[copied] = (char)ch;
                    }
                    ++copied;
                    --width;
                    ch = get_fn(ctx);
                    if (ch != EOF) {
                        consumed_any = 1;
                    }
                }
                if (ch != EOF) {
                    (void)unget_fn(ch, ctx);
                }
                if (copied == 0) {
                    return assigned;
                }
                if (!suppress) {
                    dst[copied] = '\0';
                    ++assigned;
                }
                continue;
            }
        }
    }

    return assigned;
}

static void bsdgame_fmt_putc(char *dst, size_t size, size_t *used, char ch) {
    if (dst && size > 0u && *used + 1u < size) {
        dst[*used] = ch;
    }
    *used += 1u;
}

static void bsdgame_fmt_repeat(char *dst, size_t size, size_t *used, char ch, int count) {
    while (count-- > 0) {
        bsdgame_fmt_putc(dst, size, used, ch);
    }
}

static void bsdgame_fmt_write(char *dst, size_t size, size_t *used,
                              const char *text, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        bsdgame_fmt_putc(dst, size, used, text[i]);
    }
}

static size_t bsdgame_u64_to_text(unsigned long long value, unsigned int base,
                                  int uppercase, char *buf) {
    static const char lower_digits[] = "0123456789abcdef";
    static const char upper_digits[] = "0123456789ABCDEF";
    const char *digits = uppercase ? upper_digits : lower_digits;
    size_t len = 0u;

    if (base < 2u || base > 16u) {
        buf[0] = '0';
        return 1u;
    }
    if (value == 0u) {
        buf[len++] = '0';
    } else {
        while (value != 0u) {
            buf[len++] = digits[value % base];
            value /= base;
        }
    }
    for (size_t i = 0u; i < len / 2u; ++i) {
        char tmp = buf[i];

        buf[i] = buf[len - 1u - i];
        buf[len - 1u - i] = tmp;
    }
    return len;
}

static unsigned long long bsdgame_pow10_u64(int precision) {
    unsigned long long value = 1u;

    if (precision < 0) {
        precision = 0;
    }
    if (precision > 9) {
        precision = 9;
    }
    while (precision-- > 0) {
        value *= 10u;
    }
    return value;
}

static void bsdgame_format_string(char *dst, size_t size, size_t *used,
                                  const char *text, int width, int precision,
                                  int left_align) {
    size_t len;
    int pad;

    if (!text) {
        text = "(null)";
    }
    len = strlen(text);
    if (precision >= 0 && (size_t)precision < len) {
        len = (size_t)precision;
    }
    pad = width - (int)len;
    if (!left_align && pad > 0) {
        bsdgame_fmt_repeat(dst, size, used, ' ', pad);
    }
    bsdgame_fmt_write(dst, size, used, text, len);
    if (left_align && pad > 0) {
        bsdgame_fmt_repeat(dst, size, used, ' ', pad);
    }
}

static void bsdgame_format_char(char *dst, size_t size, size_t *used,
                                int ch, int width, int left_align) {
    int pad = width - 1;

    if (!left_align && pad > 0) {
        bsdgame_fmt_repeat(dst, size, used, ' ', pad);
    }
    bsdgame_fmt_putc(dst, size, used, (char)ch);
    if (left_align && pad > 0) {
        bsdgame_fmt_repeat(dst, size, used, ' ', pad);
    }
}

static void bsdgame_format_unsigned(char *dst, size_t size, size_t *used,
                                    unsigned long long value, unsigned int base,
                                    int uppercase, int width, int precision,
                                    int left_align, int zero_pad, int alt_form,
                                    const char *prefix_override) {
    char digits[65];
    const char *prefix = prefix_override ? prefix_override : "";
    int prefix_len = prefix_override ? (int)strlen(prefix_override) : 0;
    int digit_len;
    int zeroes;
    int spaces;
    char pad_char;

    digit_len = (int)bsdgame_u64_to_text(value, base, uppercase, digits);
    if (precision == 0 && value == 0u) {
        digit_len = 0;
    }
    if (!prefix_override && alt_form && value != 0u) {
        if (base == 8u) {
            prefix = "0";
            prefix_len = 1;
        } else if (base == 16u) {
            prefix = uppercase ? "0X" : "0x";
            prefix_len = 2;
        }
    }

    zeroes = precision > digit_len ? precision - digit_len : 0;
    pad_char = (zero_pad && !left_align && precision < 0) ? '0' : ' ';
    spaces = width - prefix_len - zeroes - digit_len;
    if (spaces < 0) {
        spaces = 0;
    }
    if (pad_char == '0') {
        zeroes += spaces;
        spaces = 0;
    }

    if (!left_align && spaces > 0) {
        bsdgame_fmt_repeat(dst, size, used, ' ', spaces);
    }
    if (prefix_len > 0) {
        bsdgame_fmt_write(dst, size, used, prefix, (size_t)prefix_len);
    }
    if (zeroes > 0) {
        bsdgame_fmt_repeat(dst, size, used, '0', zeroes);
    }
    if (digit_len > 0) {
        bsdgame_fmt_write(dst, size, used, digits, (size_t)digit_len);
    }
    if (left_align && spaces > 0) {
        bsdgame_fmt_repeat(dst, size, used, ' ', spaces);
    }
}

static void bsdgame_format_signed(char *dst, size_t size, size_t *used,
                                  long long value, int width, int precision,
                                  int left_align, int zero_pad,
                                  int plus_sign, int space_sign) {
    const char *prefix = "";
    char sign[2];
    unsigned long long magnitude;

    if (value < 0) {
        sign[0] = '-';
        sign[1] = '\0';
        prefix = sign;
        magnitude = (unsigned long long)(-(value + 1)) + 1u;
    } else {
        if (plus_sign) {
            sign[0] = '+';
            sign[1] = '\0';
            prefix = sign;
        } else if (space_sign) {
            sign[0] = ' ';
            sign[1] = '\0';
            prefix = sign;
        }
        magnitude = (unsigned long long)value;
    }
    bsdgame_format_unsigned(dst, size, used, magnitude, 10u, 0,
                            width, precision, left_align, zero_pad, 0, prefix);
}

static void bsdgame_format_double(char *dst, size_t size, size_t *used,
                                  double value, int width, int precision,
                                  int left_align, int zero_pad,
                                  int plus_sign, int space_sign,
                                  int alt_form, int trim_trailing) {
    char number[96];
    char integer_digits[32];
    char frac_digits[32];
    size_t pos = 0u;
    int negative = 0;
    unsigned long long int_part;
    unsigned long long frac_part;
    unsigned long long scale;
    int total_len;
    int pad;

    if (precision < 0) {
        precision = 6;
    }
    if (precision > 9) {
        precision = 9;
    }
    if (value < 0.0) {
        negative = 1;
        value = -value;
    }

    int_part = (unsigned long long)value;
    scale = bsdgame_pow10_u64(precision);
    frac_part = (unsigned long long)(((value - (double)int_part) * (double)scale) + 0.5);
    if (scale != 0u && frac_part >= scale) {
        ++int_part;
        frac_part -= scale;
    }

    if (negative) {
        number[pos++] = '-';
    } else if (plus_sign) {
        number[pos++] = '+';
    } else if (space_sign) {
        number[pos++] = ' ';
    }

    pos += bsdgame_u64_to_text(int_part, 10u, 0, &number[pos]);
    if (precision > 0 || alt_form) {
        number[pos++] = '.';
    }
    if (precision > 0) {
        for (int i = precision - 1; i >= 0; --i) {
            frac_digits[i] = (char)('0' + (frac_part % 10u));
            frac_part /= 10u;
        }
        memcpy(&number[pos], frac_digits, (size_t)precision);
        pos += (size_t)precision;
        if (trim_trailing) {
            while (pos > 0u && number[pos - 1u] == '0') {
                --pos;
            }
            if (pos > 0u && number[pos - 1u] == '.' && !alt_form) {
                --pos;
            }
        }
    }

    total_len = (int)pos;
    pad = width - total_len;
    if (pad < 0) {
        pad = 0;
    }
    if (!left_align && pad > 0) {
        bsdgame_fmt_repeat(dst, size, used, zero_pad ? '0' : ' ', pad);
    }
    bsdgame_fmt_write(dst, size, used, number, pos);
    if (left_align && pad > 0) {
        bsdgame_fmt_repeat(dst, size, used, ' ', pad);
    }
    (void)integer_digits;
}

int bsdgame_vsnprintf(char *dst, size_t size, const char *fmt, va_list ap) {
    size_t used = 0u;

    if (!fmt) {
        if (dst && size > 0u) {
            dst[0] = '\0';
        }
        return -1;
    }

    while (*fmt != '\0') {
        if (*fmt != '%') {
            bsdgame_fmt_putc(dst, size, &used, *fmt++);
            continue;
        }

        ++fmt;
        if (*fmt == '%') {
            bsdgame_fmt_putc(dst, size, &used, *fmt++);
            continue;
        }

        {
            int left_align = 0;
            int plus_sign = 0;
            int space_sign = 0;
            int zero_pad = 0;
            int alt_form = 0;
            int width = 0;
            int precision = -1;
            enum bsdgame_length_mod length = BSDGAME_LEN_DEFAULT;
            char spec;

            for (;;) {
                if (*fmt == '-') {
                    left_align = 1;
                } else if (*fmt == '+') {
                    plus_sign = 1;
                } else if (*fmt == ' ') {
                    space_sign = 1;
                } else if (*fmt == '0') {
                    zero_pad = 1;
                } else if (*fmt == '#') {
                    alt_form = 1;
                } else {
                    break;
                }
                ++fmt;
            }

            if (*fmt == '*') {
                width = va_arg(ap, int);
                if (width < 0) {
                    left_align = 1;
                    width = -width;
                }
                ++fmt;
            } else {
                while (*fmt >= '0' && *fmt <= '9') {
                    width = (width * 10) + (*fmt - '0');
                    ++fmt;
                }
            }

            if (*fmt == '.') {
                precision = 0;
                ++fmt;
                if (*fmt == '*') {
                    precision = va_arg(ap, int);
                    ++fmt;
                } else {
                    while (*fmt >= '0' && *fmt <= '9') {
                        precision = (precision * 10) + (*fmt - '0');
                        ++fmt;
                    }
                }
                if (precision < 0) {
                    precision = 0;
                }
            }

            if (fmt[0] == 'h' && fmt[1] == 'h') {
                length = BSDGAME_LEN_HH;
                fmt += 2;
            } else if (*fmt == 'h') {
                length = BSDGAME_LEN_H;
                ++fmt;
            } else if (fmt[0] == 'l' && fmt[1] == 'l') {
                length = BSDGAME_LEN_LL;
                fmt += 2;
            } else if (*fmt == 'l') {
                length = BSDGAME_LEN_L;
                ++fmt;
            } else if (*fmt == 'z') {
                length = BSDGAME_LEN_Z;
                ++fmt;
            } else if (*fmt == 't') {
                length = BSDGAME_LEN_T;
                ++fmt;
            }

            spec = *fmt;
            if (spec == '\0') {
                break;
            }
            ++fmt;

            switch (spec) {
                case 'd':
                case 'i': {
                    long long value;

                    if (length == BSDGAME_LEN_LL) {
                        value = va_arg(ap, long long);
                    } else if (length == BSDGAME_LEN_L) {
                        value = (long long)va_arg(ap, long);
                    } else if (length == BSDGAME_LEN_Z) {
                        value = (long long)va_arg(ap, ssize_t);
                    } else if (length == BSDGAME_LEN_T) {
                        value = (long long)va_arg(ap, ptrdiff_t);
                    } else {
                        value = (long long)va_arg(ap, int);
                    }
                    bsdgame_format_signed(dst, size, &used, value,
                                          width, precision, left_align,
                                          zero_pad, plus_sign, space_sign);
                    break;
                }
                case 'u':
                case 'x':
                case 'X':
                case 'o': {
                    unsigned long long value;
                    unsigned int base = (spec == 'o') ? 8u : ((spec == 'u') ? 10u : 16u);

                    if (length == BSDGAME_LEN_LL) {
                        value = va_arg(ap, unsigned long long);
                    } else if (length == BSDGAME_LEN_L) {
                        value = (unsigned long long)va_arg(ap, unsigned long);
                    } else if (length == BSDGAME_LEN_Z) {
                        value = (unsigned long long)va_arg(ap, size_t);
                    } else if (length == BSDGAME_LEN_T) {
                        value = (unsigned long long)va_arg(ap, ptrdiff_t);
                    } else {
                        value = (unsigned long long)va_arg(ap, unsigned int);
                    }
                    bsdgame_format_unsigned(dst, size, &used, value, base,
                                            spec == 'X', width, precision,
                                            left_align, zero_pad, alt_form, 0);
                    break;
                }
                case 'c':
                    bsdgame_format_char(dst, size, &used, va_arg(ap, int), width, left_align);
                    break;
                case 's':
                    bsdgame_format_string(dst, size, &used, va_arg(ap, const char *),
                                          width, precision, left_align);
                    break;
                case 'p': {
                    void *ptr = va_arg(ap, void *);

                    bsdgame_format_unsigned(dst, size, &used, (unsigned long long)(uintptr_t)ptr,
                                            16u, 0, width, precision,
                                            left_align, zero_pad, 0, "0x");
                    break;
                }
                case 'f':
                    bsdgame_format_double(dst, size, &used, va_arg(ap, double),
                                          width, precision, left_align, zero_pad,
                                          plus_sign, space_sign, alt_form, 0);
                    break;
                case 'g':
                    bsdgame_format_double(dst, size, &used, va_arg(ap, double),
                                          width, precision, left_align, zero_pad,
                                          plus_sign, space_sign, alt_form, 1);
                    break;
                default:
                    bsdgame_fmt_putc(dst, size, &used, '%');
                    bsdgame_fmt_putc(dst, size, &used, spec);
                    break;
            }
        }
    }

    if (dst && size > 0u) {
        size_t end = (used < size - 1u) ? used : (size - 1u);

        dst[end] = '\0';
    }
    return (int)used;
}

int bsdgame_snprintf(char *dst, size_t size, const char *fmt, ...) {
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = bsdgame_vsnprintf(dst, size, fmt, ap);
    va_end(ap);
    return ret;
}

int bsdgame_vsprintf(char *dst, const char *fmt, va_list ap) {
    return bsdgame_vsnprintf(dst, (size_t)0x7fffffffu, fmt, ap);
}

int bsdgame_sprintf(char *dst, const char *fmt, ...) {
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = bsdgame_vsprintf(dst, fmt, ap);
    va_end(ap);
    return ret;
}

int bsdgame_vfprintf(FILE *stream, const char *fmt, va_list ap) {
    va_list ap_copy;
    va_list ap_render;
    char stack_buf[512];
    char *buffer = stack_buf;
    int len;

    if (!stream || !fmt) {
        return -1;
    }

    va_copy(ap_copy, ap);
    len = bsdgame_vsnprintf(0, 0u, fmt, ap_copy);
    va_end(ap_copy);
    if (len < 0) {
        return -1;
    }

    if ((size_t)len + 1u > sizeof(stack_buf)) {
        buffer = (char *)malloc((size_t)len + 1u);
        if (!buffer) {
            return -1;
        }
    }

    va_copy(ap_render, ap);
    (void)bsdgame_vsnprintf(buffer, (size_t)len + 1u, fmt, ap_render);
    va_end(ap_render);
    if (len > 0) {
        (void)fwrite(buffer, 1u, (size_t)len, stream);
    }

    if (buffer != stack_buf) {
        free(buffer);
    }
    return len;
}

int bsdgame_fprintf(FILE *stream, const char *fmt, ...) {
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = bsdgame_vfprintf(stream, fmt, ap);
    va_end(ap);
    return ret;
}

int bsdgame_vprintf(const char *fmt, va_list ap) {
    return bsdgame_vfprintf(stdout, fmt, ap);
}

int bsdgame_printf(const char *fmt, ...) {
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = bsdgame_vfprintf(stdout, fmt, ap);
    va_end(ap);
    return ret;
}

int vasprintf(char **strp, const char *fmt, va_list ap) {
    va_list ap_count;
    int len;
    char *buffer;

    if (!strp || !fmt) {
        errno = EINVAL;
        return -1;
    }

    va_copy(ap_count, ap);
    len = bsdgame_vsnprintf(0, 0u, fmt, ap_count);
    va_end(ap_count);
    if (len < 0) {
        *strp = 0;
        return -1;
    }

    buffer = (char *)malloc((size_t)len + 1u);
    if (!buffer) {
        *strp = 0;
        errno = ENOMEM;
        return -1;
    }
    (void)bsdgame_vsnprintf(buffer, (size_t)len + 1u, fmt, ap);
    *strp = buffer;
    return len;
}

int asprintf(char **strp, const char *fmt, ...) {
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vasprintf(strp, fmt, ap);
    va_end(ap);
    return ret;
}

int vsscanf(const char *str, const char *fmt, va_list ap) {
    struct bsdgame_string_scan_ctx ctx;

    ctx.cursor = str;
    return bsdgame_vxscanf(bsdgame_string_getc, bsdgame_string_ungetc, &ctx, fmt, ap);
}

int sscanf(const char *str, const char *fmt, ...) {
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vsscanf(str, fmt, ap);
    va_end(ap);
    return ret;
}

int vfscanf(FILE *stream, const char *fmt, va_list ap) {
    return bsdgame_vxscanf(bsdgame_file_getc, bsdgame_file_ungetc, stream, fmt, ap);
}

int fscanf(FILE *stream, const char *fmt, ...) {
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vfscanf(stream, fmt, ap);
    va_end(ap);
    return ret;
}

int vscanf(const char *fmt, va_list ap) {
    return vfscanf(stdin, fmt, ap);
}

int scanf(const char *fmt, ...) {
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vfscanf(stdin, fmt, ap);
    va_end(ap);
    return ret;
}

int getw(FILE *stream) {
    int value;

    if (!stream) {
        errno = EINVAL;
        return EOF;
    }
    if (fread(&value, sizeof(value), 1u, stream) != 1u) {
        return EOF;
    }
    return value;
}

int putw(int value, FILE *stream) {
    if (!stream) {
        errno = EINVAL;
        return EOF;
    }
    return fwrite(&value, sizeof(value), 1u, stream) == 1u ? 0 : EOF;
}

ssize_t getdelim(char **lineptr, size_t *n, int delim, FILE *stream) {
    size_t used = 0u;
    int ch;
    char *buffer;

    if (!lineptr || !n || !stream) {
        errno = EINVAL;
        return -1;
    }

    if (!*lineptr || *n == 0u) {
        *n = 128u;
        *lineptr = (char *)malloc(*n);
        if (!*lineptr) {
            errno = ENOMEM;
            return -1;
        }
    }

    buffer = *lineptr;
    while ((ch = fgetc(stream)) != EOF) {
        if (used + 2u > *n) {
            size_t next = *n * 2u;
            char *grown = (char *)realloc(buffer, next);

            if (!grown) {
                errno = ENOMEM;
                return -1;
            }
            buffer = grown;
            *lineptr = grown;
            *n = next;
        }
        buffer[used++] = (char)ch;
        if (ch == delim) {
            break;
        }
    }
    if (used == 0u && ch == EOF) {
        return -1;
    }
    buffer[used] = '\0';
    return (ssize_t)used;
}

ssize_t getline(char **lineptr, size_t *n, FILE *stream) {
    return getdelim(lineptr, n, '\n', stream);
}

void *reallocarray(void *ptr, size_t nmemb, size_t size) {
    if (nmemb != 0u && size > ((size_t)-1 / nmemb)) {
        errno = ENOMEM;
        return 0;
    }
    return realloc(ptr, nmemb * size);
}

int open(const char *path, int oflag, ...) {
    int fd;
    int host_fd;

    if (!path) {
        errno = EINVAL;
        return -1;
    }

    fd = bsdgame_alloc_fd();
    if (fd < 0) {
        errno = EMFILE;
        return -1;
    }

    host_fd = vibe_app_open(path, oflag);
    if (host_fd < 0) {
        errno = ENOENT;
        return -1;
    }

    g_bsdgame_fds[fd].used = 1;
    g_bsdgame_fds[fd].host_fd = host_fd;
    return fd;
}

ssize_t read(int fd, void *buf, size_t count) {
    int rc;

    if (!buf) {
        errno = EINVAL;
        return -1;
    }
    if (fd == STDIN_FILENO) {
        if (count == 0u) {
            return 0;
        }
        if (!bsdgame_fill_pending_key((unsigned long long)-1)) {
            return 0;
        }
        ((char *)buf)[0] = (char)bsdgame_take_pending_key();
        return 1;
    }
    if (!bsdgame_fd_valid(fd)) {
        errno = EBADF;
        return -1;
    }

    rc = vibe_app_read(g_bsdgame_fds[fd].host_fd, buf, (int)count);
    if (rc < 0) {
        errno = EBADF;
        return -1;
    }
    return (ssize_t)rc;
}

ssize_t write(int fd, const void *buf, size_t count) {
    if (!buf) {
        errno = EINVAL;
        return -1;
    }
    if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        const char *text = (const char *)buf;

        for (size_t i = 0u; i < count; ++i) {
            vibe_app_console_putc(text[i]);
        }
        return (ssize_t)count;
    }
    if (!bsdgame_fd_valid(fd)) {
        errno = EBADF;
        return -1;
    }

    {
        int rc = vibe_app_write(g_bsdgame_fds[fd].host_fd, buf, (int)count);

        if (rc < 0) {
            errno = EBADF;
            return -1;
        }
        return (ssize_t)rc;
    }
}

int close(int fd) {
    if (fd < 0 || fd >= BSDGAME_MAX_FDS) {
        errno = EBADF;
        return -1;
    }
    if (fd <= STDERR_FILENO) {
        return 0;
    }
    if (!g_bsdgame_fds[fd].used) {
        errno = EBADF;
        return -1;
    }

    for (int i = 3; i < BSDGAME_MAX_FDS; ++i) {
        if (i != fd &&
            g_bsdgame_fds[i].used &&
            g_bsdgame_fds[i].host_fd == g_bsdgame_fds[fd].host_fd) {
            g_bsdgame_fds[fd].used = 0;
            g_bsdgame_fds[fd].host_fd = -1;
            return 0;
        }
    }

    (void)vibe_app_close(g_bsdgame_fds[fd].host_fd);
    g_bsdgame_fds[fd].used = 0;
    g_bsdgame_fds[fd].host_fd = -1;
    return 0;
}

int dup(int fd) {
    int newfd;

    if (fd < 0 || fd >= BSDGAME_MAX_FDS) {
        errno = EBADF;
        return -1;
    }
    if (fd > STDERR_FILENO && !g_bsdgame_fds[fd].used) {
        errno = EBADF;
        return -1;
    }

    newfd = bsdgame_alloc_fd();
    if (newfd < 0) {
        errno = EMFILE;
        return -1;
    }

    g_bsdgame_fds[newfd] = g_bsdgame_fds[fd];
    g_bsdgame_fds[newfd].used = 1;
    return newfd;
}

int dup2(int fd, int newfd) {
    if (fd < 0 || fd >= BSDGAME_MAX_FDS || newfd < 0 || newfd >= BSDGAME_MAX_FDS) {
        errno = EBADF;
        return -1;
    }
    if (fd > STDERR_FILENO && !g_bsdgame_fds[fd].used) {
        errno = EBADF;
        return -1;
    }
    if (fd == newfd) {
        return newfd;
    }

    (void)close(newfd);
    g_bsdgame_fds[newfd] = g_bsdgame_fds[fd];
    g_bsdgame_fds[newfd].used = 1;
    return newfd;
}

off_t lseek(int fd, off_t offset, int whence) {
    int rc;

    if (!bsdgame_fd_valid(fd)) {
        errno = EBADF;
        return -1;
    }
    rc = vibe_app_lseek(g_bsdgame_fds[fd].host_fd, (int)offset, whence);
    if (rc < 0) {
        errno = EINVAL;
        return -1;
    }
    return (off_t)rc;
}

int setitimer(int which, const struct itimerval *new_value,
              struct itimerval *old_value) {
    (void)which;
    (void)new_value;
    if (old_value) {
        memset(old_value, 0, sizeof(*old_value));
    }
    return 0;
}

int isatty(int fd) {
    if (bsdgame_is_console_fd(fd)) {
        return 1;
    }
    errno = ENOTTY;
    return 0;
}

int stat(const char *path, struct stat *buf) {
    struct vibe_app_stat vibe_stat;

    if (!path || !buf) {
        errno = EINVAL;
        return -1;
    }
    if (vibe_app_stat(path, &vibe_stat) != 0) {
        errno = ENOENT;
        return -1;
    }

    memset(buf, 0, sizeof(*buf));
    buf->st_size = (off_t)vibe_stat.size;
    buf->st_mode = vibe_stat.is_dir ? 0040755 : 0100644;
    buf->st_nlink = 1;
    buf->st_blksize = 512u;
    buf->st_blocks = (uint32_t)((vibe_stat.size + 511) / 512);
    return 0;
}

int fstat(int fd, struct stat *buf) {
    struct vibe_app_stat vibe_stat;

    if (!buf) {
        errno = EINVAL;
        return -1;
    }
    if (bsdgame_is_console_fd(fd)) {
        memset(buf, 0, sizeof(*buf));
        buf->st_mode = 0020000;
        buf->st_nlink = 1;
        return 0;
    }
    if (!bsdgame_fd_valid(fd)) {
        errno = EBADF;
        return -1;
    }
    if (vibe_app_fstat(g_bsdgame_fds[fd].host_fd, &vibe_stat) != 0) {
        errno = EBADF;
        return -1;
    }

    memset(buf, 0, sizeof(*buf));
    buf->st_size = (off_t)vibe_stat.size;
    buf->st_mode = 0100644;
    buf->st_nlink = 1;
    buf->st_blksize = 512u;
    buf->st_blocks = (uint32_t)((vibe_stat.size + 511) / 512);
    return 0;
}

int lstat(const char *path, struct stat *buf) {
    return stat(path, buf);
}

int fcntl(int fd, int cmd, ...) {
    (void)fd;
    (void)cmd;
    errno = EINVAL;
    return -1;
}

pid_t getpid(void) {
    return 1;
}

pid_t getppid(void) {
    return 1;
}

uid_t getuid(void) {
    return 0u;
}

struct passwd *getpwuid(uid_t uid) {
    static struct passwd passwd_entry;
    static char login_buf[32];
    static char home_buf[64];
    static char shell_buf[32];
    const char *login = getlogin();
    const char *home = getenv("HOME");
    const char *shell = getenv("SHELL");

    if (!login || login[0] == '\0') {
        login = "player";
    }
    if (!home || home[0] == '\0') {
        home = "/";
    }
    if (!shell || shell[0] == '\0') {
        shell = "/bin/sh";
    }

    strlcpy(login_buf, login, sizeof(login_buf));
    strlcpy(home_buf, home, sizeof(home_buf));
    strlcpy(shell_buf, shell, sizeof(shell_buf));

    memset(&passwd_entry, 0, sizeof(passwd_entry));
    passwd_entry.pw_name = login_buf;
    passwd_entry.pw_passwd = "";
    passwd_entry.pw_uid = uid;
    passwd_entry.pw_gid = 0u;
    passwd_entry.pw_class = "";
    passwd_entry.pw_gecos = login_buf;
    passwd_entry.pw_dir = home_buf;
    passwd_entry.pw_shell = shell_buf;
    return &passwd_entry;
}

uid_t geteuid(void) {
    return 0u;
}

gid_t getgid(void) {
    return 0u;
}

gid_t getegid(void) {
    return 0u;
}

int setegid(gid_t egid) {
    (void)egid;
    return 0;
}

int setresgid(gid_t rgid, gid_t egid, gid_t sgid) {
    (void)rgid;
    (void)egid;
    (void)sgid;
    return 0;
}

char *getenv(const char *name) {
    return (char *)vibe_app_getenv(name);
}

static int bsdgame_exec_current(const char *path, char *const argv[]) {
    const char *progname = getprogname();
    const char *path_base = bsdgame_path_basename(path);
    int argc = 0;

    if (!argv || !argv[0] || argv[0][0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (path && path[0] != '\0' &&
        strcmp(path, progname) != 0 &&
        strcmp(path_base, progname) != 0) {
        errno = ENOENT;
        return -1;
    }
    if (strcmp(bsdgame_path_basename(argv[0]), progname) != 0) {
        errno = ENOENT;
        return -1;
    }

    while (argv[argc] != 0) {
        ++argc;
    }

    setprogname(argv[0]);
    vibe_bsdgame_exit(vibe_bsdgame_main(argc, argv));
    return -1;
}

int execl(const char *path, const char *arg0, ...) {
    char *argv[16];
    int argc = 0;
    va_list ap;
    const char *arg;

    if (!arg0 || arg0[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    argv[argc++] = (char *)arg0;
    va_start(ap, arg0);
    while ((arg = va_arg(ap, const char *)) != 0) {
        if (argc >= (int)(sizeof(argv) / sizeof(argv[0])) - 1) {
            va_end(ap);
            errno = E2BIG;
            return -1;
        }
        argv[argc++] = (char *)arg;
    }
    va_end(ap);
    argv[argc] = 0;
    return bsdgame_exec_current(path, argv);
}

int execlp(const char *file, const char *arg0, ...) {
    char *argv[16];
    int argc = 0;
    va_list ap;
    const char *arg;

    if (!file || !arg0 || arg0[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    argv[argc++] = (char *)arg0;
    va_start(ap, arg0);
    while ((arg = va_arg(ap, const char *)) != 0) {
        if (argc >= (int)(sizeof(argv) / sizeof(argv[0])) - 1) {
            va_end(ap);
            errno = E2BIG;
            return -1;
        }
        argv[argc++] = (char *)arg;
    }
    va_end(ap);
    argv[argc] = 0;
    return bsdgame_exec_current(file, argv);
}

char *getcwd(char *buf, size_t size) {
    if (!buf || size == 0u) {
        errno = EINVAL;
        return 0;
    }
    if (vibe_app_getcwd(buf, (int)size) != 0) {
        errno = EINVAL;
        return 0;
    }
    return buf;
}

int chdir(const char *path) {
    struct vibe_app_stat stat_buf;

    if (!path || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (strcmp(path, ".") == 0 || strcmp(path, "/") == 0) {
        return 0;
    }
    if (vibe_app_stat(path, &stat_buf) != 0) {
        errno = ENOENT;
        return -1;
    }
    if (!stat_buf.is_dir) {
        errno = ENOTDIR;
        return -1;
    }
    return 0;
}

int access(const char *path, int mode) {
    struct vibe_app_stat stat_buf;

    (void)mode;
    if (!path || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (vibe_app_stat(path, &stat_buf) != 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int rmdir(const char *path) {
    int rc;

    if (!path || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    rc = vibe_app_remove_dir(path);
    if (rc == 0) {
        return 0;
    }
    if (rc == -2) {
        errno = ENOTEMPTY;
        return -1;
    }
    if (rc == -3) {
        errno = ENOTDIR;
        return -1;
    }
    errno = ENOENT;
    return -1;
}

int mkdir(const char *path, mode_t mode) {
    int rc;

    (void)mode;
    if (!path || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    rc = vibe_app_create_dir(path);
    if (rc == 0) {
        return 0;
    }
    if (rc == -2) {
        errno = EEXIST;
        return -1;
    }
    errno = ENOENT;
    return -1;
}

int link(const char *oldpath, const char *newpath) {
    struct vibe_app_stat stat_buf;

    (void)newpath;
    if (!oldpath || !newpath || oldpath[0] == '\0' || newpath[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (vibe_app_stat(oldpath, &stat_buf) != 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

pid_t fork(void) {
    errno = EAGAIN;
    return -1;
}

unsigned int sleep(unsigned int seconds) {
    return vibe_app_sleep_ms(seconds * 1000u) == 0 ? 0u : 0u;
}

int usleep(unsigned int usec) {
    unsigned int delay_ms = usec / 1000u;

    if (delay_ms == 0u && usec > 0u) {
        delay_ms = 1u;
    }
    return vibe_app_sleep_ms(delay_ms);
}

int fsync(int fd) {
    (void)fd;
    return 0;
}

int flock(int fd, int operation) {
    (void)fd;
    (void)operation;
    return 0;
}

int unlink(const char *path) {
    (void)path;
    return 0;
}

mode_t umask(mode_t mask) {
    return mask;
}

pid_t waitpid(pid_t pid, int *status, int options) {
    (void)pid;
    (void)options;
    if (status) {
        *status = 0;
    }
    errno = ECHILD;
    return -1;
}

pid_t wait(int *status) {
    return waitpid(-1, status, 0);
}

char erasechar(void) {
    return '\b';
}

char killchar(void) {
    return 0x15;
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt) {
    ssize_t total = 0;

    if (!iov || iovcnt < 0) {
        errno = EINVAL;
        return -1;
    }
    for (int i = 0; i < iovcnt; ++i) {
        ssize_t rc = read(fd, iov[i].iov_base, iov[i].iov_len);

        if (rc < 0) {
            return total > 0 ? total : -1;
        }
        total += rc;
        if ((size_t)rc < iov[i].iov_len) {
            break;
        }
    }
    return total;
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
    ssize_t total = 0;

    if (!iov || iovcnt < 0) {
        errno = EINVAL;
        return -1;
    }
    for (int i = 0; i < iovcnt; ++i) {
        ssize_t rc = write(fd, iov[i].iov_base, iov[i].iov_len);

        if (rc < 0) {
            return total > 0 ? total : -1;
        }
        total += rc;
        if ((size_t)rc < iov[i].iov_len) {
            break;
        }
    }
    return total;
}

int tcgetattr(int fd, struct termios *termios_p) {
    if (!termios_p) {
        errno = EINVAL;
        return -1;
    }
    if (!isatty(fd)) {
        return -1;
    }
    memset(termios_p, 0, sizeof(*termios_p));
    termios_p->c_ispeed = B9600;
    termios_p->c_ospeed = B9600;
    return 0;
}

int tcsetattr(int fd, int optional_actions, const struct termios *termios_p) {
    (void)optional_actions;
    (void)termios_p;
    if (!isatty(fd)) {
        return -1;
    }
    return 0;
}

int tcflush(int fd, int queue_selector) {
    (void)queue_selector;
    if (!isatty(fd)) {
        return -1;
    }
    if (fd == STDIN_FILENO) {
        g_bsdgame_pending_key = 0;
        g_bsdgame_pending_key_valid = 0;
        while (vibe_app_poll_key() != 0) {
        }
    }
    return 0;
}

speed_t cfgetospeed(const struct termios *termios_p) {
    if (!termios_p || termios_p->c_ospeed == 0u) {
        return B9600;
    }
    return termios_p->c_ospeed;
}

speed_t baudrate(void) {
    return B9600;
}

int ioctl(int fd, unsigned long request, ...) {
    va_list ap;

    if (request == TIOCGWINSZ) {
        struct winsize *ws;

        if (!isatty(fd)) {
            return -1;
        }
        va_start(ap, request);
        ws = va_arg(ap, struct winsize *);
        va_end(ap);
        if (!ws) {
            errno = EINVAL;
            return -1;
        }
        ws->ws_row = 25u;
        ws->ws_col = 80u;
        ws->ws_xpixel = 0u;
        ws->ws_ypixel = 0u;
        return 0;
    }

    errno = ENOTTY;
    return -1;
}

static int bsdgame_poll_scan(struct pollfd *fds, nfds_t nfds, int capture_key) {
    int ready = 0;

    if (capture_key && !g_bsdgame_pending_key_valid) {
        (void)bsdgame_fill_pending_key(vibe_app_millis());
    }

    for (nfds_t i = 0u; i < nfds; ++i) {
        struct pollfd *pfd = &fds[i];

        pfd->revents = 0;
        if (pfd->fd < 0) {
            continue;
        }
        if (pfd->fd == STDIN_FILENO) {
            if ((pfd->events & (POLLIN | POLLPRI)) != 0 &&
                g_bsdgame_pending_key_valid) {
                pfd->revents |= (short)(pfd->events & (POLLIN | POLLPRI));
            }
        } else if (bsdgame_fd_valid(pfd->fd)) {
            pfd->revents |= (short)(pfd->events & (POLLIN | POLLOUT));
        } else {
            pfd->revents |= POLLNVAL;
        }
        if (pfd->revents != 0) {
            ++ready;
        }
    }

    return ready;
}

int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout_ts,
          const sigset_t *sigmask) {
    unsigned long long deadline_ms = (unsigned long long)-1;

    (void)sigmask;
    if (!fds && nfds > 0u) {
        errno = EINVAL;
        return -1;
    }
    if (timeout_ts) {
        unsigned long long timeout_ms = (unsigned long long)timeout_ts->tv_sec * 1000ull;

        timeout_ms += (unsigned long long)((timeout_ts->tv_nsec + 999999L) / 1000000L);
        deadline_ms = vibe_app_millis() + timeout_ms;
    }

    for (;;) {
        int ready = bsdgame_poll_scan(fds, nfds, 1);

        if (ready > 0) {
            return ready;
        }
        if (deadline_ms != (unsigned long long)-1 &&
            vibe_app_millis() >= deadline_ms) {
            return 0;
        }
        vibe_app_yield();
    }
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
    struct timespec ts;

    if (timeout < 0) {
        return ppoll(fds, nfds, 0, 0);
    }
    ts.tv_sec = timeout / 1000;
    ts.tv_nsec = (long)(timeout % 1000) * 1000000L;
    return ppoll(fds, nfds, &ts, 0);
}

char *strerror(int errnum) {
    switch (errnum) {
        case 0:
            return "success";
        case EPERM:
            return "operation not permitted";
        case ENOENT:
            return "no such file or directory";
        case EIO:
            return "i/o error";
        case EBADF:
            return "bad file descriptor";
        case EAGAIN:
            return "resource temporarily unavailable";
        case ENOMEM:
            return "not enough memory";
        case EACCES:
            return "permission denied";
        case EEXIST:
            return "file exists";
        case ENOTDIR:
            return "not a directory";
        case EISDIR:
            return "is a directory";
        case EINVAL:
            return "invalid argument";
        case ENFILE:
        case EMFILE:
            return "too many open files";
        case ENOTTY:
            return "not a tty";
        case ENOSPC:
            return "no space left on device";
        case ENOTEMPTY:
            return "directory not empty";
        case ENAMETOOLONG:
            return "file name too long";
        default:
            return "unknown error";
    }
}

void perror(const char *s) {
    if (s && s[0] != '\0') {
        bsdgame_fprintf(stderr, "%s: %s\n", s, strerror(errno));
    } else {
        bsdgame_fprintf(stderr, "%s\n", strerror(errno));
    }
}
