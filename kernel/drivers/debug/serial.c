#include <kernel/drivers/debug/debug.h>
#include <kernel/hal/io.h>
#include <kernel/driver_manager.h>  /* available via -Ikernel/include */
#include <kernel/lock.h>
#include <stdarg.h>
#include <stddef.h>

/* Serial port I/O for debugging output */
#define SERIAL_PORT 0x3F8

static spinlock_t g_serial_lock;
static int g_serial_lock_ready = 0;

void kernel_debug_init(void) {
    static int registered = 0;
    if (!registered) {
        /* first call only: register the driver with manager */
        register_driver("serial", "debug", kernel_debug_init);
        registered = 1;
        spinlock_init(&g_serial_lock);
        g_serial_lock_ready = 1;
        return;
    }

    /* actual initialization on second invocation */
    /* Set up serial port (COM1) at 115200 baud */
    outb(SERIAL_PORT + 1, 0x00);  /* Disable interrupts */
    outb(SERIAL_PORT + 3, 0x80);  /* Enable DLAB */
    outb(SERIAL_PORT + 0, 0x01);  /* Divisor low byte (115200 baud) */
    outb(SERIAL_PORT + 1, 0x00);  /* Divisor high byte */
    outb(SERIAL_PORT + 3, 0x03);  /* 8 bits, 1 stop, no parity */
    outb(SERIAL_PORT + 2, 0xC7);  /* Enable FIFO */
    outb(SERIAL_PORT + 4, 0x0B);  /* Set RTS/DTR */
}

static int serial_is_transmitter_empty(void) {
    return (inb(SERIAL_PORT + 5) & 0x20) != 0;
}

static void serial_putc_raw(char c) {
    while (!serial_is_transmitter_empty()) {
    }
    outb(SERIAL_PORT, (uint8_t)c);
}

static void serial_write_raw(const char *str) {
    const char *p;

    if (str == NULL) {
        return;
    }

    for (p = str; *p != '\0'; ++p) {
        if (*p == '\n') {
            serial_putc_raw('\r');
            serial_putc_raw('\n');
        } else {
            serial_putc_raw(*p);
        }
    }
}

static void serial_vprintf_raw(const char *fmt, va_list args) {
    const char *p;

    for (p = fmt; *p != '\0'; ++p) {
        if (*p == '%') {
            ++p;
            if (*p == '\0') {
                break;
            }
            
            switch (*p) {
            case 'd': {
                int value = va_arg(args, int);
                if (value < 0) {
                    serial_putc_raw('-');
                    value = -value;
                }
                
                char buf[16];
                int len = 0;
                if (value == 0) {
                    buf[len++] = '0';
                } else {
                    int v = value;
                    for (int w = v; w > 0; w /= 10) {
                        ++len;
                    }
                    for (int i = len - 1; i >= 0; --i) {
                        buf[i] = (char)('0' + (value % 10));
                        value /= 10;
                    }
                }
                for (int i = 0; i < len; ++i) {
                    serial_putc_raw(buf[i]);
                }
                break;
            }
            case 'x': {
                unsigned int value = va_arg(args, unsigned int);
                char buf[16];
                int len = 0;
                if (value == 0) {
                    buf[len++] = '0';
                } else {
                    unsigned int v = value;
                    for (int w = v; w > 0; w /= 16) {
                        ++len;
                    }
                    for (int i = len - 1; i >= 0; --i) {
                        unsigned int digit = value % 16;
                        buf[i] = (digit < 10) ? (char)('0' + digit) : (char)('a' + (digit - 10));
                        value /= 16;
                    }
                }
                for (int i = 0; i < len; ++i) {
                    serial_putc_raw(buf[i]);
                }
                break;
            }
            case 'c': {
                int c = va_arg(args, int);
                serial_putc_raw((char)c);
                break;
            }
            case 's': {
                const char *str = va_arg(args, const char *);
                serial_write_raw(str);
                break;
            }
            case '%':
                serial_putc_raw('%');
                break;
            default:
                serial_putc_raw('%');
                serial_putc_raw(*p);
                break;
            }
        } else if (*p == '\n') {
            serial_putc_raw('\r');
            serial_putc_raw('\n');
        } else {
            serial_putc_raw(*p);
        }
    }
}

void kernel_debug_putc(char c) {
    uint32_t flags = 0u;

    if (g_serial_lock_ready) {
        flags = spinlock_lock_irqsave(&g_serial_lock);
    }
    serial_putc_raw(c);
    if (g_serial_lock_ready) {
        spinlock_unlock_irqrestore(&g_serial_lock, flags);
    }
}

void kernel_debug_puts(const char *str) {
    uint32_t flags = 0u;

    if (str == NULL) {
        return;
    }
    if (g_serial_lock_ready) {
        flags = spinlock_lock_irqsave(&g_serial_lock);
    }
    serial_write_raw(str);
    if (g_serial_lock_ready) {
        spinlock_unlock_irqrestore(&g_serial_lock, flags);
    }
}

void kernel_debug_printf(const char *fmt, ...) {
    uint32_t flags = 0u;
    va_list args;

    if (fmt == NULL) {
        return;
    }
    if (g_serial_lock_ready) {
        flags = spinlock_lock_irqsave(&g_serial_lock);
    }

    va_start(args, fmt);
    serial_vprintf_raw(fmt, args);
    va_end(args);

    if (g_serial_lock_ready) {
        spinlock_unlock_irqrestore(&g_serial_lock, flags);
    }
}
