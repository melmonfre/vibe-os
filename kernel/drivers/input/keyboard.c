#include <kernel/drivers/input/input.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/hal/io.h>
#include <kernel/interrupt.h>
#include <include/userland_api.h>
#include <headers/kernel/keymap.h>
#include <include/string.h>

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

static volatile uint8_t g_kernel_kbd_shift = 0u;
static volatile uint8_t g_kernel_kbd_ctrl = 0u;
static volatile uint8_t g_kernel_kbd_extended = 0u;
static volatile uint8_t g_kernel_kbd_ready = 0u;
static volatile uint32_t g_kernel_kbd_irq_trace_budget = 24u;
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

static int kernel_keyboard_apply_ctrl(char c, uint8_t ctrl) {
    char lower;

    if (!ctrl || c == '\0') {
        return (int)(uint8_t)c;
    }

    lower = c;
    if (lower >= 'A' && lower <= 'Z') {
        lower = (char)(lower - 'A' + 'a');
    }
    if (lower >= 'a' && lower <= 'z') {
        return (int)(uint8_t)(lower - 'a' + 1);
    }
    return (int)(uint8_t)c;
}

static int kernel_keyboard_translate_set1_scancode(uint8_t scancode,
                                                   uint8_t extended,
                                                   uint8_t shift,
                                                   uint8_t ctrl) {
    if (extended) {
        switch (scancode) {
        case 0x48u:
            return KEY_ARROW_UP;
        case 0x50u:
            return KEY_ARROW_DOWN;
        case 0x4Bu:
            return KEY_ARROW_LEFT;
        case 0x4Du:
            return KEY_ARROW_RIGHT;
        case 0x53u:
            return KEY_DELETE;
        default:
            return 0;
        }
    }

    if (scancode == 0u) {
        return 0;
    }
    return kernel_keyboard_apply_ctrl(shift ? g_current_keymap->shift_map[scancode] :
                                              g_current_keymap->map[scancode],
                                      ctrl);
}

int kernel_keyboard_translate_hid_usage(uint8_t usage, uint8_t modifiers) {
    static const uint8_t alpha_scancodes[26] = {
        0x1Eu, 0x30u, 0x2Eu, 0x20u, 0x12u, 0x21u, 0x22u, 0x23u, 0x17u, 0x24u,
        0x25u, 0x26u, 0x32u, 0x31u, 0x18u, 0x19u, 0x10u, 0x13u, 0x1Fu, 0x14u,
        0x16u, 0x2Fu, 0x11u, 0x2Du, 0x15u, 0x2Cu
    };
    static const uint8_t digit_scancodes[10] = {
        0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u, 0x09u, 0x0Au, 0x0Bu
    };
    uint8_t shift = (uint8_t)(((modifiers & 0x22u) != 0u) ? 1u : 0u);
    uint8_t ctrl = (uint8_t)(((modifiers & 0x11u) != 0u) ? 1u : 0u);

    if (usage >= 0x04u && usage <= 0x1Du) {
        return kernel_keyboard_translate_set1_scancode(alpha_scancodes[usage - 0x04u], 0u, shift, ctrl);
    }
    if (usage >= 0x1Eu && usage <= 0x27u) {
        return kernel_keyboard_translate_set1_scancode(digit_scancodes[usage - 0x1Eu], 0u, shift, ctrl);
    }

    switch (usage) {
    case 0x28u:
        return '\n';
    case 0x29u:
        return 27;
    case 0x2Au:
        return '\b';
    case 0x2Bu:
        return '\t';
    case 0x2Cu:
        return ' ';
    case 0x2Du:
        return kernel_keyboard_translate_set1_scancode(0x0Cu, 0u, shift, ctrl);
    case 0x2Eu:
        return kernel_keyboard_translate_set1_scancode(0x0Du, 0u, shift, ctrl);
    case 0x2Fu:
        return kernel_keyboard_translate_set1_scancode(0x1Au, 0u, shift, ctrl);
    case 0x30u:
        return kernel_keyboard_translate_set1_scancode(0x1Bu, 0u, shift, ctrl);
    case 0x31u:
        return kernel_keyboard_translate_set1_scancode(0x2Bu, 0u, shift, ctrl);
    case 0x33u:
        return kernel_keyboard_translate_set1_scancode(0x27u, 0u, shift, ctrl);
    case 0x34u:
        return kernel_keyboard_translate_set1_scancode(0x28u, 0u, shift, ctrl);
    case 0x35u:
        return kernel_keyboard_translate_set1_scancode(0x29u, 0u, shift, ctrl);
    case 0x36u:
        return kernel_keyboard_translate_set1_scancode(0x33u, 0u, shift, ctrl);
    case 0x37u:
        return kernel_keyboard_translate_set1_scancode(0x34u, 0u, shift, ctrl);
    case 0x38u:
        return kernel_keyboard_translate_set1_scancode(0x35u, 0u, shift, ctrl);
    case 0x4Cu:
        return KEY_DELETE;
    case 0x4Fu:
        return KEY_ARROW_RIGHT;
    case 0x50u:
        return KEY_ARROW_LEFT;
    case 0x51u:
        return KEY_ARROW_DOWN;
    case 0x52u:
        return KEY_ARROW_UP;
    default:
        return 0;
    }
}

