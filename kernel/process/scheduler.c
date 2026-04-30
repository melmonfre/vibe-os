#include <stdint.h>
#include <stddef.h>
#include <kernel/kernel_string.h>  /* memcpy/memset helpers */

#include <kernel/scheduler.h>
#include <kernel/process.h>
#include <kernel/kernel.h>    /* for panic, if needed */
#include <kernel/lock.h>
#include <kernel/cpu/cpu.h>
#include <kernel/smp.h>
#include <kernel/drivers/debug/debug.h>
#include <kernel/drivers/timer/timer.h>
#include <kernel/drivers/video/video.h>
#include <kernel/event.h>
#include <kernel/microkernel/launch.h>
#include <kernel/microkernel/service.h>

/* simple singly‑linked list of processes */
static process_t *g_head = NULL;
static process_t *g_current[32];
static process_t *g_cursor[32];
static uint32_t g_timeslice_remaining[32];
static spinlock_t g_scheduler_lock;
static spinlock_t g_task_event_lock;
static int g_scheduler_boot_trace_emitted = 0;
static int g_scheduler_switch_trace_budget = 24;
static int g_scheduler_block_trace_budget = 24;
static int g_scheduler_timeout_trace_budget = 16;
static volatile int g_scheduler_preemption_ready = 0;
static volatile int g_scheduler_task_events_ready = 0;
static uint8_t g_audio_task_added_trace_emitted = 0u;
static uint8_t g_audio_first_dispatch_trace_emitted = 0u;

#define SCHEDULER_TASK_EVENT_SUBSCRIBERS 8u
#define SCHEDULER_TASK_EVENT_QUEUE_SIZE 32u
#define SCHEDULER_TASK_CLASS_EVENT_QUEUE_SIZE 32u
#define SCHEDULER_TASK_CLASS_STREAM_COUNT (MK_TASK_CLASS_VIDEO_CONTROL + 1u)

#define SCHEDULER_TIMESLICE_TICKS 4u

struct scheduler_task_event_subscription {
    uint32_t pid;
    uint32_t event_mask;
    uint32_t task_class_mask;
    kernel_mailbox_t mailbox;
    struct mk_task_event events[SCHEDULER_TASK_EVENT_QUEUE_SIZE];
};

struct scheduler_task_class_stream {
    kernel_mailbox_t mailbox;
    struct mk_task_event events[SCHEDULER_TASK_CLASS_EVENT_QUEUE_SIZE];
};

static struct scheduler_task_event_subscription
    g_task_event_subscriptions[SCHEDULER_TASK_EVENT_SUBSCRIBERS];
static struct scheduler_task_class_stream
    g_task_class_streams[SCHEDULER_TASK_CLASS_STREAM_COUNT];
static uint32_t g_task_event_sequence = 0u;

static void scheduler_timeout_tick_hook(uint32_t tick);
static void scheduler_task_event_init(void);
static void scheduler_publish_task_event(uint32_t event_type, const process_t *task);
static uint32_t scheduler_task_event_mask_for_type(uint32_t event_type);
static uint32_t scheduler_task_class_mask_for_task(const process_t *task);
static uint32_t scheduler_wait_class_for_task_class(uint32_t task_class);
static struct scheduler_task_class_stream *
scheduler_task_class_stream_for_class(uint32_t task_class);
static uint32_t scheduler_task_class_pending_events(uint32_t task_class);
static uint32_t scheduler_task_class_dropped_events(uint32_t task_class);
static struct scheduler_task_event_subscription *
scheduler_find_task_event_subscription_locked(process_t *subscriber);
static struct scheduler_task_event_subscription *
scheduler_alloc_task_event_subscription_locked(process_t *subscriber);

static int scheduler_keep_wait_boost(const process_t *task) {
    return task != NULL && task->wake_boost_budget != 0u;
}

static void scheduler_account_runtime(process_t *task, uint32_t now_ticks) {
    if (task == NULL || task->state != PROCESS_RUNNING || task->last_start_tick == 0u) {
        return;
    }

    if (now_ticks >= task->last_start_tick) {
        task->runtime_ticks += (now_ticks - task->last_start_tick);
    }
    task->last_start_tick = 0u;
}

static void scheduler_trace_switch(uint32_t cpu_index,
                                   const process_t *old_task,
                                   const process_t *next_task) {
    if (g_scheduler_switch_trace_budget <= 0 || next_task == NULL) {
        return;
    }

    g_scheduler_switch_trace_budget -= 1;
    kernel_debug_printf("scheduler: cpu=%d old=%d/%d next=%d/%d state=%d\n",
                        (int)cpu_index,
                        old_task != NULL ? old_task->pid : -1,
                        old_task != NULL ? (int)old_task->service_type : -1,
                        next_task->pid,
                        (int)next_task->service_type,
                        (int)next_task->state);
}

