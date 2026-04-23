#include <stdlib.h>
#include <string.h>
#include <userland/modules/include/syscalls.h>
#include <userland/applications/games/craft/upstream/src/map.h>
#include <userland/applications/games/craft/upstream/src/tinycthread.h>

#define CRAFT_WORKER_IDLE 0
#define CRAFT_WORKER_BUSY 1
#define CRAFT_WORKER_DONE 2
#define CRAFT_ENABLE_NATIVE_WORKERS 1
#define CRAFT_WORKER_MAX 4
#define CRAFT_THREAD_STACK_SIZE 65536u

typedef struct {
    int p;
    int q;
    int load;
    Map *block_maps[3][3];
    Map *light_maps[3][3];
    int miny;
    int maxy;
    int faces;
    float *data;
} craft_worker_item;

typedef struct {
    int index;
    int state;
    thrd_t thrd;
    mtx_t mtx;
    cnd_t cnd;
    craft_worker_item item;
} craft_worker;

extern void load_chunk(craft_worker_item *item);
extern void compute_chunk(craft_worker_item *item);

static craft_worker *g_workers[8];
static int g_worker_count = 0;
static int g_worker_rr_cursor = 0;
static int g_parallel_workers = -1;

static void craft_thread_yield_cpu(void) {
    sys_yield();
}

static int craft_thread_worker_registered(craft_worker *worker) {
    if (worker == NULL) {
        return 0;
    }
    for (int i = 0; i < g_worker_count; ++i) {
        if (g_workers[i] == worker) {
            return 1;
        }
    }
    return 0;
}

static void craft_thread_register_worker_arg(void *arg) {
    craft_worker *worker = (craft_worker *)arg;

    if (worker == NULL || craft_thread_worker_registered(worker)) {
        return;
    }
    if (g_worker_count < (int)(sizeof(g_workers) / sizeof(g_workers[0]))) {
        g_workers[g_worker_count++] = worker;
    }
}

static int craft_thread_has_native_task(const thrd_t *thr) {
    return thr != NULL && thr->state != NULL && thr->state->pid > 0;
}

static void craft_thread_cleanup_item(craft_worker_item *item) {
    if (item == NULL) {
        return;
    }
    for (int a = 0; a < 3; ++a) {
        for (int b = 0; b < 3; ++b) {
            if (item->block_maps[a][b] != NULL) {
                map_free(item->block_maps[a][b]);
                free(item->block_maps[a][b]);
                item->block_maps[a][b] = NULL;
            }
            if (item->light_maps[a][b] != NULL) {
                map_free(item->light_maps[a][b]);
                free(item->light_maps[a][b]);
                item->light_maps[a][b] = NULL;
            }
        }
    }
    if (item->data != NULL) {
        free(item->data);
        item->data = NULL;
    }
    item->faces = 0;
    item->load = 0;
}

static void craft_thread_run_fallback_worker(craft_worker *worker) {
    craft_worker_item *item;

    if (worker == NULL) {
        return;
    }
    item = &worker->item;
    if (item->load) {
        load_chunk(item);
    }
    compute_chunk(item);
    worker->state = CRAFT_WORKER_DONE;
}

#if CRAFT_ENABLE_NATIVE_WORKERS
static void craft_thread_entry(void *arg) {
    thrd_state_t *state = (thrd_state_t *)arg;
    int result = 0;

    if (state == NULL) {
        return;
    }
    if (state->func != NULL) {
        result = state->func(state->arg);
    }
    __sync_synchronize();
    state->result = result;
    state->active = 0;
    state->finished = 1;
}
#endif

static int craft_detect_parallel_workers(void) {
#if !CRAFT_ENABLE_NATIVE_WORKERS
    g_parallel_workers = 1;
    return g_parallel_workers;
#else
    struct task_snapshot_summary summary;
    uint32_t cpu_count;
    int workers;

    if (g_parallel_workers > 0) {
        return g_parallel_workers;
    }

    memset(&summary, 0, sizeof(summary));
    if (sys_task_snapshot(&summary, NULL, 0u) < 0) {
        g_parallel_workers = 1;
        return g_parallel_workers;
    }

    cpu_count = summary.started_cpu_count != 0u ? summary.started_cpu_count : summary.cpu_count;
    if (cpu_count > 1u) {
        workers = (int)(cpu_count - 1u);
    } else {
        workers = 1;
    }
    if (workers < 1) {
        workers = 1;
    }
    if (workers > CRAFT_WORKER_MAX) {
        workers = CRAFT_WORKER_MAX;
    }
    g_parallel_workers = workers;
    return g_parallel_workers;
#endif
}

