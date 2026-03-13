#include <kernel/drivers/input/input.h>
#include <kernel/hal/io.h>
#include <kernel/interrupt.h>
#include <include/userland_api.h>

#define KBD_QUEUE_SIZE 128
#define PS2_STATUS_PORT 0x64u
#define PS2_DATA_PORT 0x60u
#define PS2_STATUS_OUTPUT_FULL 0x01u
#define PS2_STATUS_INPUT_FULL 0x02u

static volatile uint16_t g_kernel_kbd_queue[KBD_QUEUE_SIZE];
static volatile uint8_t g_kernel_kbd_head = 0u;
static volatile uint8_t g_kernel_kbd_tail = 0u;
static volatile uint8_t g_kernel_kbd_shift = 0u;
static volatile uint8_t g_kernel_kbd_ctrl = 0u;
static volatile uint8_t g_kernel_kbd_extended = 0u;
static volatile uint8_t g_kernel_kbd_ready = 0u;
static char g_kernel_kbd_map[128];
static char g_kernel_kbd_shift_map[128];

static void ps2_wait_write(void) {
    while ((inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL) != 0u) {
    }
}

static int ps2_wait_read_timeout(void) {
    for (uint32_t i = 0u; i < 1000000u; ++i) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) != 0u) {
            return 1;
        }
    }
    return 0;
}

static void ps2_drain_output(void) {
    while ((inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) != 0u) {
        (void)inb(PS2_DATA_PORT);
    }
}

static void kbd_write_cmd(uint8_t value) {
    ps2_wait_write();
    outb(PS2_DATA_PORT, value);
}

static int kbd_expect_ack(void) {
    if (!ps2_wait_read_timeout()) {
        return 0;
    }
    return inb(PS2_DATA_PORT) == 0xFAu;
}