static kernel_trap_frame_t *scheduler_sanitize_resume_frame(uint32_t cpu_index,
                                                            const process_t *old_task,
                                                            const process_t *next_task,
                                                            kernel_trap_frame_t *frame) {
    uint32_t original_cs;
    uint32_t original_eflags;
    uint32_t expected_cs;
    int expect_user_mode;

    if (frame == NULL) {
        return NULL;
    }

    original_cs = frame->cs;
    original_eflags = frame->eflags;
    int frame_is_user_mode = process_frame_is_user_mode(frame);
    expected_cs = frame_is_user_mode ? USER_CS_SELECTOR : KERNEL_CS_SELECTOR;

    if (next_task != NULL && next_task->stack != NULL) {
        kernel_tss_set_kernel_stack((uintptr_t)next_task->stack + next_task->stack_size);
    } else {
        kernel_tss_set_kernel_stack(0u);
    }

    if (!frame_is_user_mode) {
        if (original_cs != KERNEL_CS_SELECTOR ||
            (original_eflags & (0x4000u | 0x3000u)) != 0u) {
            kernel_debug_printf("scheduler: sanitize cpu=%d old=%d next=%d frame=%x eip=%x cs=%x eflags=%x stack=%x size=%u\n",
                                (int)cpu_index,
                                old_task != NULL ? old_task->pid : -1,
                                next_task != NULL ? next_task->pid : -1,
                                (unsigned int)(uintptr_t)frame,
                                (unsigned int)frame->eip,
                                (unsigned int)original_cs,
                                (unsigned int)original_eflags,
                                next_task != NULL ? (unsigned int)(uintptr_t)next_task->stack : 0u,
                                next_task != NULL ? (unsigned int)next_task->stack_size : 0u);
            frame->cs = KERNEL_CS_SELECTOR;
            frame->eflags = (original_eflags | 0x00000202u) & ~(0x4000u | 0x3000u);
        }
    } else {
        kernel_user_trap_frame_t *user_frame = (kernel_user_trap_frame_t *)(void *)frame;
        if (original_cs != expected_cs) {
            kernel_debug_printf("scheduler: user-frame mismatch cpu=%d old=%d next=%d frame=%x eip=%x cs=%x expected=%x esp_dummy=%x user_esp=%x user_ss=%x\n",
                                (int)cpu_index,
                                old_task != NULL ? old_task->pid : -1,
                                next_task != NULL ? next_task->pid : -1,
                                (unsigned int)(uintptr_t)frame,
                                (unsigned int)frame->eip,
                                (unsigned int)original_cs,
                                (unsigned int)expected_cs,
                                (unsigned int)frame->esp_dummy,
                                (unsigned int)user_frame->user_esp,
                                (unsigned int)user_frame->user_ss);
        } else {
            user_frame->user_ss = USER_DS_SELECTOR;
            frame->eflags = (original_eflags | 0x00000202u) & ~(0x4000u | 0x3000u);
        }
    }
    return frame;
}

static uint32_t scheduler_online_cpu_count(void) {
    if (!smp_scheduler_enabled()) {
        return 1u;
    }

    uint32_t count = smp_started_cpu_count();

    if (count == 0u) {
        count = 1u;
    }
    if (count > kernel_cpu_count()) {
        count = kernel_cpu_count();
    }
    if (count == 0u) {
        count = 1u;
    }
    return count;
}

static uint32_t scheduler_cpu_load(uint32_t cpu_index) {
    process_t *task;
    uint32_t load = 0u;

    for (task = g_head; task != NULL; task = task->next) {
        if (task->state == PROCESS_TERMINATED) {
            continue;
        }
        if (task->current_cpu == (int)cpu_index || task->preferred_cpu == (int)cpu_index) {
            load += 1u;
        }
    }
    return load;
}

static int scheduler_pick_target_cpu(void) {
    uint32_t cpu_count = scheduler_online_cpu_count();
    uint32_t best_cpu = 0u;
    uint32_t best_load = 0u;
    uint32_t cpu;

    for (cpu = 0u; cpu < cpu_count; ++cpu) {
        uint32_t load = scheduler_cpu_load(cpu);

        if (cpu == 0u || load < best_load) {
            best_cpu = cpu;
            best_load = load;
        }
    }

    return (int)best_cpu;
}

static process_t *scheduler_first_runnable(void) {
    process_t *task;

    for (task = g_head; task != NULL; task = task->next) {
        if (task->state == PROCESS_READY && task->current_cpu < 0) {
            return task;
        }
    }
    return NULL;
}

static int scheduler_task_score(const process_t *task, uint32_t cpu_index) {
    int wait_bonus = 0;
    int wait_penalty = 0;
    int affinity_score;
    int base_score;

    if (task == NULL) {
        return 0x7fffffff;
    }
    affinity_score = 3;
    if (task->preferred_cpu == (int)cpu_index) {
        affinity_score = 0;
    } else if (task->last_cpu == (int)cpu_index) {
        affinity_score = 1;
    } else if (task->preferred_cpu < 0) {
        affinity_score = 2;
    }

    /*
     * Protect the most interactive classes from paying a large migration cost
     * under wakeup/restart pressure. They should remain responsive even when
     * load-balancing would otherwise move them away from the previous CPU.
     */
    if (task->priority_tier <= PROCESS_PRIORITY_INPUT && affinity_score > 1) {
        affinity_score = 1;
    } else if (task->priority_tier == PROCESS_PRIORITY_VIDEO && affinity_score > 2) {
        affinity_score = 2;
    }

    /*
     * Fresh service workers need one bootstrap slice before desktop/user tasks
     * dominate the queue, otherwise first-use IPC (audio/network especially)
     * can target a worker that never had the chance to enter its receive loop.
     */
    if (task->kind == PROCESS_KIND_SERVICE &&
        task->context_switches == 0u &&
        task->runtime_ticks == 0u) {
        return -40 + (int)task->priority_tier;
    }
    if (task->kind == PROCESS_KIND_USER &&
        task->context_switches == 0u &&
        task->runtime_ticks == 0u) {
        /*
         * Fresh user tasks need an immediate bootstrap slice before desktop
         * frame/input traffic dominates the ready queue. Newly launched app
         * runtimes are particularly sensitive here because they still need to
         * bring up the loader and hand control to the modular app entrypoint.
         *
         * Audio/network helpers also need an early slice or the desktop can
         * launch them successfully yet keep rendering/input busy long enough
         * that bootstrap WAVs and async reconnect work appear "hung".
         */
        if (task->priority_tier == PROCESS_PRIORITY_DESKTOP_USER) {
            return -36;
        }
        if (task->priority_tier == PROCESS_PRIORITY_AUDIO ||
            task->priority_tier == PROCESS_PRIORITY_NETWORK) {
            return -30;
        }
        if (task->priority_tier == PROCESS_PRIORITY_APP) {
            return -34;
        }
        return -12;
    }
    if (task->kind == PROCESS_KIND_USER &&
        task->priority_tier == PROCESS_PRIORITY_APP &&
        task->context_switches < 4u &&
        task->runtime_ticks < 16u) {
        /*
         * Let freshly launched app runtimes finish their initial loader/fs work
         * over the first few slices so they do not starve behind continuous
         * desktop/input wakeups on graphical boots.
         */
        return -26;
    }

    /*
     * Recently signaled tasks that were blocked on interactive queues should
     * run ahead of bulk/background work once they become ready again.
     */
    if (task->wait_result == TASK_WAIT_RESULT_SIGNALED) {
        if (task->wait_event_class == TASK_WAIT_CLASS_IPC &&
            task->wake_boost_budget != 0u) {
            /*
             * Synchronous service request/reply depends on both sides of the
             * mailbox running immediately once signaled, otherwise a top-tier
             * caller can block indefinitely behind unrelated ready work.
             */
            return task->kind == PROCESS_KIND_SERVICE ? -32 : -24;
        }
        switch (task->wait_event_class) {
        case TASK_WAIT_CLASS_INPUT:
            wait_bonus = 12;
            break;
        case TASK_WAIT_CLASS_VIDEO:
            wait_bonus = 8;
            break;
        case TASK_WAIT_CLASS_SUPERVISION:
            wait_bonus = 6;
            break;
        case TASK_WAIT_CLASS_STORAGE:
        case TASK_WAIT_CLASS_FILESYSTEM:
            wait_bonus = 4;
            break;
        case TASK_WAIT_CLASS_AUDIO:
        case TASK_WAIT_CLASS_NETWORK:
            wait_bonus = 2;
            break;
        default:
            break;
        }
    } else if (task->wait_result == TASK_WAIT_RESULT_TIMED_OUT &&
               task->wait_event_class == TASK_WAIT_CLASS_IPC) {
        /*
         * A caller that is only timing out on a synchronous service retry
         * should not outrank the service task that must run to satisfy it.
         */
        wait_penalty = 12;
    }

    base_score = (int)(task->priority_tier * 16u) + affinity_score + wait_penalty - wait_bonus;
    if (base_score < 0) {
        base_score = 0;
    }
    return base_score;
}

