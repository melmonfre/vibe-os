#include <kernel/event.h>

#include <kernel/interrupt.h>
#include <kernel/process.h>
#include <kernel/scheduler.h>
#include <kernel/smp.h>
#include <kernel/drivers/timer/timer.h>

static void kernel_waitable_enqueue(kernel_waitable_t *waitable, process_t *task) {
    process_t *tail;

    if (waitable == 0 || task == 0) {
        return;
    }

    task->wait_next = 0;
    if (waitable->wait_head == 0) {
        waitable->wait_head = task;
        return;
    }

    tail = waitable->wait_head;
    while (tail->wait_next != 0) {
        tail = tail->wait_next;
    }
    tail->wait_next = task;
}

static process_t *kernel_waitable_dequeue(kernel_waitable_t *waitable) {
    process_t *task;

    if (waitable == 0 || waitable->wait_head == 0) {
        return 0;
    }

    task = waitable->wait_head;
    waitable->wait_head = task->wait_next;
    task->wait_next = 0;
    return task;
}

void kernel_waitable_detach_task(kernel_waitable_t *waitable, process_t *task) {
    process_t *prev = 0;
    process_t *cursor;
    uint32_t flags;

    if (waitable == 0 || task == 0) {
        return;
    }

    flags = spinlock_lock_irqsave(&waitable->lock);
    cursor = waitable->wait_head;
    while (cursor != 0) {
        if (cursor == task) {
            if (prev == 0) {
                waitable->wait_head = cursor->wait_next;
            } else {
                prev->wait_next = cursor->wait_next;
            }
            cursor->wait_next = 0;
            spinlock_unlock_irqrestore(&waitable->lock, flags);
            return;
        }
        prev = cursor;
        cursor = cursor->wait_next;
    }
    spinlock_unlock_irqrestore(&waitable->lock, flags);
}

void kernel_waitable_init_ex(kernel_waitable_t *waitable,
                             uint32_t event_kind,
                             uint32_t event_class,
                             uint32_t owner_service) {
    if (waitable == 0) {
        return;
    }

    spinlock_init(&waitable->lock);
    waitable->event_kind = event_kind;
    waitable->event_class = event_class;
    waitable->owner_service = owner_service;
    waitable->pending_signals = 0u;
    waitable->signal_count = 0u;
    waitable->wake_count = 0u;
    waitable->cancel_count = 0u;
    waitable->timeout_count = 0u;
    waitable->wait_head = 0;
}

void kernel_waitable_init(kernel_waitable_t *waitable) {
    kernel_waitable_init_ex(waitable,
                            TASK_WAIT_EVENT_WAITABLE,
                            TASK_WAIT_CLASS_GENERIC,
                            0u);
}

int kernel_waitable_try_wait(kernel_waitable_t *waitable) {
    uint32_t flags;
    int ready = -1;

    if (waitable == 0) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&waitable->lock);
    if (waitable->pending_signals != 0u) {
        waitable->pending_signals -= 1u;
        waitable->signal_count += 1u;
        ready = 0;
    }
    spinlock_unlock_irqrestore(&waitable->lock, flags);
    return ready;
}

int kernel_waitable_wait_timeout(kernel_waitable_t *waitable, uint32_t timeout_ticks) {
    process_t *current;
    uint32_t flags;
    uint32_t deadline = 0u;

    if (waitable == 0) {
        return -1;
    }

    current = scheduler_current();
    if (current == 0) {
        return -1;
    }
    if (timeout_ticks != 0u) {
        deadline = kernel_timer_get_ticks() + timeout_ticks;
    }

    for (;;) {
        flags = spinlock_lock_irqsave(&waitable->lock);
        if (waitable->pending_signals != 0u) {
            waitable->pending_signals -= 1u;
            waitable->signal_count += 1u;
            current->wait_result = TASK_WAIT_RESULT_SIGNALED;
            spinlock_unlock_irqrestore(&waitable->lock, flags);
            return TASK_WAIT_RESULT_SIGNALED;
        }

        if (current->wait_channel != waitable) {
            current->wait_channel = waitable;
            kernel_waitable_enqueue(waitable, current);
        }
        spinlock_unlock_irqrestore(&waitable->lock, flags);

        if (scheduler_block_current_ex(waitable,
                                       deadline,
                                       waitable->event_kind,
                                       waitable->event_class,
                                       waitable->owner_service) != 0) {
            return -1;
        }
        yield();

        if (current->wait_result == TASK_WAIT_RESULT_SIGNALED) {
            waitable->wake_count += 1u;
            return TASK_WAIT_RESULT_SIGNALED;
        }
        if (current->wait_result == TASK_WAIT_RESULT_TIMED_OUT) {
            waitable->timeout_count += 1u;
            return TASK_WAIT_RESULT_TIMED_OUT;
        }
        if (current->wait_result == TASK_WAIT_RESULT_CANCELED) {
            waitable->cancel_count += 1u;
            return TASK_WAIT_RESULT_CANCELED;
        }
    }
}

