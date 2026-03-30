#include <kernel/drivers/input/input.h>

#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/usb/usb_host.h>
#include <kernel/event.h>
#include <kernel/interrupt.h>
#include <kernel/kernel_string.h>
#include <kernel/microkernel/service.h>
#include <kernel/smp.h>

#define KERNEL_INPUT_EVENT_QUEUE_CAPACITY 256u
#define KERNEL_INPUT_KEY_QUEUE_CAPACITY 128u
#define KERNEL_INPUT_MOUSE_QUEUE_CAPACITY 128u

static struct input_event g_input_event_queue[KERNEL_INPUT_EVENT_QUEUE_CAPACITY];
static volatile uint16_t g_input_event_head = 0u;
static volatile uint16_t g_input_event_tail = 0u;
static kernel_waitable_t g_input_event_waitable;
static int g_input_key_queue[KERNEL_INPUT_KEY_QUEUE_CAPACITY];
static volatile uint16_t g_input_key_head = 0u;
static volatile uint16_t g_input_key_tail = 0u;
static kernel_waitable_t g_input_key_waitable;
static struct mouse_state g_input_mouse_queue[KERNEL_INPUT_MOUSE_QUEUE_CAPACITY];
static volatile uint16_t g_input_mouse_head = 0u;
static volatile uint16_t g_input_mouse_tail = 0u;
static kernel_waitable_t g_input_mouse_waitable;

static void kernel_input_event_push_unlocked(const struct input_event *event) {
    uint16_t next;

    if (event == 0) {
        return;
    }

    next = (uint16_t)((g_input_event_head + 1u) % KERNEL_INPUT_EVENT_QUEUE_CAPACITY);
    if (next == g_input_event_tail) {
        g_input_event_tail = (uint16_t)((g_input_event_tail + 1u) % KERNEL_INPUT_EVENT_QUEUE_CAPACITY);
    }

    g_input_event_queue[g_input_event_head] = *event;
    g_input_event_head = next;
}

static void kernel_input_key_push_unlocked(int key) {
    uint16_t next;

    next = (uint16_t)((g_input_key_head + 1u) % KERNEL_INPUT_KEY_QUEUE_CAPACITY);
    if (next == g_input_key_tail) {
        g_input_key_tail = (uint16_t)((g_input_key_tail + 1u) % KERNEL_INPUT_KEY_QUEUE_CAPACITY);
    }

    g_input_key_queue[g_input_key_head] = key;
    g_input_key_head = next;
}

static void kernel_input_mouse_push_unlocked(const struct mouse_state *state) {
    uint16_t next;

    if (state == 0) {
        return;
    }

    next = (uint16_t)((g_input_mouse_head + 1u) % KERNEL_INPUT_MOUSE_QUEUE_CAPACITY);
    if (next == g_input_mouse_tail) {
        g_input_mouse_tail = (uint16_t)((g_input_mouse_tail + 1u) % KERNEL_INPUT_MOUSE_QUEUE_CAPACITY);
    }

    g_input_mouse_queue[g_input_mouse_head] = *state;
    g_input_mouse_head = next;
}

void kernel_input_event_init(void) {
    uint32_t flags = kernel_irq_save();

    memset(g_input_event_queue, 0, sizeof(g_input_event_queue));
    g_input_event_head = 0u;
    g_input_event_tail = 0u;
    memset(g_input_key_queue, 0, sizeof(g_input_key_queue));
    g_input_key_head = 0u;
    g_input_key_tail = 0u;
    memset(g_input_mouse_queue, 0, sizeof(g_input_mouse_queue));
    g_input_mouse_head = 0u;
    g_input_mouse_tail = 0u;
    kernel_waitable_init_ex(&g_input_event_waitable,
                            TASK_WAIT_EVENT_QUEUE,
                            TASK_WAIT_CLASS_INPUT,
                            MK_SERVICE_INPUT);
    kernel_waitable_init_ex(&g_input_key_waitable,
                            TASK_WAIT_EVENT_QUEUE,
                            TASK_WAIT_CLASS_INPUT,
                            MK_SERVICE_INPUT);
    kernel_waitable_init_ex(&g_input_mouse_waitable,
                            TASK_WAIT_EVENT_QUEUE,
                            TASK_WAIT_CLASS_INPUT,
                            MK_SERVICE_INPUT);
    kernel_irq_restore(flags);
}

int kernel_input_event_dequeue(struct input_event *event) {
    uint32_t flags;

    if (event == 0) {
        return 0;
    }

    flags = kernel_irq_save();
    if (g_input_event_tail == g_input_event_head) {
        kernel_irq_restore(flags);
        kernel_keyboard_poll();
        kernel_mouse_poll();
        kernel_usb_hid_poll();
        flags = kernel_irq_save();
        if (g_input_event_tail == g_input_event_head) {
            memset(event, 0, sizeof(*event));
            kernel_irq_restore(flags);
            return 0;
        }
    }

    *event = g_input_event_queue[g_input_event_tail];
    g_input_event_tail = (uint16_t)((g_input_event_tail + 1u) % KERNEL_INPUT_EVENT_QUEUE_CAPACITY);
    kernel_irq_restore(flags);
    return 1;
}