static void scheduler_clear_wait_channel(process_t *task) {
    if (task == NULL) {
        return;
    }

    task->wait_channel = 0;
    task->wait_deadline = 0u;
    task->wait_next = 0;
    task->wake_boost_budget = 0u;
}

void scheduler_init(void) {
    uint32_t i;

    g_head = NULL;
    for (i = 0; i < 32u; ++i) {
        g_current[i] = NULL;
        g_cursor[i] = NULL;
        g_timeslice_remaining[i] = SCHEDULER_TIMESLICE_TICKS;
    }
    spinlock_init(&g_scheduler_lock);
    g_scheduler_preemption_ready = 0;
    g_scheduler_task_events_ready = 0;
    scheduler_task_event_init();
    (void)kernel_timer_register_tick_hook(scheduler_timeout_tick_hook);
}

static void scheduler_task_event_init(void) {
    uint32_t index;

    spinlock_init(&g_task_event_lock);
    g_task_event_sequence = 0u;
    for (index = 0u; index < SCHEDULER_TASK_EVENT_SUBSCRIBERS; ++index) {
        struct scheduler_task_event_subscription *subscription =
            &g_task_event_subscriptions[index];

        memset(subscription, 0, sizeof(*subscription));
        kernel_mailbox_init(&subscription->mailbox,
                            subscription->events,
                            sizeof(subscription->events[0]),
                            SCHEDULER_TASK_EVENT_QUEUE_SIZE,
                            KERNEL_MAILBOX_DROP_OLDEST,
                            TASK_WAIT_CLASS_SUPERVISION,
                            0u);
    }
    for (index = 0u; index < SCHEDULER_TASK_CLASS_STREAM_COUNT; ++index) {
        struct scheduler_task_class_stream *stream = &g_task_class_streams[index];

        memset(stream, 0, sizeof(*stream));
        kernel_mailbox_init(&stream->mailbox,
                            stream->events,
                            sizeof(stream->events[0]),
                            SCHEDULER_TASK_CLASS_EVENT_QUEUE_SIZE,
                            KERNEL_MAILBOX_DROP_OLDEST,
                            scheduler_wait_class_for_task_class(index),
                            0u);
    }
}

static uint32_t scheduler_task_event_mask_for_type(uint32_t event_type) {
    switch (event_type) {
    case MK_TASK_EVENT_LAUNCHED:
        return MK_TASK_EVENT_MASK_LAUNCHED;
    case MK_TASK_EVENT_TERMINATED:
        return MK_TASK_EVENT_MASK_TERMINATED;
    case MK_TASK_EVENT_BLOCKED:
        return MK_TASK_EVENT_MASK_BLOCKED;
    case MK_TASK_EVENT_WOKE:
        return MK_TASK_EVENT_MASK_WOKE;
    case MK_TASK_EVENT_RESTART_REQUESTED:
        return MK_TASK_EVENT_MASK_RESTART_REQUESTED;
    default:
        return 0u;
    }
}

static uint32_t scheduler_task_class_mask_for_task(const process_t *task) {
    if (task == NULL || task->task_class == MK_TASK_CLASS_NONE) {
        return 0u;
    }
    return MK_TASK_CLASS_MASK(task->task_class);
}

static uint32_t scheduler_wait_class_for_task_class(uint32_t task_class) {
    switch (task_class) {
    case MK_TASK_CLASS_INPUT:
        return TASK_WAIT_CLASS_INPUT;
    case MK_TASK_CLASS_VIDEO_PRESENT:
    case MK_TASK_CLASS_VIDEO_CONTROL:
        return TASK_WAIT_CLASS_VIDEO;
    case MK_TASK_CLASS_STORAGE_IO:
        return TASK_WAIT_CLASS_STORAGE;
    case MK_TASK_CLASS_FILESYSTEM_IO:
        return TASK_WAIT_CLASS_FILESYSTEM;
    case MK_TASK_CLASS_AUDIO_IO:
        return TASK_WAIT_CLASS_AUDIO;
    case MK_TASK_CLASS_NETWORK_IO:
        return TASK_WAIT_CLASS_NETWORK;
    case MK_TASK_CLASS_SUPERVISION:
    case MK_TASK_CLASS_DESKTOP:
    case MK_TASK_CLASS_SHELL:
    case MK_TASK_CLASS_APP_RUNTIME:
    case MK_TASK_CLASS_CONSOLE_IO:
    default:
        return TASK_WAIT_CLASS_SUPERVISION;
    }
}

