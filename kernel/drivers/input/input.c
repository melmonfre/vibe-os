#include <kernel/drivers/input/input.h>

#include <kernel/drivers/usb/usb_host.h>
#include <kernel/event.h>
#include <kernel/kernel_string.h>
#include <kernel/microkernel/service.h>
#include <kernel/smp.h>

#define KERNEL_INPUT_EVENT_QUEUE_CAPACITY 256u
#define KERNEL_INPUT_KEY_QUEUE_CAPACITY 128u
#define KERNEL_INPUT_MOUSE_QUEUE_CAPACITY 128u

static struct input_event g_input_event_storage[KERNEL_INPUT_EVENT_QUEUE_CAPACITY];
static kernel_mailbox_t g_input_event_mailbox;
static int g_input_key_storage[KERNEL_INPUT_KEY_QUEUE_CAPACITY];
static kernel_mailbox_t g_input_key_mailbox;
static struct mouse_state g_input_mouse_storage[KERNEL_INPUT_MOUSE_QUEUE_CAPACITY];
static kernel_mailbox_t g_input_mouse_mailbox;

void kernel_input_event_init(void) {
    memset(g_input_event_storage, 0, sizeof(g_input_event_storage));
    memset(g_input_key_storage, 0, sizeof(g_input_key_storage));
    memset(g_input_mouse_storage, 0, sizeof(g_input_mouse_storage));
    kernel_mailbox_init(&g_input_event_mailbox,
                        g_input_event_storage,
                        sizeof(g_input_event_storage[0]),
                        KERNEL_INPUT_EVENT_QUEUE_CAPACITY,
                        KERNEL_MAILBOX_DROP_OLDEST,
                        TASK_WAIT_CLASS_INPUT,
                        MK_SERVICE_INPUT);
    kernel_mailbox_init(&g_input_key_mailbox,
                        g_input_key_storage,
                        sizeof(g_input_key_storage[0]),
                        KERNEL_INPUT_KEY_QUEUE_CAPACITY,
                        KERNEL_MAILBOX_DROP_OLDEST,
                        TASK_WAIT_CLASS_INPUT,
                        MK_SERVICE_INPUT);
    kernel_mailbox_init(&g_input_mouse_mailbox,
                        g_input_mouse_storage,
                        sizeof(g_input_mouse_storage[0]),
                        KERNEL_INPUT_MOUSE_QUEUE_CAPACITY,
                        KERNEL_MAILBOX_DROP_OLDEST,
                        TASK_WAIT_CLASS_INPUT,
                        MK_SERVICE_INPUT);
}

int kernel_input_event_has_data(void) {
    return kernel_mailbox_count(&g_input_event_mailbox) != 0u;
}

int kernel_input_event_dequeue(struct input_event *event) {
    if (event == 0) {
        return 0;
    }

    if (kernel_mailbox_try_receive(&g_input_event_mailbox, event) != 0) {
        kernel_keyboard_poll();
        kernel_mouse_poll();
        kernel_usb_hid_poll();
        if (kernel_mailbox_try_receive(&g_input_event_mailbox, event) != 0) {
            memset(event, 0, sizeof(*event));
            return 0;
        }
    }
    return 1;
}

int kernel_input_event_wait(struct input_event *event) {
    int wait_rc;

    if (event == 0) {
        return 0;
    }

    for (;;) {
        if (kernel_input_event_dequeue(event) != 0) {
            return 1;
        }
        wait_rc = kernel_mailbox_wait(&g_input_event_mailbox, 0u);
        if (wait_rc != TASK_WAIT_RESULT_SIGNALED) {
            return 0;
        }
    }
}

void kernel_input_event_enqueue_key(int key) {
    struct input_event event;

    if (key == 0) {
        return;
    }
    memset(&event, 0, sizeof(event));
    event.type = INPUT_EVENT_KEY;
    event.value = key;
    (void)kernel_mailbox_try_send(&g_input_event_mailbox, &event);
    (void)kernel_mailbox_try_send(&g_input_key_mailbox, &key);
    smp_wake_sleeping_cpus();
}

void kernel_input_event_enqueue_mouse(const struct mouse_state *state) {
    struct input_event event;

    if (state == 0) {
        return;
    }

    memset(&event, 0, sizeof(event));
    event.type = INPUT_EVENT_MOUSE;
    event.mouse = *state;
    (void)kernel_mailbox_try_send(&g_input_event_mailbox, &event);
    (void)kernel_mailbox_try_send(&g_input_mouse_mailbox, state);
    smp_wake_sleeping_cpus();
}

int kernel_input_key_event_has_data(void) {
    return kernel_mailbox_count(&g_input_key_mailbox) != 0u;
}

int kernel_input_key_event_dequeue(int *key) {
    if (key == 0) {
        return 0;
    }

    if (kernel_mailbox_try_receive(&g_input_key_mailbox, key) != 0) {
        kernel_keyboard_poll();
        kernel_usb_hid_poll();
        if (kernel_mailbox_try_receive(&g_input_key_mailbox, key) != 0) {
            *key = 0;
            return 0;
        }
    }
    return 1;
}

int kernel_input_key_event_wait(int *key) {
    int wait_rc;

    if (key == 0) {
        return 0;
    }

    for (;;) {
        if (kernel_input_key_event_dequeue(key) != 0) {
            return 1;
        }
        wait_rc = kernel_mailbox_wait(&g_input_key_mailbox, 0u);
        if (wait_rc != TASK_WAIT_RESULT_SIGNALED) {
            return 0;
        }
    }
}

void kernel_input_key_event_enqueue(int key) {
    kernel_input_event_enqueue_key(key);
}

int kernel_input_mouse_event_has_data(void) {
    return kernel_mailbox_count(&g_input_mouse_mailbox) != 0u;
}

int kernel_input_mouse_event_dequeue(struct mouse_state *state) {
    if (state == 0) {
        return 0;
    }

    if (kernel_mailbox_try_receive(&g_input_mouse_mailbox, state) != 0) {
        kernel_mouse_poll();
        kernel_usb_hid_poll();
        if (kernel_mailbox_try_receive(&g_input_mouse_mailbox, state) != 0) {
            memset(state, 0, sizeof(*state));
            return 0;
        }
    }
    return 1;
}

int kernel_input_mouse_event_wait(struct mouse_state *state) {
    int wait_rc;

    if (state == 0) {
        return 0;
    }

    for (;;) {
        if (kernel_input_mouse_event_dequeue(state) != 0) {
            return 1;
        }
        wait_rc = kernel_mailbox_wait(&g_input_mouse_mailbox, 0u);
        if (wait_rc != TASK_WAIT_RESULT_SIGNALED) {
            return 0;
        }
    }
}

void kernel_input_mouse_event_enqueue(const struct mouse_state *state) {
    kernel_input_event_enqueue_mouse(state);
}
