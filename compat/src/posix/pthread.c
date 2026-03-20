#include <pthread.h>
#include <lang/include/vibe_app_runtime.h>
#include <compat/posix/errno.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

struct pthread {
    vibe_app_thread_t thread;
};

struct pthread_attr {
    int detached;
};

struct pthread_mutex {
    vibe_app_mutex_t mutex;
};

struct pthread_mutex_attr {
    int type;
};

struct pthread_cond {
    vibe_app_cond_t cond;
};

struct pthread_cond_attr {
    int unused;
};

static struct pthread g_main_thread;
static int g_main_thread_ready = 0;

static void pthread_ensure_main_thread(void) {
    if (!g_main_thread_ready) {
        memset(&g_main_thread, 0, sizeof(g_main_thread));
        g_main_thread.thread.in_use = 1;
        g_main_thread.thread.started = 1;
        g_main_thread.thread.id = 0;
        g_main_thread_ready = 1;
    }
}

static int pthread_timedwait_ms(const struct timespec *ts) {
    unsigned long long ms;

    if (!ts) {
        return 0;
    }
    if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000l) {
        return -1;
    }
    ms = (unsigned long long)ts->tv_sec * 1000ull;
    ms += (unsigned long long)(ts->tv_nsec / 1000000l);
    if (ms > 0xffffffffull) {
        ms = 0xffffffffull;
    }
    return (int)ms;
}

int pthread_attr_init(pthread_attr_t *attr) {
    if (!attr) {
        return EINVAL;
    }
    *attr = (pthread_attr_t)calloc(1u, sizeof(**attr));
    return *attr ? 0 : EINVAL;
}

int pthread_attr_destroy(pthread_attr_t *attr) {
    if (!attr || !*attr) {
        return EINVAL;
    }
    free(*attr);
    *attr = NULL;
    return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *state) {
    if (!attr || !*attr || !state) {
        return EINVAL;
    }
    *state = (*attr)->detached ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE;
    return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int state) {
    if (!attr || !*attr) {
        return EINVAL;
    }
    (*attr)->detached = (state == PTHREAD_CREATE_DETACHED);
    return 0;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*fn)(void *), void *arg) {
    struct pthread *pt;
    int rc;

    if (!thread || !fn) {
        return EINVAL;
    }
    pt = (struct pthread *)calloc(1u, sizeof(*pt));
    if (!pt) {
        return EINVAL;
    }
    rc = vibe_app_thread_create(&pt->thread, (vibe_app_thread_fn)(intptr_t)fn, arg);
    if (rc != 0) {
        free(pt);
        return EINVAL;
    }
    if (attr && *attr && (*attr)->detached) {
        (void)vibe_app_thread_detach(&pt->thread);
    }
    *thread = pt;
    return 0;
}

int pthread_detach(pthread_t thread) {
    if (!thread) {
        return EINVAL;
    }
    return vibe_app_thread_detach(&thread->thread) == 0 ? 0 : EINVAL;
}

int pthread_join(pthread_t thread, void **value_ptr) {
    int result = 0;

    if (!thread) {
        return EINVAL;
    }
    if (vibe_app_thread_join(&thread->thread, &result) != 0) {
        return EINVAL;
    }
    if (value_ptr) {
        *value_ptr = (void *)(intptr_t)result;
    }
    free(thread);
    return 0;
}

pthread_t pthread_self(void) {
    pthread_ensure_main_thread();
    return &g_main_thread;
}

int pthread_equal(pthread_t a, pthread_t b) {
    return a == b;
}

void pthread_yield(void) {
    (void)vibe_app_thread_yield();
}

void pthread_exit(void *value) {
    (void)value;
    for (;;) {
        (void)vibe_app_thread_yield();
    }
}

int pthread_mutexattr_init(pthread_mutexattr_t *attr) {
    if (!attr) {
        return EINVAL;
    }
    *attr = (pthread_mutexattr_t)calloc(1u, sizeof(**attr));
    return *attr ? 0 : EINVAL;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr) {
    if (!attr || !*attr) {
        return EINVAL;
    }
    free(*attr);
    *attr = NULL;
    return 0;
}

int pthread_mutexattr_gettype(pthread_mutexattr_t *attr, int *type) {
    if (!attr || !*attr || !type) {
        return EINVAL;
    }
    *type = (*attr)->type;
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type) {
    if (!attr || !*attr) {
        return EINVAL;
    }
    (*attr)->type = type;
    return 0;
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    struct pthread_mutex *pm;
    (void)attr;

    if (!mutex) {
        return EINVAL;
    }
    pm = (struct pthread_mutex *)calloc(1u, sizeof(*pm));
    if (!pm) {
        return EINVAL;
    }
    if (vibe_app_mutex_init(&pm->mutex) != 0) {
        free(pm);
        return EINVAL;
    }
    *mutex = pm;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    if (!mutex || !*mutex) {
        return EINVAL;
    }
    free((void *)*mutex);
    *mutex = NULL;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    vibe_app_mutex_t *real_mutex;
    if (!mutex || !*mutex) {
        return EINVAL;
    }
    real_mutex = (vibe_app_mutex_t *)(uintptr_t)&(*mutex)->mutex;
    return vibe_app_mutex_lock(real_mutex) == 0 ? 0 : EINVAL;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    int rc;
    vibe_app_mutex_t *real_mutex;
    if (!mutex || !*mutex) {
        return EINVAL;
    }
    real_mutex = (vibe_app_mutex_t *)(uintptr_t)&(*mutex)->mutex;
    rc = vibe_app_mutex_trylock(real_mutex);
    if (rc == 0) {
        return 0;
    }
    return (rc == 1) ? EBUSY : EINVAL;
}

int pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *ts) {
    int timeout_ms;
    unsigned int start;

    if (!mutex || !*mutex) {
        return EINVAL;
    }
    timeout_ms = pthread_timedwait_ms(ts);
    if (timeout_ms < 0) {
        return EINVAL;
    }
    if (timeout_ms == 0) {
        return pthread_mutex_trylock(mutex);
    }

    start = vibe_app_ticks();
    for (;;) {
        int rc = pthread_mutex_trylock(mutex);
        if (rc == 0) {
            return 0;
        }
        if (rc != EBUSY) {
            return rc;
        }
        if ((unsigned int)(vibe_app_ticks() - start) >= (unsigned int)timeout_ms) {
            return ETIMEDOUT;
        }
        vibe_app_yield();
    }
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    vibe_app_mutex_t *real_mutex;
    if (!mutex || !*mutex) {
        return EINVAL;
    }
    real_mutex = (vibe_app_mutex_t *)(uintptr_t)&(*mutex)->mutex;
    return vibe_app_mutex_unlock(real_mutex) == 0 ? 0 : EINVAL;
}

int pthread_condattr_init(pthread_condattr_t *attr) {
    if (!attr) {
        return EINVAL;
    }
    *attr = (pthread_condattr_t)calloc(1u, sizeof(**attr));
    return *attr ? 0 : EINVAL;
}

int pthread_condattr_destroy(pthread_condattr_t *attr) {
    if (!attr || !*attr) {
        return EINVAL;
    }
    free(*attr);
    *attr = NULL;
    return 0;
}

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
    struct pthread_cond *pc;
    (void)attr;

    if (!cond) {
        return EINVAL;
    }
    pc = (struct pthread_cond *)calloc(1u, sizeof(*pc));
    if (!pc) {
        return EINVAL;
    }
    if (vibe_app_cond_init(&pc->cond) != 0) {
        free(pc);
        return EINVAL;
    }
    *cond = pc;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond) {
    if (!cond || !*cond) {
        return EINVAL;
    }
    free(*cond);
    *cond = NULL;
    return 0;
}

int pthread_cond_signal(pthread_cond_t *cond) {
    if (!cond || !*cond) {
        return EINVAL;
    }
    return vibe_app_cond_signal(&(*cond)->cond) == 0 ? 0 : EINVAL;
}

int pthread_cond_broadcast(pthread_cond_t *cond) {
    if (!cond || !*cond) {
        return EINVAL;
    }
    return vibe_app_cond_broadcast(&(*cond)->cond) == 0 ? 0 : EINVAL;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    vibe_app_mutex_t *real_mutex;
    if (!cond || !*cond || !mutex || !*mutex) {
        return EINVAL;
    }
    real_mutex = (vibe_app_mutex_t *)(uintptr_t)&(*mutex)->mutex;
    return vibe_app_cond_wait(&(*cond)->cond, real_mutex) == 0 ? 0 : EINVAL;
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *ts) {
    int timeout_ms;
    int rc;
    vibe_app_mutex_t *real_mutex;

    if (!cond || !*cond || !mutex || !*mutex) {
        return EINVAL;
    }
    timeout_ms = pthread_timedwait_ms(ts);
    if (timeout_ms < 0) {
        return EINVAL;
    }
    real_mutex = (vibe_app_mutex_t *)(uintptr_t)&(*mutex)->mutex;
    rc = vibe_app_cond_timedwait_ms(&(*cond)->cond, real_mutex, (unsigned int)timeout_ms);
    if (rc == 0) {
        return 0;
    }
    return (rc == 1) ? ETIMEDOUT : EINVAL;
}

int pthread_once(pthread_once_t *once, void (*init_routine)(void)) {
    if (!once || !init_routine) {
        return EINVAL;
    }
    if (once->state == PTHREAD_DONE_INIT) {
        return 0;
    }
    if (!once->mutex) {
        if (pthread_mutex_init(&once->mutex, NULL) != 0) {
            return EINVAL;
        }
    }
    pthread_mutex_lock(&once->mutex);
    if (once->state != PTHREAD_DONE_INIT) {
        init_routine();
        once->state = PTHREAD_DONE_INIT;
    }
    pthread_mutex_unlock(&once->mutex);
    return 0;
}