static struct scheduler_task_class_stream *
scheduler_task_class_stream_for_class(uint32_t task_class) {
    if (task_class == MK_TASK_CLASS_NONE || task_class >= SCHEDULER_TASK_CLASS_STREAM_COUNT) {
        return NULL;
    }

    return &g_task_class_streams[task_class];
}

static uint32_t scheduler_task_class_pending_events(uint32_t task_class) {
    struct scheduler_task_class_stream *stream = scheduler_task_class_stream_for_class(task_class);

    if (stream == NULL) {
        return 0u;
    }
    return kernel_mailbox_count(&stream->mailbox);
}

static uint32_t scheduler_task_class_dropped_events(uint32_t task_class) {
    struct scheduler_task_class_stream *stream = scheduler_task_class_stream_for_class(task_class);

    if (stream == NULL) {
        return 0u;
    }
    return kernel_mailbox_dropped(&stream->mailbox);
}

static struct scheduler_task_event_subscription *
scheduler_find_task_event_subscription_locked(process_t *subscriber) {
    uint32_t index;

    if (subscriber == NULL || subscriber->pid <= 0) {
        return NULL;
    }

    for (index = 0u; index < SCHEDULER_TASK_EVENT_SUBSCRIBERS; ++index) {
        struct scheduler_task_event_subscription *subscription =
            &g_task_event_subscriptions[index];

        if (subscription->pid == (uint32_t)subscriber->pid) {
            return subscription;
        }
    }
    return NULL;
}

static struct scheduler_task_event_subscription *
scheduler_alloc_task_event_subscription_locked(process_t *subscriber) {
    uint32_t index;
    struct scheduler_task_event_subscription *empty = NULL;

    if (subscriber == NULL || subscriber->pid <= 0) {
        return NULL;
    }

    for (index = 0u; index < SCHEDULER_TASK_EVENT_SUBSCRIBERS; ++index) {
        struct scheduler_task_event_subscription *subscription =
            &g_task_event_subscriptions[index];

        if (subscription->pid == (uint32_t)subscriber->pid) {
            return subscription;
        }
        if (empty == NULL &&
            (subscription->pid == 0u ||
             scheduler_find_task_by_pid((int)subscription->pid) == NULL)) {
            empty = subscription;
        }
    }

    if (empty != NULL) {
        memset(empty, 0, sizeof(*empty));
        empty->pid = (uint32_t)subscriber->pid;
        empty->event_mask = MK_TASK_EVENT_MASK_LIFECYCLE;
        empty->task_class_mask = MK_TASK_CLASS_MASK_ALL;
        kernel_mailbox_init(&empty->mailbox,
                            empty->events,
                            sizeof(empty->events[0]),
                            SCHEDULER_TASK_EVENT_QUEUE_SIZE,
                            KERNEL_MAILBOX_DROP_OLDEST,
                            TASK_WAIT_CLASS_SUPERVISION,
                            0u);
    }
    return empty;
}

static void scheduler_publish_task_event(uint32_t event_type, const process_t *task) {
    uint32_t index;
    uint32_t flags;
    uint32_t event_mask;
    uint32_t task_class_mask;
    const struct mk_launch_context *context;
    process_t *mutable_task;
    struct scheduler_task_class_stream *class_stream;
    struct mk_task_event event_buf;
    kernel_mailbox_t *target_mailboxes[SCHEDULER_TASK_EVENT_SUBSCRIBERS];
    uint32_t target_mailbox_count = 0u;

    if (task == NULL || task->pid <= 0 || event_type == MK_TASK_EVENT_NONE) {
        return;
    }
    if (!g_scheduler_task_events_ready) {
        return;
    }

    event_mask = scheduler_task_event_mask_for_type(event_type);
    if (event_mask == 0u) {
        return;
    }
    task_class_mask = scheduler_task_class_mask_for_task(task);
    if (task_class_mask == 0u) {
        task_class_mask = MK_TASK_CLASS_MASK_ALL;
    }

    context = mk_launch_context_for_pid(task->pid);
    mutable_task = (process_t *)task;
    flags = spinlock_lock_irqsave(&g_task_event_lock);

    memset(&event_buf, 0, sizeof(event_buf));
    event_buf.abi_version = 1u;
    event_buf.event_type = event_type;
    event_buf.pid = (uint32_t)task->pid;
    event_buf.kind = (uint32_t)task->kind;
    event_buf.service_type = task->service_type;
    event_buf.task_class = task->task_class;
    event_buf.priority_tier = task->priority_tier;
    event_buf.flags = context != 0 ? context->flags : 0u;
    event_buf.sequence = ++g_task_event_sequence;
    event_buf.tick = kernel_timer_get_ticks();

    class_stream = scheduler_task_class_stream_for_class(task->task_class);

    mutable_task->last_task_event_sequence = event_buf.sequence;
    mutable_task->last_task_event_type = event_buf.event_type;
    mutable_task->last_task_event_tick = event_buf.tick;

    for (index = 0u; index < SCHEDULER_TASK_EVENT_SUBSCRIBERS; ++index) {
        struct scheduler_task_event_subscription *subscription =
            &g_task_event_subscriptions[index];

        if (subscription->pid == 0u) {
            continue;
        }
        if ((subscription->event_mask & event_mask) == 0u) {
            continue;
        }
        if ((subscription->task_class_mask & task_class_mask) == 0u) {
            continue;
        }
        if (target_mailbox_count < SCHEDULER_TASK_EVENT_SUBSCRIBERS) {
            target_mailboxes[target_mailbox_count++] = &subscription->mailbox;
        }
    }
    spinlock_unlock_irqrestore(&g_task_event_lock, flags);

    if (class_stream != NULL) {
        (void)kernel_mailbox_try_send(&class_stream->mailbox, &event_buf);
        event_buf.class_pending_depth = kernel_mailbox_count(&class_stream->mailbox);
        event_buf.class_dropped_events = kernel_mailbox_dropped(&class_stream->mailbox);
    }
    for (index = 0u; index < target_mailbox_count; ++index) {
        (void)kernel_mailbox_try_send(target_mailboxes[index], &event_buf);
    }
}