int kernel_input_event_wait(struct input_event *event) {
    if (event == 0) {
        return 0;
    }

    for (;;) {
        if (kernel_input_event_dequeue(event) != 0) {
            return 1;
        }
        if (kernel_waitable_wait(&g_input_event_waitable) != 0) {
            return 0;
        }
    }
}

void kernel_input_event_enqueue_key(int key) {
    struct input_event event;
    uint32_t flags;

    if (key == 0) {
        return;
    }

    memset(&event, 0, sizeof(event));
    event.type = INPUT_EVENT_KEY;
    event.value = key;

    flags = kernel_irq_save();
    kernel_input_event_push_unlocked(&event);
    kernel_input_key_push_unlocked(key);
    kernel_irq_restore(flags);
    kernel_waitable_signal(&g_input_event_waitable, 1u);
    kernel_waitable_signal(&g_input_key_waitable, 1u);
    smp_wake_sleeping_cpus();
}

void kernel_input_event_enqueue_mouse(const struct mouse_state *state) {
    struct input_event event;
    uint32_t flags;

    if (state == 0) {
        return;
    }

    memset(&event, 0, sizeof(event));
    event.type = INPUT_EVENT_MOUSE;
    event.mouse = *state;

    flags = kernel_irq_save();
    kernel_input_event_push_unlocked(&event);
    kernel_input_mouse_push_unlocked(state);
    kernel_irq_restore(flags);
    kernel_waitable_signal(&g_input_event_waitable, 1u);
    kernel_waitable_signal(&g_input_mouse_waitable, 1u);
    smp_wake_sleeping_cpus();
}

int kernel_input_key_event_has_data(void) {
    uint32_t flags;
    int ready;

    flags = kernel_irq_save();
    ready = g_input_key_tail != g_input_key_head;
    kernel_irq_restore(flags);
    return ready;
}

int kernel_input_key_event_dequeue(int *key) {
    uint32_t flags;

    if (key == 0) {
        return 0;
    }

    flags = kernel_irq_save();
    if (g_input_key_tail == g_input_key_head) {
        kernel_irq_restore(flags);
        kernel_keyboard_poll();
        kernel_usb_hid_poll();
        flags = kernel_irq_save();
        if (g_input_key_tail == g_input_key_head) {
            *key = 0;
            kernel_irq_restore(flags);
            return 0;
        }
    }

    *key = g_input_key_queue[g_input_key_tail];
    g_input_key_tail = (uint16_t)((g_input_key_tail + 1u) % KERNEL_INPUT_KEY_QUEUE_CAPACITY);
    kernel_irq_restore(flags);
    return 1;
}

int kernel_input_key_event_wait(int *key) {
    if (key == 0) {
        return 0;
    }

    for (;;) {
        if (kernel_input_key_event_dequeue(key) != 0) {
            return 1;
        }
        if (kernel_waitable_wait(&g_input_key_waitable) != 0) {
            return 0;
        }
    }
}

void kernel_input_key_event_enqueue(int key) {
    kernel_input_event_enqueue_key(key);
}

int kernel_input_mouse_event_has_data(void) {
    uint32_t flags;
    int ready;

    flags = kernel_irq_save();
    ready = g_input_mouse_tail != g_input_mouse_head;
    kernel_irq_restore(flags);
    return ready;
}

int kernel_input_mouse_event_dequeue(struct mouse_state *state) {
    uint32_t flags;

    if (state == 0) {
        return 0;
    }

    flags = kernel_irq_save();
    if (g_input_mouse_tail == g_input_mouse_head) {
        kernel_irq_restore(flags);
        kernel_mouse_poll();
        kernel_usb_hid_poll();
        flags = kernel_irq_save();
        if (g_input_mouse_tail == g_input_mouse_head) {
            memset(state, 0, sizeof(*state));
            kernel_irq_restore(flags);
            return 0;
        }
    }

    *state = g_input_mouse_queue[g_input_mouse_tail];
    g_input_mouse_tail = (uint16_t)((g_input_mouse_tail + 1u) % KERNEL_INPUT_MOUSE_QUEUE_CAPACITY);
    kernel_irq_restore(flags);
    return 1;
}

int kernel_input_mouse_event_wait(struct mouse_state *state) {
    if (state == 0) {
        return 0;
    }

    for (;;) {
        if (kernel_input_mouse_event_dequeue(state) != 0) {
            return 1;
        }
        if (kernel_waitable_wait(&g_input_mouse_waitable) != 0) {
            return 0;
        }
    }
}

void kernel_input_mouse_event_enqueue(const struct mouse_state *state) {
    kernel_input_event_enqueue_mouse(state);
}
