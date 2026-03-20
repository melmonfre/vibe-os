#include <kernel/drivers/input/input.h>
#include <kernel/hal/io.h>
#include <kernel/interrupt.h>
#include <include/userland_api.h>
#include <headers/kernel/keymap.h>
#include <include/string.h>

#define KBD_QUEUE_SIZE 128
#define PS2_STATUS_PORT 0x64u
#define PS2_DATA_PORT 0x60u
#define PS2_STATUS_OUTPUT_FULL 0x01u
#define PS2_STATUS_INPUT_FULL 0x02u
#define PS2_STATUS_AUX_OUTPUT_FULL 0x20u
#define PS2_TIMEOUT_SPINS 1000000u
#define PS2_DRAIN_LIMIT 64u

extern keymap_t keymap_us;
extern keymap_t keymap_pt_br;
extern keymap_t keymap_br_abnt2;
extern keymap_t keymap_us_intl;
extern keymap_t keymap_es;
extern keymap_t keymap_fr;
extern keymap_t keymap_de;

static const keymap_t* g_available_keymaps[] = {
    &keymap_us,
    &keymap_pt_br,
    &keymap_br_abnt2,
    &keymap_us_intl,
    &keymap_es,
    &keymap_fr,
    &keymap_de,
};
static const int g_num_available_keymaps = sizeof(g_available_keymaps) / sizeof(keymap_t*);

static volatile uint16_t g_kernel_kbd_queue[KBD_QUEUE_SIZE];
static volatile uint8_t g_kernel_kbd_head = 0u;
static volatile uint8_t g_kernel_kbd_tail = 0u;
static volatile uint8_t g_kernel_kbd_shift = 0u;
static volatile uint8_t g_kernel_kbd_ctrl = 0u;
static volatile uint8_t g_kernel_kbd_extended = 0u;
static volatile uint8_t g_kernel_kbd_ready = 0u;
static const keymap_t* g_current_keymap = &keymap_us;

static int ps2_wait_write_timeout(void) {
    for (uint32_t i = 0u; i < PS2_TIMEOUT_SPINS; ++i) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL) == 0u) {
            return 1;
        }
    }
    return 0;
}

static int ps2_wait_read_timeout(void) {
    for (uint32_t i = 0u; i < PS2_TIMEOUT_SPINS; ++i) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) != 0u) {
            return 1;
        }
    }
    return 0;
}

static void ps2_drain_output(void) {
    for (uint32_t i = 0u; i < PS2_DRAIN_LIMIT; ++i) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) == 0u) {
            break;
        }
        (void)inb(PS2_DATA_PORT);
    }
}

static int kbd_write_cmd(uint8_t value) {
    if (!ps2_wait_write_timeout()) {
        return 0;
    }
    outb(PS2_DATA_PORT, value);
    return 1;
}

static int kbd_expect_ack(void) {
    if (!ps2_wait_read_timeout()) {
        return 0;
    }
    return inb(PS2_DATA_PORT) == 0xFAu;
}

int kernel_keyboard_set_layout(const char* name) {
    for (int i = 0; i < g_num_available_keymaps; ++i) {
        if (strcmp(g_available_keymaps[i]->name, name) == 0) {
            g_current_keymap = g_available_keymaps[i];
            return 0;
        }
    }
    return -1;
}

const char* kernel_keyboard_get_layout(void) {
    return g_current_keymap->name;
}

void kernel_keyboard_get_available_layouts(char* buffer, int size) {
    int offset = 0;
    for (int i = 0; i < g_num_available_keymaps; ++i) {
        int len = strlen(g_available_keymaps[i]->name);
        if (offset + len + 2 < size) {
            strcpy(buffer + offset, g_available_keymaps[i]->name);
            offset += len;
            buffer[offset++] = '\n';
        }
    }
    buffer[offset] = '\0';
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
    const uint8_t status = inb(PS2_STATUS_PORT);
    uint8_t scancode;

    if ((status & PS2_STATUS_OUTPUT_FULL) == 0u ||
        (status & PS2_STATUS_AUX_OUTPUT_FULL) != 0u) {
        kernel_pic_send_eoi(1);
        return;
    }

    scancode = inb(PS2_DATA_PORT);

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

    char c = g_kernel_kbd_shift ? g_current_keymap->shift_map[scancode] : g_current_keymap->map[scancode];
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
    kernel_keyboard_set_layout("us");

    ps2_drain_output();

    if (!ps2_wait_write_timeout()) {
        return;
    }
    outb(PS2_STATUS_PORT, 0xAEu);

    ps2_drain_output();
    if (!ps2_wait_write_timeout()) {
        return;
    }
    outb(PS2_STATUS_PORT, 0x20u);
    if (!ps2_wait_read_timeout()) {
        return;
    }

    config = inb(PS2_DATA_PORT);
    config |= 0x01u;
    config &= (uint8_t)~0x10u;

    if (!ps2_wait_write_timeout()) {
        return;
    }
    outb(PS2_STATUS_PORT, 0x60u);
    if (!ps2_wait_write_timeout()) {
        return;
    }
    outb(PS2_DATA_PORT, config);

    ps2_drain_output();
    if (!kbd_write_cmd(0xF6u)) {
        return;
    }
    if (!kbd_expect_ack()) {
        return;
    }

    ps2_drain_output();
    if (!kbd_write_cmd(0xF4u)) {
        return;
    }
    if (!kbd_expect_ack()) {
        return;
    }

    g_kernel_kbd_ready = 1u;
}

void kernel_keyboard_prepare_for_graphics(void) {
    uint32_t flags = kernel_irq_save();

    g_kernel_kbd_extended = 0u;
    ps2_drain_output();

    kernel_irq_restore(flags);
}