void scheduler_publish_lifecycle_event(uint32_t event_type, const process_t *task) {
    scheduler_publish_task_event(event_type, task);
}

void scheduler_add_task(process_t *proc) {
    uint32_t flags;

    if (proc == NULL) {
        return;
    }
    flags = spinlock_lock_irqsave(&g_scheduler_lock);
    proc->next = NULL;
    proc->state = PROCESS_READY;
    proc->current_cpu = -1;
    proc->last_cpu = -1;
    if (proc->preferred_cpu < 0) {
        proc->preferred_cpu = scheduler_pick_target_cpu();
    }
    if (g_head == NULL) {
        g_head = proc;
        g_cursor[proc->preferred_cpu >= 0 ? (uint32_t)proc->preferred_cpu : 0u] = proc;
    } else {
        process_t *p = g_head;
        while (p->next) {
            p = p->next;
        }
        p->next = proc;
    }
    spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
    if (!g_audio_task_added_trace_emitted &&
        proc->service_type == MK_SERVICE_AUDIO) {
        g_audio_task_added_trace_emitted = 1u;
        kernel_text_puts("audiosvc: task added\n");
    }
    if (proc->service_type == MK_SERVICE_AUDIO) {
        kernel_text_puts("audiosvc: before publish\n");
    }
    scheduler_publish_task_event(MK_TASK_EVENT_LAUNCHED, proc);
    if (proc->service_type == MK_SERVICE_AUDIO) {
        kernel_text_puts("audiosvc: after publish\n");
    }
    smp_wake_sleeping_cpus();
    if (proc->service_type == MK_SERVICE_AUDIO) {
        kernel_text_puts("audiosvc: wake sent\n");
    }
}

static process_t *find_next(uint32_t cpu_index, process_t *current) {
    process_t *start;
    process_t *task;
    process_t *best = NULL;
    int best_score = 0x7fffffff;

    if (g_head == NULL) {
        return NULL;
    }

    start = g_cursor[cpu_index];
    if (start == NULL) {
        start = current != NULL && current->next != NULL ? current->next : g_head;
    }
    if (start == NULL) {
        start = scheduler_first_runnable();
    }
    if (start == NULL) {
        return NULL;
    }

    task = start;
    do {
        if (task->state == PROCESS_READY && task->current_cpu < 0) {
            int score = scheduler_task_score(task, cpu_index);

            if (best == NULL || score < best_score) {
                best = task;
                best_score = score;
                if (score == 0) {
                    break;
                }
            }
        }

        task = task->next != NULL ? task->next : g_head;
    } while (task != NULL && task != start);

    return best;
}

void scheduler_set_preemption_ready(int ready) {
    g_scheduler_preemption_ready = ready != 0 ? 1 : 0;
}

void scheduler_set_task_events_ready(int ready) {
    g_scheduler_task_events_ready = ready != 0 ? 1 : 0;
}

kernel_trap_frame_t *scheduler_schedule_frame(kernel_trap_frame_t *frame, int preemptive) {
    uint32_t cpu_index = kernel_cpu_index();
    uint32_t now_ticks = kernel_timer_get_ticks();
    process_t *current;
    process_t *next;
    process_t *old_task;
    kernel_trap_frame_t *resume_frame;
    uint32_t flags = spinlock_lock_irqsave(&g_scheduler_lock);

    (void)preemptive;

    if (cpu_index >= 32u) {
        cpu_index = 0u;
    }
    current = g_current[cpu_index];
    if (!g_scheduler_preemption_ready) {
        spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
        return frame;
    }
    if (preemptive &&
        current != NULL &&
        current->state == PROCESS_RUNNING &&
        current->current_cpu == (int)cpu_index) {
        current->context = frame;
        if (g_timeslice_remaining[cpu_index] > 1u) {
            g_timeslice_remaining[cpu_index] -= 1u;
            spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
            return frame;
        }
    }
    if (current && current->state == PROCESS_RUNNING && current->current_cpu == (int)cpu_index) {
        current->context = frame;
        scheduler_account_runtime(current, now_ticks);
        if (current->wake_boost_budget != 0u) {
            current->wake_boost_budget -= 1u;
        }
        if (current->wait_result != TASK_WAIT_RESULT_NONE &&
            !scheduler_keep_wait_boost(current)) {
            /*
             * Wakeup priority should buy the task its first resumed slice, not
             * permanently bias every later reschedule after the wait was
             * already consumed in userland/kernel code.
             */
            current->wait_result = TASK_WAIT_RESULT_NONE;
            current->wait_event_kind = TASK_WAIT_EVENT_NONE;
            current->wait_event_class = TASK_WAIT_CLASS_NONE;
            current->wait_owner_service = 0u;
            current->wake_boost_budget = 0u;
        }
        current->state = PROCESS_READY;
        current->current_cpu = -1;
        current->last_cpu = (int)cpu_index;
    } else if (current && current->state == PROCESS_BLOCKED &&
               current->current_cpu == (int)cpu_index) {
        current->context = frame;
        scheduler_account_runtime(current, now_ticks);
        current->current_cpu = -1;
        current->last_cpu = (int)cpu_index;
    }
    next = find_next(cpu_index, current);
    if (next == NULL) {
        if (current && current->state == PROCESS_READY) {
            current->state = PROCESS_RUNNING;
            current->current_cpu = (int)cpu_index;
            current->last_cpu = (int)cpu_index;
            current->last_start_tick = now_ticks;
            g_timeslice_remaining[cpu_index] = SCHEDULER_TIMESLICE_TICKS;
            g_current[cpu_index] = current;
            resume_frame = current->context != NULL ? current->context : frame;
            spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
            return scheduler_sanitize_resume_frame(cpu_index, current, current, resume_frame);
        }

        g_current[cpu_index] = 0;
        g_timeslice_remaining[cpu_index] = SCHEDULER_TIMESLICE_TICKS;
        spinlock_unlock_irqrestore(&g_scheduler_lock, flags);

        for (;;) {
            __asm__ volatile("sti; hlt; cli" : : : "memory");
            now_ticks = kernel_timer_get_ticks();
            flags = spinlock_lock_irqsave(&g_scheduler_lock);
            next = find_next(cpu_index, 0);
            if (next != NULL) {
                break;
            }
            spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
        }
    }

    g_cursor[cpu_index] = next->next != NULL ? next->next : g_head;
    old_task = current;

    if (next != current) {
        next->state = PROCESS_RUNNING;
        next->current_cpu = (int)cpu_index;
        next->last_cpu = (int)cpu_index;
        next->last_start_tick = now_ticks;
        next->context_switches += 1u;
        g_timeslice_remaining[cpu_index] = SCHEDULER_TIMESLICE_TICKS;
        g_current[cpu_index] = next;
        resume_frame = next->context;
        if (resume_frame == NULL) {
            kernel_debug_printf("scheduler: select missing context pid=%d next_context=NULL\n",
                                next->pid);
        } else if (next->runs_in_user_mode &&
                   !process_frame_is_user_mode(resume_frame)) {
            kernel_debug_printf("scheduler: select bad user context pid=%d context=%x cs=%x\n",
                                next->pid,
                                (unsigned int)(uintptr_t)resume_frame,
                                (unsigned int)resume_frame->cs);
        }
        spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
        if (!g_audio_first_dispatch_trace_emitted &&
            next->service_type == MK_SERVICE_AUDIO) {
            g_audio_first_dispatch_trace_emitted = 1u;
            kernel_text_puts("audiosvc: first dispatch\n");
        }
        if (old_task == NULL && !g_scheduler_boot_trace_emitted) {
            g_scheduler_boot_trace_emitted = 1;
            kernel_debug_printf("scheduler: first dispatch cpu=%d pid=%d kind=%d service=%d eip=%x esp=%x\n",
                                (int)cpu_index,
                                next->pid,
                                (int)next->kind,
                                (int)next->service_type,
                                (unsigned int)process_saved_eip(next),
                                (unsigned int)process_saved_esp(next));
        }
        scheduler_trace_switch(cpu_index, old_task, next);
        return scheduler_sanitize_resume_frame(cpu_index,
                                               old_task,
                                               next,
                                               resume_frame != NULL ? resume_frame : frame);
    } else {
        current->state = PROCESS_RUNNING;
        current->current_cpu = (int)cpu_index;
        current->last_cpu = (int)cpu_index;
        current->last_start_tick = now_ticks;
        current->context = frame;
        g_timeslice_remaining[cpu_index] = SCHEDULER_TIMESLICE_TICKS;
        resume_frame = current->context;
        spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
        return scheduler_sanitize_resume_frame(cpu_index,
                                               current,
                                               current,
                                               resume_frame != NULL ? resume_frame : frame);
    }
}