int craft_thread_parallel_workers(void) {
    return craft_detect_parallel_workers();
}

int thrd_create(thrd_t *thr, thrd_start_t func, void *arg) {
    if (thr == NULL) {
        return thrd_error;
    }

    craft_thread_register_worker_arg(arg);
    thr->state = NULL;

#if !CRAFT_ENABLE_NATIVE_WORKERS
    (void)func;
    return thrd_success;
#else
    int pid = -1;
    thrd_state_t *state = (thrd_state_t *)malloc(sizeof(*state));
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
        state->active = 1;
        state->finished = 0;
        state->result = 0;
        state->func = func;
        state->arg = arg;
        pid = sys_task_create((uintptr_t)craft_thread_entry,
                              state,
                              CRAFT_THREAD_STACK_SIZE,
                              0u);
        if (pid > 0) {
            state->pid = pid;
            thr->state = state;
            return thrd_success;
        }
        free(state);
        return thrd_error;
    }
    return thrd_nomem;
#endif
}

int thrd_join(thrd_t thr, int *res) {
    thrd_state_t *state = thr.state;

    if (state != NULL) {
        while (!state->finished) {
            craft_thread_yield_cpu();
        }
        if (res) {
            *res = state->result;
        }
        free(state);
        return thrd_success;
    }
    if (res) {
        *res = 0;
    }
    return thrd_success;
}

int mtx_init(mtx_t *mtx, int type) {
    if (mtx) {
        mtx->locked = 0;
        mtx->type = type;
    }
    return thrd_success;
}

void mtx_destroy(mtx_t *mtx) {
    (void)mtx;
}

int mtx_lock(mtx_t *mtx) {
    if (!mtx) {
        return thrd_error;
    }
    while (__sync_lock_test_and_set(&mtx->locked, 1) != 0) {
        craft_thread_yield_cpu();
    }
    return thrd_success;
}

int mtx_unlock(mtx_t *mtx) {
    if (!mtx) {
        return thrd_error;
    }
    __sync_synchronize();
    __sync_lock_release(&mtx->locked);
    return thrd_success;
}

int cnd_init(cnd_t *cond) {
    if (cond) {
        cond->generation = 0u;
    }
    return thrd_success;
}

void cnd_destroy(cnd_t *cond) {
    (void)cond;
}

int cnd_signal(cnd_t *cond) {
    if (!cond) {
        return thrd_error;
    }
    (void)__sync_add_and_fetch(&cond->generation, 1u);
    return thrd_success;
}

int cnd_wait(cnd_t *cond, mtx_t *mtx) {
    unsigned int generation;

    if (!cond || !mtx) {
        return thrd_error;
    }
    generation = cond->generation;
    mtx_unlock(mtx);
    while (cond->generation == generation) {
        craft_thread_yield_cpu();
    }
    mtx_lock(mtx);
    return thrd_success;
}

void craft_thread_pump(int max_jobs) {
    int jobs = 0;
    int scanned = 0;

    if (max_jobs <= 0) {
        max_jobs = 1;
    }
    while (g_worker_count > 0 && scanned < g_worker_count && jobs < max_jobs) {
        int index = g_worker_rr_cursor % g_worker_count;
        craft_worker *worker = g_workers[index];

        g_worker_rr_cursor = (index + 1) % g_worker_count;
        scanned += 1;
        if (!worker || worker->state != CRAFT_WORKER_BUSY) {
            continue;
        }
        if (craft_thread_has_native_task(&worker->thrd)) {
            continue;
        }

        craft_thread_run_fallback_worker(worker);
        jobs += 1;
    }
}

void craft_thread_reset(void) {
    g_worker_rr_cursor = 0;
    for (int i = 0; i < g_worker_count; ++i) {
        craft_worker *worker = g_workers[i];
        int native_task;

        if (!worker) {
            continue;
        }
        native_task = craft_thread_has_native_task(&worker->thrd);
        for (;;) {
            int state;

            mtx_lock(&worker->mtx);
            state = worker->state;
            if (state != CRAFT_WORKER_BUSY || !native_task) {
                craft_thread_cleanup_item(&worker->item);
                worker->state = CRAFT_WORKER_IDLE;
                mtx_unlock(&worker->mtx);
                break;
            }
            mtx_unlock(&worker->mtx);
            craft_thread_yield_cpu();
        }
    }
}