static void kbd_init_maps(void) {
    for (int i = 0; i < 128; ++i) {
        g_kernel_kbd_map[i] = '\0';
        g_kernel_kbd_shift_map[i] = '\0';
    }

    g_kernel_kbd_map[0x02] = '1'; g_kernel_kbd_shift_map[0x02] = '!';
    g_kernel_kbd_map[0x03] = '2'; g_kernel_kbd_shift_map[0x03] = '@';
    g_kernel_kbd_map[0x04] = '3'; g_kernel_kbd_shift_map[0x04] = '#';
    g_kernel_kbd_map[0x05] = '4'; g_kernel_kbd_shift_map[0x05] = '$';
    g_kernel_kbd_map[0x06] = '5'; g_kernel_kbd_shift_map[0x06] = '%';
    g_kernel_kbd_map[0x07] = '6'; g_kernel_kbd_shift_map[0x07] = '^';
    g_kernel_kbd_map[0x08] = '7'; g_kernel_kbd_shift_map[0x08] = '&';
    g_kernel_kbd_map[0x09] = '8'; g_kernel_kbd_shift_map[0x09] = '*';
    g_kernel_kbd_map[0x0A] = '9'; g_kernel_kbd_shift_map[0x0A] = '(';
    g_kernel_kbd_map[0x0B] = '0'; g_kernel_kbd_shift_map[0x0B] = ')';
    g_kernel_kbd_map[0x0C] = '-'; g_kernel_kbd_shift_map[0x0C] = '_';
    g_kernel_kbd_map[0x0D] = '='; g_kernel_kbd_shift_map[0x0D] = '+';
    g_kernel_kbd_map[0x0E] = '\b'; g_kernel_kbd_shift_map[0x0E] = '\b';
    g_kernel_kbd_map[0x0F] = '\t'; g_kernel_kbd_shift_map[0x0F] = '\t';
    g_kernel_kbd_map[0x10] = 'q'; g_kernel_kbd_shift_map[0x10] = 'Q';
    g_kernel_kbd_map[0x11] = 'w'; g_kernel_kbd_shift_map[0x11] = 'W';
    g_kernel_kbd_map[0x12] = 'e'; g_kernel_kbd_shift_map[0x12] = 'E';
    g_kernel_kbd_map[0x13] = 'r'; g_kernel_kbd_shift_map[0x13] = 'R';
    g_kernel_kbd_map[0x14] = 't'; g_kernel_kbd_shift_map[0x14] = 'T';
    g_kernel_kbd_map[0x15] = 'y'; g_kernel_kbd_shift_map[0x15] = 'Y';
    g_kernel_kbd_map[0x16] = 'u'; g_kernel_kbd_shift_map[0x16] = 'U';
    g_kernel_kbd_map[0x17] = 'i'; g_kernel_kbd_shift_map[0x17] = 'I';
    g_kernel_kbd_map[0x18] = 'o'; g_kernel_kbd_shift_map[0x18] = 'O';
    g_kernel_kbd_map[0x19] = 'p'; g_kernel_kbd_shift_map[0x19] = 'P';
    g_kernel_kbd_map[0x1A] = '['; g_kernel_kbd_shift_map[0x1A] = '{';
    g_kernel_kbd_map[0x1B] = ']'; g_kernel_kbd_shift_map[0x1B] = '}';
    g_kernel_kbd_map[0x1C] = '\n'; g_kernel_kbd_shift_map[0x1C] = '\n';
    g_kernel_kbd_map[0x1E] = 'a'; g_kernel_kbd_shift_map[0x1E] = 'A';
    g_kernel_kbd_map[0x1F] = 's'; g_kernel_kbd_shift_map[0x1F] = 'S';
    g_kernel_kbd_map[0x20] = 'd'; g_kernel_kbd_shift_map[0x20] = 'D';
    g_kernel_kbd_map[0x21] = 'f'; g_kernel_kbd_shift_map[0x21] = 'F';
    g_kernel_kbd_map[0x22] = 'g'; g_kernel_kbd_shift_map[0x22] = 'G';
    g_kernel_kbd_map[0x23] = 'h'; g_kernel_kbd_shift_map[0x23] = 'H';
    g_kernel_kbd_map[0x24] = 'j'; g_kernel_kbd_shift_map[0x24] = 'J';
    g_kernel_kbd_map[0x25] = 'k'; g_kernel_kbd_shift_map[0x25] = 'K';
    g_kernel_kbd_map[0x26] = 'l'; g_kernel_kbd_shift_map[0x26] = 'L';
    g_kernel_kbd_map[0x27] = ';'; g_kernel_kbd_shift_map[0x27] = ':';
    g_kernel_kbd_map[0x28] = '\''; g_kernel_kbd_shift_map[0x28] = '"';
    g_kernel_kbd_map[0x29] = '`'; g_kernel_kbd_shift_map[0x29] = '~';
    g_kernel_kbd_map[0x2B] = '\\'; g_kernel_kbd_shift_map[0x2B] = '|';
    g_kernel_kbd_map[0x2C] = 'z'; g_kernel_kbd_shift_map[0x2C] = 'Z';
    g_kernel_kbd_map[0x2D] = 'x'; g_kernel_kbd_shift_map[0x2D] = 'X';
    g_kernel_kbd_map[0x2E] = 'c'; g_kernel_kbd_shift_map[0x2E] = 'C';
    g_kernel_kbd_map[0x2F] = 'v'; g_kernel_kbd_shift_map[0x2F] = 'V';
    g_kernel_kbd_map[0x30] = 'b'; g_kernel_kbd_shift_map[0x30] = 'B';
    g_kernel_kbd_map[0x31] = 'n'; g_kernel_kbd_shift_map[0x31] = 'N';
    g_kernel_kbd_map[0x32] = 'm'; g_kernel_kbd_shift_map[0x32] = 'M';
    g_kernel_kbd_map[0x33] = ','; g_kernel_kbd_shift_map[0x33] = '<';
    g_kernel_kbd_map[0x34] = '.'; g_kernel_kbd_shift_map[0x34] = '>';
    g_kernel_kbd_map[0x35] = '/'; g_kernel_kbd_shift_map[0x35] = '?';
    g_kernel_kbd_map[0x39] = ' '; g_kernel_kbd_shift_map[0x39] = ' ';
}

static void kbd_push_key(uint16_t key) {
    const uint8_t next = (uint8_t)((g_kernel_kbd_head + 1u) % KBD_QUEUE_SIZE);
    if (next == g_kernel_kbd_tail) {
        return;
    }
    g_kernel_kbd_queue[g_kernel_kbd_head] = key;
    g_kernel_kbd_head = next;
}