void schedule(void) {
    yield();
}

void yield(void) {
    __asm__ volatile("int $0x81" : : : "memory");
}

process_t *scheduler_current(void) {
    return scheduler_current_for_cpu(kernel_cpu_index());
}

process_t *scheduler_current_for_cpu(uint32_t cpu_index) {
    process_t *current;
    uint32_t flags;

    if (cpu_index >= 32u) {
        cpu_index = 0u;
    }
    flags = spinlock_lock_irqsave(&g_scheduler_lock);
    current = g_current[cpu_index];
    spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
    return current;
}

uint32_t scheduler_current_pid_for_cpu(uint32_t cpu_index) {
    process_t *current;
    uint32_t flags;

    if (cpu_index >= 32u) {
        cpu_index = 0u;
    }
    flags = spinlock_lock_irqsave(&g_scheduler_lock);
    current = g_current[cpu_index];
    spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
    return current != NULL ? (uint32_t)current->pid : 0u;
}

uint32_t scheduler_current_pid(void) {
    return scheduler_current_pid_for_cpu(kernel_cpu_index());
}

process_t *scheduler_find_task_by_pid(int pid) {
    process_t *task;
    uint32_t flags;

    if (pid <= 0) {
        return NULL;
    }

    flags = spinlock_lock_irqsave(&g_scheduler_lock);
    for (task = g_head; task != NULL; task = task->next) {
        if (task->pid == pid && task->state != PROCESS_TERMINATED) {
            spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
            return task;
        }
    }
    spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
    return NULL;
}

void scheduler_terminate_task(process_t *task) {
    uint32_t flags;
    uint32_t cpu;
    uint32_t now_ticks;

    if (task == NULL) {
        return;
    }

    now_ticks = kernel_timer_get_ticks();
    flags = spinlock_lock_irqsave(&g_scheduler_lock);
    scheduler_account_runtime(task, now_ticks);
    task->state = PROCESS_TERMINATED;
    task->current_cpu = -1;
    task->preferred_cpu = -1;
    task->last_cpu = -1;
    task->wait_channel = 0;
    task->wait_deadline = 0u;
    task->wait_result = TASK_WAIT_RESULT_NONE;
    task->wait_event_kind = TASK_WAIT_EVENT_NONE;
    task->wait_event_class = TASK_WAIT_CLASS_NONE;
    task->wait_owner_service = 0u;
    task->wake_boost_budget = 0u;
    task->wait_next = 0;

    for (cpu = 0u; cpu < 32u; ++cpu) {
        if (g_current[cpu] == task) {
            g_current[cpu] = NULL;
        }
        if (g_cursor[cpu] == task) {
            g_cursor[cpu] = task->next != NULL ? task->next : g_head;
            if (g_cursor[cpu] == task) {
                g_cursor[cpu] = NULL;
            }
        }
    }

    spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
    scheduler_publish_task_event(MK_TASK_EVENT_TERMINATED, task);
    mk_launch_release_pid(task->pid);
}