int kernel_waitable_wait(kernel_waitable_t *waitable) {
    return kernel_waitable_wait_timeout(waitable, 0u);
}

void kernel_waitable_signal(kernel_waitable_t *waitable, uint32_t count) {
    uint32_t flags;
    uint32_t woke = 0u;

    if (waitable == 0 || count == 0u) {
        return;
    }

    flags = spinlock_lock_irqsave(&waitable->lock);
    waitable->pending_signals += count;
    spinlock_unlock_irqrestore(&waitable->lock, flags);

    while (count != 0u) {
        process_t *task;

        flags = spinlock_lock_irqsave(&waitable->lock);
        task = kernel_waitable_dequeue(waitable);
        if (task != 0) {
            task->wait_channel = 0;
        }
        spinlock_unlock_irqrestore(&waitable->lock, flags);

        if (task == 0) {
            break;
        }
        if (scheduler_complete_wait(task, TASK_WAIT_RESULT_SIGNALED) == 0) {
            count -= 1u;
            woke += 1u;
        }
    }

    if (woke != 0u) {
        smp_wake_sleeping_cpus();
    }
}

void kernel_waitable_cancel(kernel_waitable_t *waitable, uint32_t count) {
    uint32_t flags;
    uint32_t woke = 0u;

    if (waitable == 0 || count == 0u) {
        return;
    }

    while (count != 0u) {
        process_t *task;

        flags = spinlock_lock_irqsave(&waitable->lock);
        task = kernel_waitable_dequeue(waitable);
        if (task != 0) {
            task->wait_channel = 0;
        }
        spinlock_unlock_irqrestore(&waitable->lock, flags);
        if (task == 0) {
            break;
        }
        if (scheduler_complete_wait(task, TASK_WAIT_RESULT_CANCELED) == 0) {
            count -= 1u;
            woke += 1u;
        }
    }

    if (woke != 0u) {
        smp_wake_sleeping_cpus();
    }
}

void kernel_signal_init(kernel_signal_t *signal, uint32_t event_class, uint32_t owner_service) {
    if (signal == 0) {
        return;
    }
    kernel_waitable_init_ex(&signal->waitable,
                            TASK_WAIT_EVENT_SIGNAL,
                            event_class,
                            owner_service);
}

void kernel_signal_raise(kernel_signal_t *signal, uint32_t count) {
    if (signal == 0) {
        return;
    }
    kernel_waitable_signal(&signal->waitable, count);
}

void kernel_signal_cancel(kernel_signal_t *signal, uint32_t count) {
    if (signal == 0) {
        return;
    }
    kernel_waitable_cancel(&signal->waitable, count);
}

int kernel_signal_wait(kernel_signal_t *signal, uint32_t timeout_ticks) {
    if (signal == 0) {
        return -1;
    }
    return kernel_waitable_wait_timeout(&signal->waitable, timeout_ticks);
}

void kernel_completion_init(kernel_completion_t *completion, uint32_t event_class, uint32_t owner_service) {
    if (completion == 0) {
        return;
    }
    kernel_waitable_init_ex(&completion->waitable,
                            TASK_WAIT_EVENT_COMPLETION,
                            event_class,
                            owner_service);
}

void kernel_completion_complete(kernel_completion_t *completion) {
    if (completion == 0) {
        return;
    }
    kernel_waitable_signal(&completion->waitable, 1u);
}

void kernel_completion_cancel(kernel_completion_t *completion) {
    if (completion == 0) {
        return;
    }
    kernel_waitable_cancel(&completion->waitable, 1u);
}

int kernel_completion_wait(kernel_completion_t *completion, uint32_t timeout_ticks) {
    if (completion == 0) {
        return -1;
    }
    return kernel_waitable_wait_timeout(&completion->waitable, timeout_ticks);
}