static void kbd_push_key(uint16_t key) {
    kernel_input_key_event_enqueue((int)key);
}

static void kernel_keyboard_process_scancode(uint8_t scancode) {
    if (!g_kernel_kbd_ready) {
        return;
    }

    if (scancode == 0xE0u) {
        g_kernel_kbd_extended = 1u;
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
        return;
    }

    if (scancode == 0x2Au || scancode == 0x36u) {
        g_kernel_kbd_shift = 1u;
        return;
    }

    if (scancode == 0x1Du) {
        g_kernel_kbd_ctrl = 1u;
        return;
    }

    if (scancode == 0xAAu || scancode == 0xB6u) {
        g_kernel_kbd_shift = 0u;
        return;
    }

    if (scancode == 0x9Du) {
        g_kernel_kbd_ctrl = 0u;
        return;
    }

    if ((scancode & 0x80u) != 0u) {
        return;
    }

    if (scancode == 0xFAu || scancode == 0xFEu) {
        return;
    }

    {
        int key = kernel_keyboard_translate_set1_scancode(scancode, 0u, g_kernel_kbd_shift, g_kernel_kbd_ctrl);
        if (key != 0) {
            kbd_push_key((uint16_t)key);
        }
    }
}

int kernel_keyboard_read(void) {
    int value = 0;
    if (kernel_input_key_event_dequeue(&value) == 0) {
        return 0;
    }
    return value;
}

void kernel_keyboard_irq_handler(void) {
    const uint8_t status = inb(PS2_STATUS_PORT);
    uint8_t scancode;

    if ((status & PS2_STATUS_OUTPUT_FULL) == 0u ||
        (status & PS2_STATUS_AUX_OUTPUT_FULL) != 0u) {
        kernel_irq_complete(1);
        return;
    }

    scancode = inb(PS2_DATA_PORT);
    if (g_kernel_kbd_irq_trace_budget > 0u) {
        g_kernel_kbd_irq_trace_budget -= 1u;
        kernel_debug_printf("kbd: irq scancode=%d ext=%d ctrl=%d shift=%d ready=%d\n",
                            (int)scancode,
                            (int)g_kernel_kbd_extended,
                            (int)g_kernel_kbd_ctrl,
                            (int)g_kernel_kbd_shift,
                            (int)g_kernel_kbd_ready);
    }
    kernel_keyboard_process_scancode(scancode);
    kernel_irq_complete(1);
}

void kernel_keyboard_poll(void) {
    uint32_t flags = kernel_irq_save();

    while ((inb(PS2_STATUS_PORT) & (PS2_STATUS_OUTPUT_FULL | PS2_STATUS_AUX_OUTPUT_FULL)) ==
           PS2_STATUS_OUTPUT_FULL) {
        uint8_t scancode = inb(PS2_DATA_PORT);
        kernel_keyboard_process_scancode(scancode);
    }

    kernel_irq_restore(flags);
}

void kernel_keyboard_init(void) {
    uint8_t config;

    g_kernel_kbd_shift = 0u;
    g_kernel_kbd_ctrl = 0u;
    g_kernel_kbd_extended = 0u;
    g_kernel_kbd_ready = 0u;
    kernel_input_event_init();
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
    kernel_debug_puts("kbd: init ready\n");
}

void kernel_keyboard_prepare_for_graphics(void) {
    uint32_t flags = kernel_irq_save();

    g_kernel_kbd_extended = 0u;
    ps2_drain_output();

    kernel_irq_restore(flags);
}