int scheduler_block_current_ex(const void *wait_channel,
                               uint32_t wait_deadline,
                               uint32_t wait_event_kind,
                               uint32_t wait_event_class,
                               uint32_t wait_owner_service) {
    process_t *current;
    uint32_t cpu_index = kernel_cpu_index();
    uint32_t flags;

    if (cpu_index >= 32u) {
        cpu_index = 0u;
    }

    flags = spinlock_lock_irqsave(&g_scheduler_lock);
    current = g_current[cpu_index];
    if (current == NULL || current->state != PROCESS_RUNNING) {
        spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
        return -1;
    }

    current->state = PROCESS_BLOCKED;
    current->wait_channel = wait_channel;
    current->wait_deadline = wait_deadline;
    current->wait_result = TASK_WAIT_RESULT_NONE;
    current->wait_event_kind = wait_event_kind;
    current->wait_event_class = wait_event_class;
    current->wait_owner_service = wait_owner_service;
    current->wake_boost_budget = 0u;
    spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
    if (g_scheduler_block_trace_budget > 0) {
        g_scheduler_block_trace_budget -= 1;
        kernel_debug_printf("scheduler: block pid=%d kind=%d svc=%d class=%d event=%d owner=%d deadline=%d\n",
                            current->pid,
                            (int)current->kind,
                            (int)current->service_type,
                            (int)wait_event_class,
                            (int)wait_event_kind,
                            (int)wait_owner_service,
                            (int)wait_deadline);
    }
    scheduler_publish_task_event(MK_TASK_EVENT_BLOCKED, current);
    return 0;
}

int scheduler_block_current(const void *wait_channel) {
    return scheduler_block_current_ex(wait_channel,
                                      0u,
                                      TASK_WAIT_EVENT_WAITABLE,
                                      TASK_WAIT_CLASS_GENERIC,
                                      0u);
}

int scheduler_wake_task(process_t *task) {
    return scheduler_complete_wait(task, TASK_WAIT_RESULT_SIGNALED);
}

int scheduler_complete_wait(process_t *task, uint32_t wait_result) {
    uint32_t flags;

    if (task == NULL) {
        return -1;
    }

    flags = spinlock_lock_irqsave(&g_scheduler_lock);
    if (task->state == PROCESS_BLOCKED) {
        int inline_cpu = task->current_cpu;
        int inline_wake = 0;

        if (inline_cpu >= 0 && inline_cpu < 32 && g_current[(uint32_t)inline_cpu] == task) {
            /*
             * A wait can complete before the task actually yields. In that
             * case the task is still executing on its current CPU, so leaving
             * it READY would cause the next trap to skip saving the live frame
             * and later resume stale stack state.
             */
            inline_wake = 1;
        }
        task->state = inline_wake ? PROCESS_RUNNING : PROCESS_READY;
        if (!inline_wake) {
            task->current_cpu = -1;
        }
        scheduler_clear_wait_channel(task);
        task->wait_result = wait_result;
        task->wake_boost_budget =
            (wait_result == TASK_WAIT_RESULT_SIGNALED &&
             task->wait_event_class == TASK_WAIT_CLASS_IPC)
                ? (task->kind == PROCESS_KIND_SERVICE ? 12u : 8u)
                : 0u;
        spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
        scheduler_publish_task_event(MK_TASK_EVENT_WOKE, task);
        return 0;
    }
    spinlock_unlock_irqrestore(&g_scheduler_lock, flags);
    return -1;
}

int scheduler_task_event_subscribe(process_t *subscriber,
                                   uint32_t event_mask,
                                   uint32_t task_class_mask) {
    uint32_t flags;
    struct scheduler_task_event_subscription *subscription;
    int rc;

    scheduler_set_task_events_ready(1);
    flags = spinlock_lock_irqsave(&g_task_event_lock);
    subscription = scheduler_alloc_task_event_subscription_locked(subscriber);
    if (subscription != NULL) {
        subscription->event_mask = event_mask != 0u ? event_mask : MK_TASK_EVENT_MASK_LIFECYCLE;
        subscription->task_class_mask = task_class_mask != 0u ? task_class_mask : MK_TASK_CLASS_MASK_ALL;
        rc = 0;
    } else {
        rc = -1;
    }
    spinlock_unlock_irqrestore(&g_task_event_lock, flags);
    return rc;
}

int scheduler_task_event_receive(process_t *subscriber,
                                 struct mk_task_event *event,
                                 uint32_t timeout_ticks) {
    int wait_rc;

    if (subscriber == NULL || event == NULL) {
        return -1;
    }

    for (;;) {
        struct scheduler_task_event_subscription *subscription;
        uint32_t flags;

        flags = spinlock_lock_irqsave(&g_task_event_lock);
        subscription = scheduler_find_task_event_subscription_locked(subscriber);
        if (subscription == NULL) {
            spinlock_unlock_irqrestore(&g_task_event_lock, flags);
            return -1;
        }
        if (kernel_mailbox_try_receive(&subscription->mailbox, event) == 0) {
            spinlock_unlock_irqrestore(&g_task_event_lock, flags);
            return 0;
        }
        spinlock_unlock_irqrestore(&g_task_event_lock, flags);
        if (timeout_ticks == 0u) {
            return -1;
        }
        if (timeout_ticks == MK_TASK_EVENT_WAIT_FOREVER) {
            wait_rc = kernel_mailbox_wait(&subscription->mailbox, 0u);
        } else {
            wait_rc = kernel_mailbox_wait(&subscription->mailbox, timeout_ticks);
        }
        if (wait_rc != TASK_WAIT_RESULT_SIGNALED) {
            return -1;
        }
    }
}