int kernel_keyboard_read(void) {
    int value = 0;

    if (g_kernel_kbd_tail != g_kernel_kbd_head) {
        value = (int)g_kernel_kbd_queue[g_kernel_kbd_tail];
        g_kernel_kbd_tail = (uint8_t)((g_kernel_kbd_tail + 1u) % KBD_QUEUE_SIZE);
    }

    return value;
}

void kernel_keyboard_irq_handler(void) {
    const uint8_t scancode = inb(PS2_DATA_PORT);

    if (!g_kernel_kbd_ready) {
        kernel_pic_send_eoi(1);
        return;
    }

    if (scancode == 0xE0u) {
        g_kernel_kbd_extended = 1u;
        kernel_pic_send_eoi(1);
        return;
    }

    if (g_kernel_kbd_extended) {
        g_kernel_kbd_extended = 0u;
        if ((scancode & 0x80u) == 0u) {
            uint16_t key = 0u;

            if (scancode == 0x48u) key = KEY_ARROW_UP;
            else if (scancode == 0x50u) key = KEY_ARROW_DOWN;
            else if (scancode == 0x4Bu) key = KEY_ARROW_LEFT;
            else if (scancode == 0x4Du) key = KEY_ARROW_RIGHT;
            else if (scancode == 0x53u) key = KEY_DELETE;

            if (key != 0u) {
                kbd_push_key(key);
            }
        }
        kernel_pic_send_eoi(1);
        return;
    }

    if (scancode == 0x2Au || scancode == 0x36u) {
        g_kernel_kbd_shift = 1u;
        kernel_pic_send_eoi(1);
        return;
    }

    if (scancode == 0x1Du) {
        g_kernel_kbd_ctrl = 1u;
        kernel_pic_send_eoi(1);
        return;
    }

    if (scancode == 0xAAu || scancode == 0xB6u) {
        g_kernel_kbd_shift = 0u;
        kernel_pic_send_eoi(1);
        return;
    }

    if (scancode == 0x9Du) {
        g_kernel_kbd_ctrl = 0u;
        kernel_pic_send_eoi(1);
        return;
    }

    if ((scancode & 0x80u) != 0u) {
        kernel_pic_send_eoi(1);
        return;
    }

    if (scancode == 0xFAu || scancode == 0xFEu) {
        kernel_pic_send_eoi(1);
        return;
    }

    char c = g_kernel_kbd_shift ? g_kernel_kbd_shift_map[scancode] : g_kernel_kbd_map[scancode];
    if (c != '\0') {
        if (g_kernel_kbd_ctrl) {
            char lower = c;

            if (lower >= 'A' && lower <= 'Z') {
                lower = (char)(lower - 'A' + 'a');
            }
            if (lower >= 'a' && lower <= 'z') {
                c = (char)(lower - 'a' + 1);
            }
        }
        kbd_push_key((uint16_t)(uint8_t)c);
    }

    kernel_pic_send_eoi(1);
}

void kernel_keyboard_init(void) {
    uint8_t config;

    g_kernel_kbd_head = 0u;
    g_kernel_kbd_tail = 0u;
    g_kernel_kbd_shift = 0u;
    g_kernel_kbd_ctrl = 0u;
    g_kernel_kbd_extended = 0u;
    g_kernel_kbd_ready = 0u;
    kbd_init_maps();

    ps2_drain_output();

    ps2_wait_write();
    outb(PS2_STATUS_PORT, 0xAEu);

    ps2_wait_write();
    outb(PS2_STATUS_PORT, 0x20u);
    if (!ps2_wait_read_timeout()) {
        return;
    }

    config = inb(PS2_DATA_PORT);
    config |= 0x01u;
    config &= (uint8_t)~0x10u;

    ps2_wait_write();
    outb(PS2_STATUS_PORT, 0x60u);
    ps2_wait_write();
    outb(PS2_DATA_PORT, config);

    kbd_write_cmd(0xF6u);
    if (!kbd_expect_ack()) {
        return;
    }

    kbd_write_cmd(0xF4u);
    if (!kbd_expect_ack()) {
        return;
    }

    g_kernel_kbd_ready = 1u;
}
