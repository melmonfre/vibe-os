#include <userland/applications/games/craft/upstream/src/tinycthread.h>

#define CRAFT_WORKER_IDLE 0
#define CRAFT_WORKER_BUSY 1
#define CRAFT_WORKER_DONE 2

typedef struct {
    int p;
    int q;
    int load;
    void *block_maps[3][3];
    void *light_maps[3][3];
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

int thrd_create(thrd_t *thr, thrd_start_t func, void *arg) {
    (void)func;
    if (thr) {
        thr->active = 1;
    }
    if (arg && g_worker_count < (int)(sizeof(g_workers) / sizeof(g_workers[0]))) {
        g_workers[g_worker_count++] = (craft_worker *)arg;
    }
    return thrd_success;
}

int thrd_join(thrd_t thr, int *res) {
    (void)thr;
    if (res) {
        *res = 0;
    }
    return thrd_success;
}

int mtx_init(mtx_t *mtx, int type) {
    (void)type;
    if (mtx) {
        mtx->locked = 0;
    }
    return thrd_success;
}

void mtx_destroy(mtx_t *mtx) {
    (void)mtx;
}

int mtx_lock(mtx_t *mtx) {
    if (mtx) {
        mtx->locked = 1;
    }
    return thrd_success;
}

int mtx_unlock(mtx_t *mtx) {
    if (mtx) {
        mtx->locked = 0;
    }
    return thrd_success;
}

int cnd_init(cnd_t *cond) {
    if (cond) {
        cond->signaled = 0;
        cond->owner = 0;
    }
    return thrd_success;
}

void cnd_destroy(cnd_t *cond) {
    (void)cond;
}

int cnd_signal(cnd_t *cond) {
    if (cond) {
        cond->signaled = 1;
    }
    return thrd_success;
}

int cnd_wait(cnd_t *cond, mtx_t *mtx) {
    (void)mtx;
    if (cond) {
        return cond->signaled ? thrd_success : thrd_busy;
    }
    return thrd_success;
}

void craft_thread_pump(int max_jobs) {
    int jobs = 0;

    for (int i = 0; i < g_worker_count; ++i) {
        craft_worker *worker = g_workers[i];
        craft_worker_item *item;

        if (!worker || worker->state != CRAFT_WORKER_BUSY || jobs >= max_jobs) {
            continue;
        }
        if (!worker->cnd.signaled) {
            continue;
        }

        item = &worker->item;
        if (item->load) {
            load_chunk(item);
        }
        compute_chunk(item);
        worker->state = CRAFT_WORKER_DONE;
        worker->cnd.signaled = 0;
        jobs += 1;
    }
}

void craft_thread_reset(void) {
    for (int i = 0; i < g_worker_count; ++i) {
        if (g_workers[i]) {
            g_workers[i]->state = CRAFT_WORKER_IDLE;
            g_workers[i]->cnd.signaled = 0;
        }
    }
}