static void scheduler_timeout_tick_hook(uint32_t tick) {
    process_t *task;
    process_t *woke_tasks[16];
    uint8_t woke_inline[16];
    uint32_t woke_count = 0u;
    uint32_t wake_sleepers = 0u;
    uint32_t flags;

    flags = spinlock_lock_irqsave(&g_scheduler_lock);
    for (task = g_head; task != NULL; task = task->next) {
        int inline_cpu;
        int inline_wake;

        if (task->state != PROCESS_BLOCKED || task->wait_channel == 0 || task->wait_deadline == 0u) {
            continue;
        }
        if (tick < task->wait_deadline) {
            continue;
        }

        inline_cpu = task->current_cpu;
        inline_wake = 0;
        if (inline_cpu >= 0 && inline_cpu < 32 &&
            g_current[(uint32_t)inline_cpu] == task) {
            /*
             * A timed wait can expire before the blocked task actually yields.
             * Keep that task RUNNING so the live frame in-flight returns to the
             * caller instead of being treated as a stale READY context.
             */
            inline_wake = 1;
        }
        kernel_waitable_detach_task((kernel_waitable_t *)task->wait_channel, task);
        task->state = inline_wake ? PROCESS_RUNNING : PROCESS_READY;
        if (!inline_wake) {
            task->current_cpu = -1;
            wake_sleepers = 1u;
        }
        scheduler_clear_wait_channel(task);
        task->wait_result = TASK_WAIT_RESULT_TIMED_OUT;
        if (woke_count < (sizeof(woke_tasks) / sizeof(woke_tasks[0]))) {
            woke_tasks[woke_count++] = task;
            woke_inline[woke_count - 1u] = (uint8_t)inline_wake;
        }
    }
    spinlock_unlock_irqrestore(&g_scheduler_lock, flags);

    if (wake_sleepers != 0u) {
        smp_wake_sleeping_cpus();
    }

    for (uint32_t i = 0u; i < woke_count; ++i) {
        if (g_scheduler_timeout_trace_budget > 0) {
            g_scheduler_timeout_trace_budget -= 1;
            kernel_debug_printf("scheduler: timeout pid=%d class=%d event=%d owner=%d inline=%d\n",
                                woke_tasks[i]->pid,
                                (int)woke_tasks[i]->wait_event_class,
                                (int)woke_tasks[i]->wait_event_kind,
                                (int)woke_tasks[i]->wait_owner_service,
                                (int)woke_inline[i]);
        }
        scheduler_publish_task_event(MK_TASK_EVENT_WOKE, woke_tasks[i]);
    }
}

uint32_t scheduler_snapshot(struct task_snapshot_entry *entries,
                            uint32_t max_entries,
                            struct task_snapshot_summary *summary) {
    process_t *task;
    uint32_t count = 0u;
    uint32_t flags;
    uint32_t now_ticks = kernel_timer_get_ticks();

    if (summary != NULL) {
        uint32_t snapshot_cpu = kernel_cpu_index();

        memset(summary, 0, sizeof(*summary));
        summary->abi_version = TASK_SNAPSHOT_ABI_VERSION;
        summary->uptime_ticks = now_ticks;
        summary->cpu_count = kernel_cpu_count();
        summary->current_pid = scheduler_current_pid_for_cpu(snapshot_cpu);
        for (uint32_t cpu = 0u; cpu < summary->cpu_count; ++cpu) {
            const struct kernel_cpu_state *state = kernel_cpu_state(cpu);

            if (state != NULL && state->started != 0u) {
                summary->started_cpu_count += 1u;
            }
        }
    }

    flags = spinlock_lock_irqsave(&g_scheduler_lock);
    for (task = g_head; task != NULL; task = task->next) {
        uint32_t runtime_ticks = task->runtime_ticks;

        if (task->state == PROCESS_TERMINATED) {
            continue;
        }

        if (task->state == PROCESS_RUNNING && task->last_start_tick != 0u && now_ticks >= task->last_start_tick) {
            runtime_ticks += (now_ticks - task->last_start_tick);
        }

        if (summary != NULL) {
            summary->total_tasks += 1u;
            if (task->state == PROCESS_RUNNING) {
                summary->running_tasks += 1u;
            } else if (task->state == PROCESS_READY) {
                summary->ready_tasks += 1u;
            } else if (task->state == PROCESS_BLOCKED) {
                summary->blocked_tasks += 1u;
            }
            if (task->wait_result == TASK_WAIT_RESULT_TIMED_OUT) {
                summary->timed_out_waits += 1u;
            } else if (task->wait_result == TASK_WAIT_RESULT_CANCELED) {
                summary->canceled_waits += 1u;
            }
            if (task->wait_channel != 0) {
                const kernel_waitable_t *waitable = (const kernel_waitable_t *)task->wait_channel;

                summary->pending_event_signals += waitable->pending_signals;
            }
        }

        if (entries != NULL && count < max_entries) {
            struct task_snapshot_entry *entry = &entries[count];

            memset(entry, 0, sizeof(*entry));
            entry->pid = (uint32_t)task->pid;
            entry->kind = (uint32_t)task->kind;
            entry->state = (uint32_t)task->state;
            entry->current_cpu = task->current_cpu;
            entry->preferred_cpu = task->preferred_cpu;
            entry->last_cpu = task->last_cpu;
            entry->stack_size = task->stack_size;
            entry->runtime_ticks = runtime_ticks;
            entry->context_switches = task->context_switches;
            entry->service_type = task->service_type;
            entry->priority_tier = task->priority_tier;
            entry->task_class = task->task_class;
            entry->wait_result = task->wait_result;
            entry->wait_event_kind = task->wait_event_kind;
            entry->wait_event_class = task->wait_event_class;
            entry->wait_owner_service = task->wait_owner_service;
            entry->wait_deadline = task->wait_deadline;
            entry->wait_pending_signals =
                task->wait_channel != 0 ? ((const kernel_waitable_t *)task->wait_channel)->pending_signals : 0u;
            entry->last_task_event_sequence = task->last_task_event_sequence;
            entry->last_task_event_type = task->last_task_event_type;
            entry->last_task_event_tick = task->last_task_event_tick;
            entry->task_class_pending_events = scheduler_task_class_pending_events(task->task_class);
            entry->task_class_dropped_events = scheduler_task_class_dropped_events(task->task_class);
            count += 1u;
        }
    }
    spinlock_unlock_irqrestore(&g_scheduler_lock, flags);

    if (summary != NULL) {
        uint32_t event_flags;

        event_flags = spinlock_lock_irqsave(&g_task_event_lock);
        summary->latest_task_event_sequence = g_task_event_sequence;
        spinlock_unlock_irqrestore(&g_task_event_lock, event_flags);

        for (uint32_t task_class = 1u; task_class < SCHEDULER_TASK_CLASS_STREAM_COUNT; ++task_class) {
            summary->task_class_pending_events += scheduler_task_class_pending_events(task_class);
            summary->task_class_dropped_events += scheduler_task_class_dropped_events(task_class);
        }
    }

    return count;
}
