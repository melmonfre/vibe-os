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

int thrd_create(thrd_t *thr, thrd_start_t func, void *arg) {
    (void)func;
    (void)arg;
    if (thr) {
        thr->active = 1;
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
        if (cond->owner) {
            craft_worker *worker = (craft_worker *)cond->owner;
            if (worker->state == CRAFT_WORKER_BUSY) {
                craft_worker_item *item = &worker->item;
                if (item->load) {
                    load_chunk(item);
                }
                compute_chunk(item);
                worker->state = CRAFT_WORKER_DONE;
            }
        }
    }
    return thrd_success;
}

int cnd_wait(cnd_t *cond, mtx_t *mtx) {
    (void)mtx;
    if (cond) {
        cond->signaled = 0;
    }
    return thrd_success;
}
